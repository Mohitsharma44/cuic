/***
    Author: Mohit Sharma
    NYU CUSP Nov 2017
***/
#include <set>
#include <iostream>
#include <unistd.h>
#include "stdio.h"
#include "cordef.h"
#include "GenApi/GenApi.h"              //!< GenApi lib definitions.
#include "gevapi.h"                             //!< GEV lib definitions.
#include "SapX11Util.h"
#include "X_Display_utils.h"
#include <sched.h>
#include <algorithm>
#include <fstream>
// -- Specially for Logging
#include <plog/Log.h>
#include <plog/Appenders/ColorConsoleAppender.h>
// -- Specially for rabbitmq
#include <thread>
#include <chrono>
#include "asiohandler.h"
// -- Specially for parsing JSON
#include "json.hpp"

#define MAX_NETIF                                       8
#define MAX_CAMERAS_PER_NETIF   32
#define MAX_CAMERAS             (MAX_NETIF * MAX_CAMERAS_PER_NETIF)
// Enable/disable buffer FULL/EMPTY handling (cycling)
#define USE_SYNCHRONOUS_BUFFER_CYCLING  0
// Enable/disable transfer tuning (buffering, timeouts, thread affinity).
#define TUNE_STREAMING_THREADS 0
// Maximum number of buffers in camera
#define NUM_BUF 8

using json = nlohmann::json;


/*
  Valid Commands:
  a = abort the capture
  ? = return help string
  t = to check if system supports turbo mode (non functional for now)
  g = capture forever as fast as possible
  s = stop acquisition
  q = close all connections and release the camera
*/
std::set<char> VALID_COMMANDS = {'a', '?', 't', 'g', 's', 'q', 'x'};

// Declaring Functions
int camera_commands(void *context, std::string command);


// stupid logging enum
enum // Define log instances. Default is 0 and is omitted from this enum.
  {
    FileLog = 1
  };

template<typename ValueType>
std::string stringulate(ValueType v)
{
  std::ostringstream oss;
  oss << v;
  return oss.str();
}

typedef struct tagMY_CONTEXT
{
  GenApi::CNodeMapRef *Camera;
  GEV_CAMERA_HANDLE   camHandle;
  pthread_t           tid;
  BOOL                exit;
  PUINT8              bufAddress[NUM_BUF];
  int                 interval;
  int                 capture;
  bool                stop;
  bool                status;
}MY_CONTEXT, *PMY_CONTEXT;

static unsigned long us_timer_init( void )
{
  struct timeval tm;
  unsigned long msec;

  // Get the time and turn it into a millisecond counter.
  gettimeofday( &tm, NULL);

  msec = (tm.tv_sec * 1000000) + (tm.tv_usec);
  return msec;
}
static unsigned long ms_timer_init( void )
{
  struct timeval tm;
  unsigned long msec;

  // Get the time and turn it into a millisecond counter.
  gettimeofday( &tm, NULL);

  msec = (tm.tv_sec * 1000) + (tm.tv_usec / 1000);
  return msec;
}

static int ms_timer_interval_elapsed( unsigned long origin, unsigned long timeout)
{
  struct timeval tm;
  unsigned long msec;

  // Get the time and turn it into a millisecond counter.
  gettimeofday( &tm, NULL);

  msec = (tm.tv_sec * 1000) + (tm.tv_usec / 1000);

  // Check if the timeout has expired.
  if ( msec > origin )
    {
      return ((msec - origin) >= timeout) ? TRUE : FALSE;
    }
  else
    {
      return ((origin - msec) >= timeout) ? TRUE : FALSE;
    }
}


char GetKey()
{
  char key = getchar();
  while ((key == '\r') || (key == '\n'))
    {
      key = getchar();
    }
  return key;
}

void PrintMenu()
{
  printf("GRAB CTL   : [S]=stop, [1-9]=snap N, [G]=continuous, [A]=Abort\n");
  printf("MISC       : [Q]or[ESC]=end,         [T]=Toggle TurboMode (if available)\n");
}

//void * ImageSaveThread( void *context)
void * ImageSaveThread( void *context)
{
  MY_CONTEXT *saveContext = (MY_CONTEXT *)context;

  if (saveContext != NULL)
    {
      //LOG_FATAL << "... Im here ...";
      //LOG_FATAL << "My thread Id is: " << saveContext->tid;
      // While we are still running.
      while(!saveContext->exit)
        {
          GEV_BUFFER_OBJECT *img = NULL;
          GEV_STATUS status = 0;
          PUINT8 imgaddr = NULL;
          // Wait for images to be received
          status = GevWaitForNextImage(saveContext->camHandle, &img, 1000);
          if ((img != NULL) && (status == GEVLIB_OK))
            {
              LOG_VERBOSE << "Image Status: " << img->status;
              LOG_VERBOSE_(FileLog) << "Image Status: " << img->status;
              LOG_VERBOSE << "Buffer State: " << img->recv_size;
              LOG_VERBOSE_(FileLog) << "Buffer State: " << img->recv_size;
              LOG_VERBOSE << "Buffer ID: " << img->id;
              LOG_VERBOSE_(FileLog) << "Buffer ID: " << img->id;
              LOG_VERBOSE << "Buffer Height: " << img->h;
              LOG_VERBOSE_(FileLog) << "Buffer Height: " << img->h;
              LOG_VERBOSE << "Buffer Width: " << img->w;
              LOG_VERBOSE_(FileLog) << "Buffer Width: " << img->w;
              LOG_VERBOSE << "Buffer Depth: " << img->d;
              LOG_VERBOSE_(FileLog) << "Buffer Depth: " << img->d;
              LOG_VERBOSE << "Buffer Format: " << std::hex << img->format;
              LOG_VERBOSE_(FileLog) << "Buffer Format: " << std::hex << img->format;
              imgaddr = img->address;
              std::string fname = "test-";
              fname += stringulate(ms_timer_init());
              fname += ".raw";
              const char* filename = fname.c_str();
              LOG_INFO << "Saving file " << filename;
              LOG_INFO_(FileLog) << "Saving file " << filename;
              // Write the image data to file
              std::ofstream ot;
              ot.open(filename, std::ios::out|std::ios::binary);
              ot.write((const char *)imgaddr, img->recv_size);
              ot.flush();
              ot.close();
            }
#if USE_SYNCHRONOUS_BUFFER_CYCLING
          if (img != NULL)
            {
              // Release the buffer back to the image transfer process.
              GevReleaseImage( saveContext->camHandle, img);
            }
#endif
          sleep(saveContext->interval);
        }
      LOG_WARNING << "Terminating ImgSaveThread";
      LOG_WARNING_(FileLog) << "Terminating ImgSaveThread";
    }
  LOG_WARNING << "Thread " << saveContext->tid << " is exiting";
  LOG_WARNING_(FileLog) << "Thread " << saveContext->tid << " is exiting";
  pthread_exit(0);
}

int IsTurboDriveAvailable(GEV_CAMERA_HANDLE handle)
{
  LOG_VERBOSE << "checking if TurboDrive is available";
  LOG_VERBOSE_(FileLog) << "checking if TurboDrive is available";
  int type;
  UINT32 val = 0;

  if ( 0 == GevGetFeatureValue( handle, "transferTurboCurrentlyAbailable",  &type, sizeof(UINT32), &val) )
    {
      // Current / Standard method present - this feature indicates if TurboMode is available.
      // (Yes - it is spelled that odd way on purpose).
      return (val != 0);
    }
  else
    {
      // Legacy mode check - standard feature is not there try it manually.
      char pxlfmt_str[64] = {0};

      // Mandatory feature (always present).
      GevGetFeatureValueAsString( handle, "PixelFormat", &type, sizeof(pxlfmt_str), pxlfmt_str);

      // Set the "turbo" capability selector for this format.
      if ( 0 != GevSetFeatureValueAsString( handle, "transferTurboCapabilitySelector", pxlfmt_str) )
        {
          // Either the capability selector is not present or the pixel format is not part of the
          // capability set.
          // Either way - TurboMode is NOT AVAILABLE.....
          return 0;
        }
      else
        {
          // The capabilty set exists so TurboMode is AVAILABLE.
          // It is up to the camera to send TurboMode data if it can - so we let it.
          return 1;
        }
    }
  return 0;
}

static void OutputFeatureValuePair( const char *feature_name, const char *value_string, FILE *fp )
{
  if ( (feature_name != NULL)  && (value_string != NULL) )
    {
      // Feature : Value pair output (in one place in to ease changing formats or output method - if desired).
      LOG_VERBOSE << feature_name << " set to " << value_string;
      LOG_VERBOSE_(FileLog) << feature_name << " set to " << value_string;
      fprintf(fp, "%s %s\n", feature_name, value_string);
      //printf("%s --> %s\n", feature_name, value_string);
    }
}

static void OutputFeatureValues( const GenApi::CNodePtr &ptrFeature, FILE *fp )
{
  GenApi::CCategoryPtr ptrCategory(ptrFeature);
  if( ptrCategory.IsValid() )
    {
      GenApi::FeatureList_t Features;
      ptrCategory->GetFeatures(Features);
      for( GenApi::FeatureList_t::iterator itFeature=Features.begin(); itFeature!=Features.end(); itFeature++ )
        {
          OutputFeatureValues( (*itFeature), fp );
        }
    }
  else
    {
      // Store only "streamable" features (since only they can be restored).
      if ( ptrFeature->IsStreamable() )
        {
          // Create a selector set (in case this feature is selected)
          bool selectorSettingWasOutput = false;
          GenApi::CSelectorSet selectorSet(ptrFeature);

          // Loop through all the selectors that select this feature.
          // Use the magical CSelectorSet class that handles the
          //   "set of selectors that select this feature" and indexes
          // through all possible combinations so we can save all of them.
          selectorSet.SetFirst();
          do
            {
              GenApi::CValuePtr valNode(ptrFeature);
              if ( valNode.IsValid() && (GenApi::RW == valNode->GetAccessMode()) && (ptrFeature->IsFeature()) )
                {
                  // Its a valid streamable feature.
                  // Get its selectors (if it has any)
                  GenApi::FeatureList_t selectorList;
                  selectorSet.GetSelectorList( selectorList, true );

                  for ( GenApi::FeatureList_t ::iterator itSelector=selectorList.begin(); itSelector!=selectorList.end(); itSelector++ )
                    {
                      // Output selector : selectorValue as a feature : value pair.
                      selectorSettingWasOutput = true;
                      GenApi::CNodePtr selectedNode( *itSelector);
                      GenApi::CValuePtr selectedValue( *itSelector);
                      OutputFeatureValuePair(static_cast<const char *>(selectedNode->GetName()), static_cast<const char *>(selectedValue->ToString()), fp);
                    }
                  // Output feature : value pair for this selector combination
                  // It just outputs the feature : value pair if there are no selectors.
                  OutputFeatureValuePair(static_cast<const char *>(ptrFeature->GetName()), static_cast<const char *>(valNode->ToString()), fp);
                }

            } while( selectorSet.SetNext());
          // Reset to original selector/selected value (if any was used)
          selectorSet.Restore();

          // Save the original settings for any selector that was handled (looped over) above.
          if (selectorSettingWasOutput)
            {
              GenApi::FeatureList_t selectingFeatures;
              selectorSet.GetSelectorList( selectingFeatures, true);
              for ( GenApi::FeatureList_t ::iterator itSelector = selectingFeatures.begin(); itSelector != selectingFeatures.end(); ++itSelector)
                {
                  GenApi::CNodePtr selectedNode( *itSelector);
                  GenApi::CValuePtr selectedValue( *itSelector);
                  OutputFeatureValuePair(static_cast<const char *>(selectedNode->GetName()), static_cast<const char *>(selectedValue->ToString()), fp);
                }
            }
        }
    }
}

static void ValidateFeatureValues( const GenApi::CNodePtr &ptrFeature )
{
  GenApi::CCategoryPtr ptrCategory(ptrFeature);
  if( ptrCategory.IsValid() )
    {
      GenApi::FeatureList_t Features;
      ptrCategory->GetFeatures(Features);
      for( GenApi::FeatureList_t::iterator itFeature=Features.begin(); itFeature!=Features.end(); itFeature++ )
        {
          ValidateFeatureValues( (*itFeature) );
        }
    }
  else
    {
      // Issue a "Get" on the feature (with validate set to true).
      GenApi::CValuePtr valNode(ptrFeature);
      if ((GenApi::RW == valNode->GetAccessMode()) || (GenApi::RO == valNode->GetAccessMode()) )
        {
          int status = 0;
          try {
            valNode->ToString(true);
          }
          // Catch all possible exceptions from a node access.
          CATCH_GENAPI_ERROR(status);
          if (status != 0)
            {
              LOG_WARNING << "Validation failed for feature "
                          << static_cast<const char *>(ptrFeature->GetName());
              LOG_WARNING_(FileLog) << "Validation failed for feature "
                                    << static_cast<const char *>(ptrFeature->GetName());
            }
        }
    }
}

int CamFeatures(GenApi::CNodeMapRef *Camera, const char* filename, const char *command)
{
  LOG_VERBOSE << "Inside CamFeatures";
  LOG_VERBOSE_(FileLog) << "Inside CamFeatures";
  int error_count = 0;
  int feature_count = 0;
  GEV_STATUS status = 0;
  FILE *fp = NULL;
  LOG_DEBUG << "Put the camera in streaming feature mode";
  LOG_DEBUG_(FileLog) << "Put the camera in streaming feature mode";
  GenApi::CCommandPtr start = Camera->_GetNode("Std::DeviceFeaturePersistenceStart");
  if (start) {
    try {
      int done = FALSE;
      int timeout = 5;
      start->Execute();
      while(!done && (timeout-- > 0))
        {
          usleep(10000);
          done = start->IsDone();
        }
    }
    // Catch all possible exceptions from a node access.
    CATCH_GENAPI_ERROR(status);

    // Traverse the node map and dump all the { feature value } pairs.
    if ( status == 0 )
      {
        if (strcmp(command, "store") == 0)
          {
            // Opening file to dump all the features
            if (filename == NULL)
              {
                fp = stdout;
              }
            fp = fopen(filename, "w");
            if (fp == NULL)
              {
                LOG_FATAL << "Error opening the configuration file";
                LOG_FATAL_(FileLog) << "Error opening the configuration file";
              }
            // Find the standard "Root" node and dump the features.
            GenApi::CNodePtr pRoot = Camera->_GetNode("Root");
            LOG_DEBUG << "Writing feature values to the file";
            LOG_DEBUG_(FileLog) << "Writing feature values to the file";
            OutputFeatureValues(pRoot, fp);
          }
        else if (strcmp(command, "load") == 0)
          {
            // Opening file to dump all the features
            if (filename == NULL)
              {
                fp = stdout;
              }
            fp = fopen(filename, "r");
            if (fp == NULL)
              {
                LOG_FATAL << "Error opening the configuration file";
                LOG_FATAL_(FileLog) << "Error opening the configuration file";
              }
            char feature_name[MAX_GEVSTRING_LENGTH+1] = {0};
            char value_str[MAX_GEVSTRING_LENGTH+1] = {0};
            while ( 2 == fscanf(fp, "%s %s", feature_name, value_str) )
              {
                status = 0;
                // Find node and write the feature string (without validation).
                GenApi::CNodePtr pNode = Camera->_GetNode(feature_name);
                if (pNode)
                  {
                    GenApi::CValuePtr valNode(pNode);
                    try {
                      valNode->FromString(value_str, false);
                    }
                    // Catch all possible exceptions from a node access.
                    CATCH_GENAPI_ERROR(status);
                    if (status != 0)
                      {
                        error_count++;
                        LOG_WARNING << "Error restoring feature " << feature_name
                                    << " : with value : " << value_str;
                        LOG_WARNING_(FileLog) << "Error restoring feature " << feature_name
                                              << " : with value : " << value_str;
                      }
                    else
                      {
                        feature_count++;
                      }
                  }
                else
                  {
                    error_count++;
                    LOG_WARNING << "Error restoring feature " << feature_name
                                << " : with value : " << value_str;
                    LOG_WARNING_(FileLog) << "Error restoring feature " << feature_name
                                          << " : with value : " << value_str;
                  }
              }
            if (error_count == 0)
              {
                LOG_INFO << feature_count << " Features loaded successfully";
                LOG_INFO_(FileLog) << feature_count << " Features loaded successfully";
              }
            else
              {
                LOG_INFO << feature_count << " Features loaded successfully : "
                         << error_count << " Features had errors";
                LOG_INFO_(FileLog) << feature_count << " Features loaded successfully : "
                                   << error_count << " Features had errors";
              }
          }
      }

    // End the "streaming feature mode".
    LOG_DEBUG << "Ending the streaming mode";
    LOG_DEBUG_(FileLog) << "Ending the streaming mode";
    GenApi::CCommandPtr end = Camera->_GetNode("Std::DeviceFeaturePersistenceEnd");
    if ( end  )
      {
        try {
          int done = FALSE;
          int timeout = 5;
          end->Execute();
          while(!done && (timeout-- > 0))
            {
              usleep(10000);
              done = end->IsDone();
            }
        }
        // Catch all possible exceptions from a node access.
        CATCH_GENAPI_ERROR(status);
      }

    // Done - close the file
    fclose(fp);
    // Validate if features are loaded into the camera
    if (strcmp(command, "load") == 0)
      {
        if (status == 0)
          {
            // Iterate through all of the features calling "Get" with validation enabled.
            // Find the standard "Root" node and dump the features.
            GenApi::CNodePtr pRoot = Camera->_GetNode("Root");
            LOG_DEBUG << "Validating Features set on camera";
            LOG_DEBUG_(FileLog) << "Validating Features set on camera";
            ValidateFeatureValues( pRoot );
          }
      }
    return 0;
  }
  else
    {
      return 1;
    }
}


int cleanup(MY_CONTEXT *context)
{
  UINT16 status;
  int numBuffers = NUM_BUF;
  MY_CONTEXT *Cleanupcontext = context;
  //PUINT8 bufAddress[NUM_BUF] = {Cleanupcontext->&bufAddress};
  GEV_CAMERA_HANDLE handle = Cleanupcontext -> camHandle;
  LOG_INFO << "Aborting and discarding the queued buffer(s)";
  LOG_INFO_(FileLog) << "Aborting and discarding the queued buffer(s)";
  GevAbortImageTransfer(handle);
  //status = GevFreeImageTransfer(handle);
  //status = GevSetImageParameters(handle, maxWidth,  maxHeight, x_offset,  y_offset, format);

  status = GevFreeImageTransfer( handle);

  for (int i = 0; i < numBuffers; i++)
    {
      LOG_VERBOSE << "Clearing buffer " << i;
      LOG_VERBOSE_(FileLog) << "Clearing buffer " << i;
      //free(bufAddress[i]);
      free(Cleanupcontext->bufAddress[i]);
    }
  LOG_VERBOSE << "Closing connection to the camera";
  LOG_VERBOSE_(FileLog) << "Closing connection to the camera";
  GevCloseCamera(&handle);

  LOG_VERBOSE << "Shutting down the API";
  LOG_VERBOSE_(FileLog) << "Shutting down the API";
  // Close down the API.
  GevApiUninitialize();

  LOG_VERBOSE << "Closing socket connection";
  LOG_VERBOSE_(FileLog) << "Closing socket connection";
  // Close socket API
  _CloseSocketAPI ();     // must close API even on error

  LOG_INFO << "Connection to the camera closed successfully";
  LOG_INFO_(FileLog) << "Connection to the camera closed successfully";

  return 0;
}

//MY_CONTEXT & initialize_cameras()
//MY_CONTEXT * initialize_cameras(MY_CONTEXT & context)
//void initialize_cameras(void *MY_CONTEXT)
const MY_CONTEXT & initialize_cameras(MY_CONTEXT & context)
{

  GEV_DEVICE_INTERFACE  pCamera[MAX_CAMERAS] = {0};
  UINT16 status;
  //MY_CONTEXT context = {0};
  int numCamera = 0;
  int camIndex = 0;
  pthread_t  tid;
  int done = FALSE;
  int turboDriveAvailable = 0;

  LOG_VERBOSE << "Initializing";
  LOG_VERBOSE_(FileLog) << "Initializing";
  // Greetings
  printf ("\nGigE Vision Library Using Teledyne Dalsa (%s)\n", __DATE__);
  printf ("Copyright (c) 2017, CUSP.\nAll rights reserved.\n\n");
  //===================================================================================
  // Set default options for the library.
  {
    GEVLIB_CONFIG_OPTIONS options = {0};

    GevGetLibraryConfigOptions( &options);
    //options.logLevel = GEV_LOG_LEVEL_OFF;
    //options.logLevel = GEV_LOG_LEVEL_TRACE;
    options.logLevel = GEV_LOG_LEVEL_NORMAL;
    GevSetLibraryConfigOptions( &options);
  }

  //====================================================================================
  // DISCOVER Cameras
  //
  // Get all the IP addresses of attached network cards.
  LOG_VERBOSE << "Discovering Cameras on the network";
  LOG_VERBOSE_(FileLog) << "Discovering Cameras on the network";
  status = GevGetCameraList( pCamera, MAX_CAMERAS, &numCamera);

  LOG_INFO << numCamera <<" camera(s) on the network";
  LOG_INFO_(FileLog) << numCamera <<" camera(s) on the network";

  // Select the first camera found (unless the command line has a parameter = the camera index)
  if (camIndex != -1)
    {
      //====================================================================
      // Connect to Camera
      //
      //
      int i;
      UINT32 height = 0;
      UINT32 width = 0;
      UINT32 x_offset = 0;
      UINT32 y_offset = 0;
      UINT32 format = 0;
      UINT32 maxHeight = 1600;
      UINT32 maxWidth = 2048;
      UINT32 maxDepth = 2;
      UINT32 size;
      int numBuffers = NUM_BUF;
      PUINT8 bufAddress[NUM_BUF];
      GEV_CAMERA_HANDLE handle = NULL;
      float exposure = 0;
      float gain = 0;
      float framerate = 0;

      //====================================================================
      // Open the camera.
      LOG_DEBUG << "Opening the connection to the camera";
      LOG_DEBUG_(FileLog) << "Opening the connection to the camera";
      status = GevOpenCamera( &pCamera[camIndex], GevExclusiveMode, &handle);
      if (status == 0)
        {
          //=================================================================
          // Initiliaze access to GenICam features via Camera XML File
          LOG_VERBOSE << "Accessing camera XML file for GenICam features";
          LOG_VERBOSE_(FileLog) << "Accessing camera XML file for GenICam features";
          status = GevInitGenICamXMLFeatures( handle, TRUE);
          if (status == GEVLIB_OK)
            {
              // Get the name of XML file name back (example only - in case you need it somewhere).
              char xmlFileName[MAX_PATH] = {0};
              status = GevGetGenICamXML_FileName( handle, (int)sizeof(xmlFileName), xmlFileName);
              if (status == GEVLIB_OK)
                {
                  LOG_DEBUG << "XML stored as " << xmlFileName;
                  LOG_DEBUG_(FileLog) << "XML stored as " << xmlFileName;
                }
            }
        }

      // Go on to adjust some API related settings (for tuning / diagnostics / etc....).
      if ( status == 0 )
        {
          GEV_CAMERA_OPTIONS camOptions = {0};

          // Adjust the camera interface options if desired (see the manual)
          GevGetCameraInterfaceOptions( handle, &camOptions);
          //camOptions.heartbeat_timeout_ms = 90000;                // For debugging (delay camera timeout while in debugger)

#if TUNE_STREAMING_THREADS
          // Some tuning can be done here. (see the manual)
          camOptions.streamFrame_timeout_ms = 1001;                             // Internal timeout for frame reception.
          camOptions.streamNumFramesBuffered = 4;                         // Buffer frames internally.
          camOptions.streamMemoryLimitMax = 64*1024*1024;         // Adjust packet memory buffering limit.
          camOptions.streamPktSize = 9180;                                                        // Adjust the GVSP packet size.
          camOptions.streamPktDelay = 10;                                                       // Add usecs between packets to pace arrival at NIC.

          // Assign specific CPUs to threads (affinity) - if required for better performance.
          {
            int numCpus = _GetNumCpus();
            if (numCpus > 1)
              {
                camOptions.streamThreadAffinity = numCpus-1;
                camOptions.serverThreadAffinity = numCpus-2;
              }
          }
#endif
          // Write the adjusted interface options back.
          GevSetCameraInterfaceOptions( handle, &camOptions);

          //=====================================================================
          // Get the GenICam FeatureNodeMap object and access the camera features.
          GenApi::CNodeMapRef *Camera = static_cast<GenApi::CNodeMapRef*>(GevGetFeatureNodeMap(handle));

          if (Camera)
            {
              // Access some features using the bare GenApi interface methods
              try
                {
                  // Save All Features
                  const char* filename = "config_fname.csv";
                  const char* store_command = "store";
                  const char* load_command = "load";
                  LOG_DEBUG << "Saving All Features";
                  LOG_DEBUG_(FileLog) << "Saving All Features";
                  status = CamFeatures(Camera, filename, store_command);
                  if (status != 0){
                    LOG_WARNING << "Couldn't save features";
                    LOG_WARNING_(FileLog) << "Couldn't save features";
                  }
                  LOG_DEBUG << "Loading All Features ";
                  LOG_DEBUG_(FileLog) << "Loading All Features ";
                  status = CamFeatures(Camera, filename, load_command);
                  if (status != 0){
                    LOG_WARNING << "Couldn't load features";
                    LOG_WARNING_(FileLog) << "Couldn't load features";
                  }

                  // Datatypes of Features
                  GenApi::CNodePtr pNode = NULL;
                  GenApi::CIntegerPtr ptrIntNode = 0;
                  GenApi::CEnumerationPtr ptrEnumNode = NULL;
                  GenApi::CFloatPtr ptrFloatNode = 0;

                  // --------- Set Parameters Manually if need be
                  /*
                  // Disable auto brightness
                  //pNode = Camera->_GetNode("autoBrightnessMode");
                  //GenApi::CValuePtr autobrightval(pNode);
                  //autobrightval->FromString("0", false);


                  // Disable auto exposure
                  //pNode = Camera->_GetNode("ExposureAuto");
                  //GenApi::CValuePtr autoexpval(pNode);
                  //autoexpval->FromString("0", false);

                  // Set Gain -- Will not work with Analog Mode
                  pNode = Camera->_GetNode("Gain");
                  GenApi::CValuePtr expVal1(pNode);
                  expVal1->FromString("1.0", false);
                  */


                  // Set ExposureTime
                  pNode = Camera->_GetNode("ExposureTime");
                  GenApi::CValuePtr expVal(pNode);
                  expVal->FromString("90000.0", false);

                  // Set Framerate
                  pNode = Camera->_GetNode("AcquisitionFrameRate");
                  GenApi::CValuePtr expval2(pNode);
                  expval2->FromString("0.5", false);

                  // --------- Get Parameters
                  // Get Width and Height
                  //GenApi::CIntegerPtr ptrIntNode = Camera->_GetNode("Width");
                  ptrIntNode = Camera->_GetNode("Width");
                  width = (UINT32) ptrIntNode->GetValue();

                  ptrIntNode = Camera->_GetNode("Height");
                  height = (UINT32) ptrIntNode->GetValue();

                  // Get Pixel Format
                  //GenApi::CEnumerationPtr ptrEnumNode = Camera->_GetNode("PixelFormat") ;
                  ptrEnumNode = Camera->_GetNode("PixelFormat");
                  format = (UINT32)ptrEnumNode->GetIntValue();

                  // Get ExposureTime
                  //GenApi::CFloatPtr ptrExposureNode = Camera->_GetNode("ExposureTime");
                  ptrFloatNode = Camera->_GetNode("ExposureTime");
                  exposure = (float)ptrFloatNode->GetValue();

                  // Get Gain
                  //GenApi::CFloatPtr ptrGainNode = Camera->_GetNode("Gain");
                  ptrFloatNode = Camera->_GetNode("Gain");
                  gain = (float)ptrFloatNode->GetValue();

                  // Get Framerate
                  ptrFloatNode = Camera->_GetNode("AcquisitionFrameRate");
                  framerate = (float)ptrFloatNode->GetValue();

                  // Get AutoExposure Value
                  //ptrEnumNode = Camera->_GetNode("ExposureAuto");
                  //autoexposure = (UINT32)ptrEnumNode->GetIntValue();
                  //printf("MSS: ExposureAuto: %d\n", autoexposure);

                  // Get AutoBrightness Value
                  //ptrEnumNode = Camera->_GetNode("autoBrightnessMode");
                  //autobright = (UINT32)ptrEnumNode->GetIntValue();
                  //printf("MSS: autoBrightnessMode: %d\n", autobright);

                }
              // Catch all possible exceptions from a node access.
              CATCH_GENAPI_ERROR(status);
              //status = 0;
            }

          if (status == 0)
            {
              //=================================================================
              // Set up a grab/transfer from this camera
              //
              LOG_VERBOSE << "Camera ROI set";
              LOG_VERBOSE_(FileLog) << "Camera ROI set";
              LOG_DEBUG << "Height = " << height;
              LOG_DEBUG_(FileLog) << "Height = " << height;
              LOG_DEBUG << "Width = " << width;
              LOG_DEBUG_(FileLog) << "Width = " << width;
              LOG_DEBUG  << "PixelFormat (val) = " << std::hex <<format;
              LOG_DEBUG_(FileLog)  << "PixelFormat (val) = " << std::hex <<format;
              LOG_DEBUG  << "Exposure = " << exposure;
              LOG_DEBUG_(FileLog)  << "Exposure = " << exposure;
              LOG_DEBUG  << "Gain = " << gain;
              LOG_DEBUG_(FileLog)  << "Gain = " << gain;
              LOG_DEBUG  << "Framerate = " << framerate;
              LOG_DEBUG_(FileLog)  << "Framerate = " << framerate;

              maxHeight = height;
              maxWidth = width;
              maxDepth = GetPixelSizeInBytes(format);

              // Allocate image buffers
              size = maxDepth * maxWidth * maxHeight;
#define IMGSIZE (size)

              for (i = 0; i < numBuffers; i++)
                {
                  bufAddress[i] = (PUINT8)malloc(size);
                  memset(bufAddress[i], 0, size);

                }
              // Boost application RT response (not too high since GEV library boosts data receive thread to max allowed)
              // SCHED_FIFO can cause many unintentional side effects.
              // SCHED_RR has fewer side effects.
              // SCHED_OTHER (normal scheduler) is not too bad afer all.
              if (0)
                {
                  struct sched_param param = {0};
                  param.sched_priority = (sched_get_priority_max(SCHED_FIFO) - sched_get_priority_min(SCHED_FIFO)) / 2;
                  sched_setscheduler(0, SCHED_FIFO, &param); // Don't care if it fails since we can't do anyting about it.
                }

#if USE_SYNCHRONOUS_BUFFER_CYCLING
              // Initialize a transfer with synchronous buffer handling.
              status = GevInitImageTransfer( handle, SynchronousNextEmpty, numBuffers, bufAddress);
#else
              // Initialize a transfer with asynchronous buffer handling.
              status = GevInitImageTransfer( handle, Asynchronous, numBuffers, bufAddress);
#endif

              // Make sure the transfer settings are consistent.
              //if (status == GEVLIB_OK)
              //  {
              // Todo
              //  }
              // Create a thread to receive images from the API and save them.
              context.Camera = Camera;
              context.camHandle = handle;
              context.exit = FALSE;
              context.tid = tid;
              for (i = 0; i< numBuffers; i++)
                {
                  context.bufAddress[i] = bufAddress[i];
                }
              pthread_create(&tid, NULL, ImageSaveThread, &context);
              // Call the main command loop or the example.
              PrintMenu();
              /*
                LOG_INFO << "Aborting and discarding the queued buffer(s)";
                LOG_INFO_(FileLog) << "Aborting and discarding the queued buffer(s)";
                GevAbortImageTransfer(handle);
                status = GevFreeImageTransfer(handle);
                //status = GevSetImageParameters(handle, maxWidth,  maxHeight, x_offset,  y_offset, format);

                status = GevFreeImageTransfer( handle);

                for (i = 0; i < numBuffers; i++)
                {
                LOG_VERBOSE << "Clearing buffer " << i;
                LOG_VERBOSE_(FileLog) << "Clearing buffer " << i;
                free(bufAddress[i]);
                }
              */
            }
          /*
            LOG_VERBOSE << "Closing connection to the camera";
            LOG_VERBOSE_(FileLog) << "Closing connection to the camera";
            GevCloseCamera(&handle);
          */
        }
      else
        {
          LOG_FATAL << "Error opening camera - " << std::hex << status;
          LOG_FATAL_(FileLog) << "Error opening camera - " << std::hex << status;
          cleanup(&context);
        }
    }
  /*
    LOG_VERBOSE << "Shutting down the API";
    LOG_VERBOSE_(FileLog) << "Shutting down the API";
    // Close down the API.
    GevApiUninitialize();

    LOG_VERBOSE << "Closing socket connection";
    LOG_VERBOSE_(FileLog) << "Closing socket connection";
    // Close socket API
    _CloseSocketAPI ();     // must close API even on error

    LOG_INFO << "Connection to the camera closed successfully";
    LOG_INFO_(FileLog) << "Connection to the camera closed successfully";
  */
  //int resp = camera_commands(&context, "832040");
  //LOG_WARNING << "Received response: " << resp;
  //while(true){
  //  usleep(10000);
  //}
  //context.exit = true;
  //pthread_join( context.tid, NULL);
  //cleanup(&context);
  //LOG_FATAL << "context: " << context;
  //return &context;
  LOG_VERBOSE << "Initialization succeeded.";
  LOG_VERBOSE_(FileLog) << "Initialization succeeded.";
}

int camera_commands(void *context, std::string command)
{

  LOG_INFO << "Command Received is: " << command;
  LOG_INFO_(FileLog) << "Command Received is: " << command;
  MY_CONTEXT *Commandcontext = (MY_CONTEXT *)context;
  GEV_CAMERA_HANDLE handle = Commandcontext->camHandle;
  UINT16 status;
  int i;
  int turboDriveAvailable = 0;
  int numBuffers = NUM_BUF;
  //PUINT8 bufAddress[NUM_BUF];
  //UINT8 bufAddress = {Commandcontext->bufAddress};
  UINT32 format = 0;
  UINT32 size = IMGSIZE;

  // Explicitly parsing for it to be compatible with new features
  auto parsed_commands = json::parse(command);
  Commandcontext->capture = parsed_commands["capture"];
  Commandcontext->interval = parsed_commands["interval"];
  Commandcontext->stop = parsed_commands["stop"];
  Commandcontext->status = parsed_commands["status"];

  if (parsed_commands["exposure"] != -1){
    if (Commandcontext->Camera)
      {
        float exposure = 0;
        GenApi::CFloatPtr ptrFloatNode = 0;
        ptrFloatNode = Commandcontext->Camera->_GetNode("ExposureTime");
        exposure = (UINT32) ptrFloatNode->GetValue();
      }
  }

  //if (std::stoi(command) == 30)
  if (Commandcontext->status)
    {
      LOG_INFO << "I shall return some status -- " << Commandcontext->status;
    }

  if (Commandcontext->stop)
    {
      LOG_INFO << "Stopping Image transfer";
      LOG_INFO_(FileLog) << "Stopping Image transfer";
      GevStopImageTransfer(handle);
    }

  if (Commandcontext->capture)
    {
      if (Commandcontext->capture > 0)
        {
          int c = Commandcontext->capture;
          for (i = 0; i < numBuffers; i++)
            {
              LOG_VERBOSE << "allocating memory for buffer " << i;
              LOG_VERBOSE_(FileLog) << "allocating memory for buffer " << i;
              //memset(bufAddress[i], 0, size);
              memset(Commandcontext->bufAddress[i], 0, size);
            }
          Commandcontext->exit = false;
          LOG_DEBUG << "Starting image transfer for " << c << " frames";
          LOG_DEBUG_(FileLog) << "Starting image transfer for " << c << " frames";
          status = GevStartImageTransfer( handle, (UINT32)(c));
          if (status != 0) {
            LOG_WARNING << "Error starting grab - " << std::hex << status;
            LOG_WARNING_(FileLog) << "Error starting grab - " << std::hex << status;
          }
          else
            {
              LOG_DEBUG << "Ready to grab frames \n";
              LOG_DEBUG_(FileLog) << "Ready to grab frames \n";
            }
        }
      else if (Commandcontext->capture == -1)
        {
          for (i = 0; i < numBuffers; i++)
            {
              LOG_VERBOSE << "allocating memory for buffer " << i;
              LOG_VERBOSE_(FileLog) << "allocating memory for buffer " << i;
              //memset(bufAddress[i], 0, size);
              memset(Commandcontext->bufAddress[i], 0, size);
            }
          Commandcontext->exit = false;
          LOG_DEBUG << "Starting continuous image transfer";
          LOG_DEBUG_(FileLog) << "Starting continuous image transfer";
          status = GevStartImageTransfer( handle, -1);
          if (status != 0) {
            LOG_WARNING << "Error starting grab - " << std::hex << status;
            LOG_WARNING_(FileLog) << "Error starting grab - " << std::hex << status;
          }
        }
      else
        {
          LOG_WARNING << "There is no point in saving 0 images :/ ";
          return -1;
        }
    }
  /*
    bool command_is_in = VALID_COMMANDS.find(command[0]) != VALID_COMMANDS.end();
    bool command_is_digit = isdigit(command[0]);
    if (command_is_in | command_is_digit)
    {
    char c = command[0];
    if ((c == 'T') || (c =='t'))
    {
    // See if TurboDrive is available.
    turboDriveAvailable = IsTurboDriveAvailable(handle);
    if (turboDriveAvailable)
    {
    int type;
    UINT32 val = 1;
    GevGetFeatureValue(handle, "transferTurboMode", &type, sizeof(UINT32), &val);
    val = (val == 0) ? 1 : 0;
    GevSetFeatureValue(handle, "transferTurboMode", sizeof(UINT32), &val);
    GevGetFeatureValue(handle, "transferTurboMode", &type, sizeof(UINT32), &val);
    if (val == 1)
    {
    LOG_INFO << "TurboMode Enabled";
    LOG_INFO_(FileLog) << "TurboMode Enabled";
    }
    else
    {
    LOG_INFO << "TurboMode Disabled";
    LOG_INFO_(FileLog) << "TurboMode Disabled";
    }
    }
    else
    {
    LOG_WARNING << "*** TurboDrive is NOT Available for this device/pixel format combination ***";
    LOG_WARNING_(FileLog) << "*** TurboDrive is NOT Available for this device/pixel format combination ***";
    }
    }

    // Stop
    if ((c == 'S') || (c =='s') || (c  == '0'))
    {
    LOG_INFO << "Stopping Image transfer";
    LOG_INFO_(FileLog) << "Stopping Image transfer";
    GevStopImageTransfer(handle);
    }
    //Abort
    if ((c == 'A') || (c =='a'))
    {
    LOG_INFO << "Aborting and discarding the queued buffer(s)";
    LOG_INFO_(FileLog) << "Aborting and discarding the queued buffer(s)";
    GevAbortImageTransfer(handle);
    }
    // Snap N (1 to 9 frames)
    if ((c >= '1')&&(c <='9'))
    {
    for (i = 0; i < numBuffers; i++)
    {
    LOG_VERBOSE << "allocating memory for buffer " << i;
    LOG_VERBOSE_(FileLog) << "allocating memory for buffer " << i;
    //memset(bufAddress[i], 0, size);
    memset(Commandcontext->bufAddress[i], 0, size);
    }
    LOG_DEBUG << "Starting image transfer for " << c << " frames";
    LOG_DEBUG_(FileLog) << "Starting image transfer for " << c << " frames";
    status = GevStartImageTransfer( handle, (UINT32)(c-'0'));
    if (status != 0) {
    LOG_WARNING << "Error starting grab - " << std::hex << status;
    LOG_WARNING_(FileLog) << "Error starting grab - " << std::hex << status;
    }
    else
    {
    LOG_DEBUG << "Ready to grab frames \n";
    LOG_DEBUG_(FileLog) << "Ready to grab frames \n";
    }
    }
    // Continuous grab.
    if ((c == 'G') || (c =='g'))
    {
    for (i = 0; i < numBuffers; i++)
    {
    LOG_VERBOSE << "allocating memory for buffer " << i;
    LOG_VERBOSE_(FileLog) << "allocating memory for buffer " << i;
    //memset(bufAddress[i], 0, size);
    memset(Commandcontext->bufAddress[i], 0, size);
    }
    LOG_DEBUG << "Starting continuous image transfer";
    LOG_DEBUG_(FileLog) << "Starting continuous image transfer";
    status = GevStartImageTransfer( handle, -1);
    if (status != 0) {
    LOG_WARNING << "Error starting grab - " << std::hex << status;
    LOG_WARNING_(FileLog) << "Error starting grab - " << std::hex << status;
    }
    }

    if (c == '?')
    {
    PrintMenu();
    }

    if ((c == 'x') || (c == 'x'))
    {
    Commandcontext->interval = 5;
    }


    if ((c == 0x1b) || (c == 'q') || (c == 'Q'))
    {
    LOG_INFO << "Stopping image transfer";
    LOG_INFO_(FileLog) << "Stopping image transfer";
    GevStopImageTransfer(handle);
    Commandcontext->exit = TRUE;
    // Joining is crapping out! But the thread has explicit
    // call to pthread_exit(0) so we are good. Tested.
    // But this should be looked into.
    //pthread_join( Commandcontext->tid, NULL);
    cleanup(Commandcontext);
    }

    return 0;
    }
    else
    {
    return -1;
    }
  */
  return 0;
}


int main(int argc, char* argv[])
{
  // Log file appender (fname, fsize<10mbytes>, #backups)
  static plog::RollingFileAppender<plog::TxtFormatter> fileAppender("/var/log/cuic/teledyneCapture.log", 10000000, 30);
  // colored logging to console (because, why not!)
  // If color is not needed, replace by plog::ConsoleAppender
  static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
  // log to console and to file
  plog::init(plog::debug, &consoleAppender);
  plog::init<FileLog>(plog::debug, &fileAppender);

  // -------- AMQP TESTING v2.6.2
  boost::asio::io_service ioService;
  AsioHandler handler(ioService);
  handler.connect("localhost", 5672);
  const char* cuic_command = NULL;
  MY_CONTEXT cuicContext = {0};
  initialize_cameras(cuicContext);

  AMQP::Connection connection(&handler, AMQP::Login("guest", "guest"), "/");

  AMQP::Channel channel(&connection);
  channel.setQos(1);
  channel.declareQueue("rpc_queue");
  channel.consume("")
    .onReceived([&channel, &cuicContext](const AMQP::Message &message,
                                         uint64_t deliveryTag,
                                         bool redelivered)
                {
                  //MY_CONTEXT cuicContext = {0};
                  const auto body = message.message();
                  //const std::string body = message.message();
                  //LOG_INFO << " -- IN MAIN -- " << cuicContext.tid;
                  //std::cout<<" [.] fib("<<body<<")";
                  LOG_VERBOSE<<" ... CorId"<<message.correlationID()<<std::endl;
                  LOG_VERBOSE_(FileLog)<<" ... CorId"<<message.correlationID()<<std::endl;
                  //AMQP::Envelope env(std::to_string(fib(std::stoi(body))));
                  //AMQP::Envelope env(std::to_string(camera_commands(std::to_string(fib(std::stoi(body))))));
                  AMQP::Envelope env(std::to_string(camera_commands(&cuicContext, std::string(body))));
                  env.setCorrelationID(message.correlationID());
                  channel.publish("", message.replyTo(), env);
                  //std::cout << "ENV MESSAGE: " << env.message() << std::endl;
                  channel.ack(deliveryTag);
                })
    .onSuccess([&channel](const std::string &consumertag){
        LOG_DEBUG <<"Channel ready for messages";
        LOG_DEBUG_(FileLog) <<"Channel ready for messages";
      });

  //std::cout << " [x] Awaiting RPC requests" << std::endl;
  LOG_INFO << " [x] Awaiting RPC requests";
  LOG_INFO_(FileLog) << " [x] Awaiting RPC requests";
  ioService.run();
  //cleanup(&cuicContext);
  //LOG_WARNING << "Testing in Main .... thread id" << Commandcontext->tid;

  //pthread_create(&tid, NULL, ImageSaveThread, &context);
  //while(true){
  //  usleep(100000);
  //}

  return 0;
}
