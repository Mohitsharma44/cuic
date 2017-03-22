/*
   ===============
       C U I C
   ===============
CUSP Urban Imaging Camera

This code contains functions for 
- Enumerating the Interfaces on host system
- Enumerating the devices on every Interface
- Connect to the camera
- Start Capturing the Images


Author = Mohit Sharma
Date = July 26 2016
Copyright (c) CUSP

*/

#define PV_GUI_NOT_AVAILABLE

#include <sstream>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <memory>
#include <ctime>
#include <pthread.h>
#include <PvSampleUtils.h>
#include <PvSystem.h>
#include <PvInterface.h>
#include <PvDevice.h>
#include <PvDeviceGEV.h>
#include <PvDeviceU3V.h>
#include <PvStream.h>
#include <PvStreamGEV.h>
#include <PvStreamU3V.h>
#include <PvPipeline.h>
#include <PvBuffer.h>
#include <PvBufferWriter.h>
#include <PvConfigurationWriter.h>
#include <PvConfigurationReader.h>
#include "spdlog/spdlog.h"
#include "config.h"
#include <libconfig.h++>
#include <time.h>
#include <ctime>

using namespace libconfig;

// For casting to string
#define SSTR( x ) static_cast< std::ostringstream & >(( std::ostringstream() << std::dec << x ) ).str()

// Globals
// -- Device settings
#define BUFFER_COUNT ( 16 )
// -- Local Configuration 
#define DEVICE_CONFIGURATION_TAG ( "DeviceConfiguration" )
#define STREAM_CONFIGURATION_TAG ( "StreamConfiguration" )
#define STRING_INFORMATION_TAG ( "Copyright" )
#define COPYRIGHT ( "CUSP 2016" )
#define BASEPATH ("/mnt/ramdisk/")
#define IFNAME ("eth0")
// -- Image configuration
#define WHITE_BALANCE_AUTO ("Off")
#define HDR_ENABLE (0)
#define GAIN (1)
#define EXPOSURE (200000)



const int NUM_SECONDS = 10;

PV_INIT_SIGNAL_HANDLER();

// -- Function Prototypes
std::shared_ptr<spdlog::logger> initialize();
std::pair<int, int> findInterface();
//PvDevice *ConnectToDevice(const PvDeviceInfo *DeviceInfo);
PvDevice *ConnectToDevice(PvResult *Result, const PvDeviceInfo *DeviceInfo);
PvStream *OpenStream( const PvDeviceInfo *DeviceInfo );
void ConfigureStream( PvDevice *Device, PvStream *Stream);
PvPipeline *CreatePipeline( PvDevice *Device, PvStream *Stream);
void AcquireImages( std::string mac_addr, PvDevice *Device, PvStream *Stream, PvPipeline *pipeline, time_t timestamp);
// -- 

std::shared_ptr<spdlog::logger> initialize()
{
  // -- Setting up stdout and file logger
  std::vector<spdlog::sink_ptr> sinks;
  auto stdout_sink = spdlog::sinks::stdout_sink_mt::instance();
  auto color_sink = std::make_shared<spdlog::sinks::ansicolor_sink>(stdout_sink);  
  sinks.push_back(color_sink);
  sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_st>("/var/log/cuic/cuicCapture", "log", 23, 59));
  auto combined_logger = std::make_shared<spdlog::logger>("cuicCapture", sinks.begin(), sinks.end());
  //register it to access globally
  spdlog::register_logger(combined_logger);
  // Force flush to file when encountered error
  combined_logger->flush_on(spdlog::level::err);
  return combined_logger;
}

//const char *current_device;
std::shared_ptr<spdlog::logger> logger;
Config cfg;
// Struct to hold data/objects on/of every device
/*
struct device_data
{
  // A vector to store all the DeviceInfo
  std::vector<const PvDeviceInfo *>devinfo;
  // A vector to store all Devices
  std::vector<PvDevice *>vecDevices;
  // A vector to store the Streams
  std::vector<PvStream *>vecStreams;
  // A vector to store the pipelines
  std::vector<PvPipeline *>vecPipelines;
};
*/
struct device_data
{
  // Store Device Info
  std::string mac_addr;
  const PvDeviceInfo *Devinfo;
  // Store Device Objects
  PvDevice *Device;
  // Store Stream Objects
  PvStream *Stream;
  // Store Pipelines
  PvPipeline *Pipeline;
  // Store Communication-related settings
  PvGenParameterArray *CommParameters;
  // Store Device Settings
  PvGenParameterArray *DeviceParameters;
  // Store Stream Settings
  PvGenParameterArray *StreamParameters;
};

/*
void *capture_image(void *device_data_arg)
  {
    struct device_data *devd;
    devd = (struct device_data *) device_data_arg;
    while ( !PvKbHit() )
      {
	for(unsigned int devi = 0; devi < devd->Devinfo.size(); ++devi) 
	  {
	    const PvDeviceInfoGEV* DeviceInfoGEV = dynamic_cast<const PvDeviceInfoGEV*>( devd->Devinfo[devi] );
	    logger->info("Device "+SSTR(devi)+" : "+SSTR(DeviceInfoGEV->GetDisplayID().GetAscii()) );
	    current_device = DeviceInfoGEV->GetMACAddress().GetAscii();
	    AcquireImages( devd->vecDevices[devi], devd->vecStreams[devi], devd->vecPipelines[devi] );
	    sleep(2);
	  }
      }
    PvGetChar();
  }
*/

const char* toChar( double data )
{
  // Casting double to string
  stringstream ss;
  ss << data;
  const char* str = ss.str().c_str();
  return str;
}

void *captureImage(void *device_data_arg)
  {
    struct tm tm = {0};
    struct device_data *devdata;
    devdata = (struct device_data *) device_data_arg;
    while ( !PvKbHit() )
      {
	const PvDeviceInfoGEV* DeviceInfoGEV = dynamic_cast<const PvDeviceInfoGEV*>( devdata->Devinfo );
	//StoreConfiguration(devdata->Device, devdata->Stream, devdata->mac_addr);
	//sleep(0.5);
	PvPropertyList list;
	double camera_exposure_value;
	PvString exp_name, gain_name, hdr_name, wbal_name, lut_name;
	PvString balautowhite, hdren, luten, gain;
	PvConfigurationWriter configwriter;
	std::string config_fname;
	logger->info("Capturing from Device: "+SSTR(DeviceInfoGEV->GetDisplayID().GetAscii()) );
	PvGenParameter *ExpParameter  = devdata->DeviceParameters->Get("ExposureTime");
	PvGenParameter *GainParameter = devdata->DeviceParameters->GetFloat("Gain");
	PvGenParameter *WBalParameter = devdata->DeviceParameters->GetEnum("BalanceWhiteAuto");
	PvGenParameter *HDRParameter  = devdata->DeviceParameters->GetBoolean("HDREnable");
	PvGenParameter *LUTParameter  = devdata->DeviceParameters->GetBoolean("LUTEnable");

	// Get Current Setting values from camera
	static_cast<PvGenFloat *>(ExpParameter)->GetValue(camera_exposure_value);
	ExpParameter->GetName(exp_name);
	//static_cast<PvGenFloat *>(ExpParameter)->SetValue(EXPOSURE);

	gain = GainParameter->ToString();
	GainParameter->GetName(gain_name);

	balautowhite = WBalParameter->ToString();
	WBalParameter->GetName(wbal_name);

	hdren = HDRParameter->ToString();
	HDRParameter->GetName(hdr_name);

	luten = LUTParameter->ToString();
	LUTParameter->GetName(lut_name);

	// add the values to configuration file
	list.Add(PvProperty( gain_name, gain ));
	list.Add(PvProperty( wbal_name, balautowhite ));
	list.Add(PvProperty( hdr_name, hdren ));
	list.Add(PvProperty( lut_name, luten ));
	
	logger->info("ExposureTime is: "+SSTR(camera_exposure_value));
	/**
	// log to info
	logger->info("Balance Auto White: "+SSTR(balautowhite.GetAscii()));
	logger->info("HDRENABLE: "+SSTR(hdren.GetAscii()));
	logger->info("LUTEnable: "+SSTR(luten.GetAscii()));
	logger->info("Gain: "+SSTR(gain.GetAscii()));

	// Set Values
	logger->info("setting gain");
	static_cast<PvGenFloat *>(GainParameter)->SetValue(GAIN);
	logger->info("disable hdr");
	static_cast<PvGenBoolean *>(HDRParameter)->SetValue(HDR_ENABLE);	
	**/

	// Get Current system time and convert to time_t
	time_t now = time(0);
	// Convert current time to EST tm struct
	std::tm *local_tm = localtime(&now);
	/**
	try
	  {
	    // Parse the exposure values and time from configuration file
	    const Setting& root = cfg.getRoot();
	    const Setting &exposures = root["exposure_times"];
	    int exposure_count = exposures.getLength();
	    //logger->info("Total exposures: "+SSTR(exposure_count));
	    for (int i=0; i<exposure_count; i++)
	      {
		const Setting &exposure = exposures[i];
		int exp_val;
		std::string time_;
		
		exposure.lookupValue("time", time_);
		exposure.lookupValue("exposure", exp_val);
		// Parse time from config file to time_t
		const char *time_details = time_.c_str();
		strptime(time_details, "%H:%M:%S", &tm);
		time_t next_t = mktime(&tm);
		// create new time with only H:M:S from locatime
		local_tm->tm_mday = tm.tm_mday = 0;
		local_tm->tm_mon = tm.tm_mon = 0;
		local_tm->tm_year = tm.tm_year = 0;
		time_t compare_t = mktime(local_tm);
		//cout << (std::fabs(std::difftime(compare_t, next_t))) << endl;		
		//cout << compare_t << " ---- " << next_t << " ---- " << (std::fabs(std::difftime(compare_t, next_t))) << endl;
		// Check if current time is within 15 minutes of the time in configuration file		
		if (std::fabs(std::difftime(compare_t, next_t)) <= 900)
		  {
		    // If current camera exposure is different than configuration file:
		    if (camera_exposure_value != exp_val)
		      {
			logger->warn("Will Set exposure to: "+SSTR(exp_val));
			static_cast<PvGenFloat *>(ExpParameter)->SetValue(exp_val);
		      }
		  }
	      }
	  }
	catch(const SettingNotFoundException &nfex)
	  {
	    logger->error("Error in getting Settings from Configuration file");
	  }
	static_cast<PvGenFloat *>(ExpParameter)->GetValue(camera_exposure_value);
	logger->info("ExposureTime is: "+SSTR(camera_exposure_value));
	**/
	list.Add(PvProperty( (PvString)exp_name, (PvString) toChar(camera_exposure_value) ));
	configwriter.Store(&list, (PvString)"My Params");
	std::time_t timestamp = std::time(nullptr);
	config_fname = BASEPATH + devdata->mac_addr +  "-" + SSTR( timestamp ) + ".hdr";
	configwriter.Save( config_fname.c_str() );
	AcquireImages( devdata->mac_addr, devdata->Device, devdata->Stream, devdata->Pipeline, timestamp );
	sleep(5);
      }
    PvGetChar();
  }


int main(int, char*[])
{
  logger = initialize();
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%n] [%l] [thread %t] - %v");
  logger->info("Starting Up...");
  sleep (5);
  // Parse Configuration File
  try
    {
      cfg.readFile("/opt/pleora/ebus_sdk/Ubuntu-14.04-x86_64/share/samples/cuic/src/configuration.cfg");
    }
  catch(const ParseException &pex)
    {
      logger->error("Parse Error at: " + SSTR(pex.getFile()) + " : " + SSTR(pex.getLine()) + " - " + SSTR(pex.getError()));
      return(EXIT_FAILURE);
    }
  
  std::string name = cfg.lookup("name");
  logger->info("Config Name: "+SSTR(name));
  
  
  std::pair<int, int> inter;
  int interface_id, device_count;
  const PvDeviceInfo *DeviceInfo = NULL;
  PvDevice *Device = NULL;
  PvStream *Stream = NULL;
  PvPipeline *Pipeline = NULL;
  PvSystem System;
  PvResult Result;
  pthread_t thread1;
  int thread_retcode;
  //struct device_data devd;
  
  PV_SAMPLE_INIT();
  // Get interface_id and device count in that interface
  inter = findInterface();
  interface_id = inter.first;
  device_count = inter.second;
  // Find all the devices
  Result = System.Find();
  
  pthread_t threads[device_count];
  struct device_data devd[device_count];

  for (int i=0; i<device_count; i++)
    {
      const PvInterface* Interface = System.GetInterface( interface_id );
      const PvDeviceInfo *DeviceInfo = Interface->GetDeviceInfo( i );
      PvDeviceGEV DeviceGEV;
      //devd.devinfo.push_back(DeviceInfo);
      const PvDeviceInfoGEV* DeviceInfoGEV = dynamic_cast<const PvDeviceInfoGEV*>( DeviceInfo );
      devd[i].mac_addr = (PvString) DeviceInfoGEV->GetMACAddress().GetAscii();
      devd[i].Devinfo = DeviceInfo;
      devd[i].CommParameters = DeviceGEV.GetCommunicationParameters();

      // Connect to cameras
      Device = PvDevice::CreateAndConnect( devd[i].Devinfo, &Result);
      if ( Device == NULL )
	{
	  logger->warn(std::string("Unable to connect to: ") + DeviceInfo->GetDisplayID().GetAscii() );
	}
      logger->info(std::string("Connecting to Device: ") + DeviceInfo->GetDisplayID().GetAscii());
      //devd.vecDevices.push_back(Device);
      devd[i].Device = Device;
      devd[i].DeviceParameters = Device->GetParameters();
      
      // Open the Streams from Cameras
      Stream = PvStream::CreateAndOpen( DeviceInfo->GetConnectionID(), &Result );
      if ( Stream == NULL)
	{
	  logger->error(std::string("Cannot Open Stream ") + Result.GetCodeString().GetAscii() );
	}
      // Configure Streams
      PvDeviceGEV* GEVDevice = dynamic_cast<PvDeviceGEV *>( Device );
      if ( GEVDevice != NULL)
	{
	  PvStreamGEV* GEVStream = dynamic_cast<PvStreamGEV *>( Stream );
	  // Negotiate Packet size
	  GEVDevice->NegotiatePacketSize();
	  // Configure device streaming destination
	  GEVDevice->SetStreamDestination( GEVStream->GetLocalIPAddress(), GEVStream->GetLocalPort());
	}
      //devd.vecStreams.push_back(Stream);
      devd[i].Stream = Stream;
      devd[i].StreamParameters = Stream->GetParameters();

      // Create Pipeline for the stream
      PvPipeline *Pipeline = new PvPipeline( Stream );
      if (Pipeline != NULL)
	{
	  uint32_t payload_size = Device->GetPayloadSize();
	  // Set buffer count and Buffer size
	  Pipeline->SetBufferCount( BUFFER_COUNT );
	  Pipeline->SetBufferSize( payload_size );
	  //devd.vecPipelines.push_back(Pipeline);
	  devd[i].Pipeline = Pipeline;
	}
      // Load the configuration ONCE ONLY!
      //logger->warn("Restoring Configuration for: "+SSTR(devd[i].mac_addr));
      //RestoreConfiguration(devd[i].Device, devd[i].Stream, devd[i].mac_addr);
    }
  
  for (int i=0; i<device_count; i++)
    {
      pthread_create( &threads[i], NULL, captureImage, (void *)&devd[i] );
      if (thread_retcode) 
	{
	  logger->error("Unable to create thread: " + SSTR(thread_retcode));
	  exit(-1);
	}
      sleep(2);
    }

  // Join Threads
  for (int i=0; i<device_count; i++)
    {
      pthread_join( threads[i], NULL );
    }

  /*
  while ( !PvKbHit() )
    {
      for(unsigned int devi = 0; devi < devd.devinfo.size(); ++devi) 
	{
	  const PvDeviceInfoGEV* DeviceInfoGEV = dynamic_cast<const PvDeviceInfoGEV*>( devd.devinfo[devi] );
	  logger->info("Device "+SSTR(devi)+" : "+SSTR(DeviceInfoGEV->GetDisplayID().GetAscii()) );
	  current_device = DeviceInfoGEV->GetMACAddress().GetAscii();
	  AcquireImages( devd.vecDevices[devi], devd.vecStreams[devi], devd.vecPipelines[devi] );
	  sleep(2);
	}
      sleep(5);
    }
  PvGetChar();
  */

  // Shutdown everything
  /*
  for (unsigned int devi = 0; devi < devinfo.size(); ++devi)
    {
      delete vecPipelines[devi];
      vecStreams[devi]->Close();
      PvStream::Free( Stream );
      logger->debug("Disconnecting the Device ");
      vecDevices[devi]->Disconnect();
      PvDevice::Free( Device );
    }
  */
  /*
  // Connect to all devices found on particular interface
  while ( !PvKbHit() )
    {
      for (int i=0; i<device_count; i++)
	{
	  DeviceInfo = SelectDevice( &Result, interface_id, i );
	  
	  if ( DeviceInfo != NULL )
	    {
	      Device = ConnectToDevice( DeviceInfo );
	      if ( Device != NULL )
		{
		  Stream = OpenStream( DeviceInfo );
		  if ( Stream != NULL )
		    {
		      ConfigureStream( Device, Stream );
		      Pipeline = CreatePipeline( Device, Stream);
		      if ( Pipeline )
			{
			  AcquireImages( Device, Stream, Pipeline );
			  delete Pipeline;
			}
		      // Close Stream
		      logger->info("Closing Stream");
		      Stream->Close();
		      PvStream::Free( Stream );
		    }
		  // Disconnect the Device
		  logger->info("Disconnecting the Device ");
		  Device->Disconnect();
		  PvDevice::Free( Device );	  
		}
	 
		}
	  
	}
    }
  PvGetChar();
  */
  PV_SAMPLE_TERMINATE();
  
  return 0;
}


std::pair<int, int> findInterface()
{
  int gigEInterfaceId, deviceCount;
  logger->info("Finding Devices on Interface: "+SSTR(IFNAME));
  PvResult Result;
  //const PvDeviceInfo* lLastDeviceInfo = NULL;

  // Find all devices on the network
  PvSystem System;
  Result = System.Find();
  if ( !Result.IsOK() )
    {
      logger->error(std::string("PvSystem err in findDevices: ") + *Result.GetCodeString().GetAscii() );
      return std::make_pair(-1, -1);
    }

  // Go through all interfaces
  uint32_t InterfaceCount = System.GetInterfaceCount();
  //cout << "Interfaces on the system: "<< InterfaceCount << endl << endl;
  
  for (uint32_t x = 0; x < InterfaceCount; x++)
    {
      //cout << "Scanning Interface "<< x << endl;
      
      // Get Pointer to the interfaace
      const PvInterface* Interface = System.GetInterface( x );
      //cout << "Interface Id: " << x << endl;
      
      // For Network Adapter
      const PvNetworkAdapter* NIC = dynamic_cast<const PvNetworkAdapter*>( Interface );
      // For GigE devices

      if ( NIC != NULL && std::string(NIC->GetName().GetAscii()) == IFNAME)
	{
	  uint32_t DeviceCount = Interface->GetDeviceCount();
	  //cout << "Interface Name: " << NIC->GetName().GetAscii() << endl;
	  //cout << " - Physical Address: " << NIC->GetMACAddress().GetAscii() << endl;
	  //cout << " - IP Address: " << NIC->GetIPAddress().GetAscii() << endl << endl;
	  gigEInterfaceId = x;
	  deviceCount = DeviceCount;
	  //cout << x << deviceCount << endl;
	}
    }
  // Return a specific interface id
  return std::make_pair(gigEInterfaceId, deviceCount);
}


PvStream *OpenStream( const PvDeviceInfo *DeviceInfo )
{
  PvStream *Stream;
  PvResult Result;
  PvSystem System;

  Result = System.Find();
  // Open Stream to GigE or USB3
  logger->debug("Opening Stream to device ");
  //cout << "Opening Stream to device " << endl;
  Stream = PvStream::CreateAndOpen( DeviceInfo->GetConnectionID(), &Result );
  if ( Stream == NULL)
    {
      logger->error(std::string("Cannot Open Stream ") + Result.GetCodeString().GetAscii() );
      //cout << "Cannot Open Stream " << Result.GetCodeString().GetAscii() << endl;
    }
  return Stream;
}

void ConfigureStream( PvDevice *Device, PvStream *Stream)
{
  // Configuration is only for GigE devices
  PvDeviceGEV* GEVDevice = dynamic_cast<PvDeviceGEV *>( Device );
  if ( GEVDevice != NULL)
    {
      PvStreamGEV* GEVStream = dynamic_cast<PvStreamGEV *>( Stream );
      // Negotiate Packet size
      GEVDevice->NegotiatePacketSize();
      // Configure device streaming destination
      GEVDevice->SetStreamDestination( GEVStream->GetLocalIPAddress(), GEVStream->GetLocalPort());
      
    }
}


PvPipeline *CreatePipeline( PvDevice *Device, PvStream *Stream )
{
  // Create Pipeline for streaming data
  PvPipeline *Pipeline = new PvPipeline( Stream );
  
  if (Pipeline != NULL)
    {
      uint32_t payload_size = Device->GetPayloadSize();
      // Set buffer count and Buffer size
      Pipeline->SetBufferCount( BUFFER_COUNT );
      Pipeline->SetBufferSize( payload_size );
    }
  return Pipeline;
}


void AcquireImages( std::string mac_addr, PvDevice *Device, PvStream *Stream, PvPipeline *Pipeline, time_t timestamp )
{
   // Get Device Parameters to control streaming
   PvGenParameterArray *DeviceParams = Device->GetParameters();
   
   // Map Acquisition Start and Stop Commands
   PvGenCommand *Start = dynamic_cast<PvGenCommand *>( DeviceParams->Get( "AcquisitionStart" ));
   PvGenCommand *Stop = dynamic_cast<PvGenCommand *>( DeviceParams->Get( "AcquisitionStop" ));
   
   // Initialize Pipeline
   logger->debug("Starting Pipeline");
   Pipeline->Start();
   
   // Get Stream Parameters
   PvGenParameterArray *StreamParams = Stream->GetParameters();
   
   // Map sone GenICam stream stats counters
   PvGenFloat *FrameRate = dynamic_cast<PvGenFloat *>( StreamParams->Get( "AcquisitionRate" ));
   PvGenFloat *Bandwidth = dynamic_cast<PvGenFloat *>( StreamParams->Get( "Bandwidth" ));
   
   // Enable Streaming and Start Acquisition
   logger->debug("Enabling Streaming and Starting Acquisition");
   //cout << "Enabling Streaming and Starting Acquisition" << endl;
   Device->StreamEnable();
   Start->Execute();
   
   // Acquire Images until a Keypress
   char Doodle[] = "|\\-|-/";
   int DoodleIndex = 0;
   double FrameRateVal = 0.0;
   double BandwidthVal = 0.0;
   std::string meta_fname;
   std::string fname;
   
   //while ( !PvKbHit() )
   //  {
   PvBuffer *Buffer = NULL;
   PvResult OperationalResult;
   // Retrieve next buffer
   // sleep if exposure is high
   sleep(2);
   PvResult Result = Pipeline->RetrieveNextBuffer( &Buffer, 1000, &OperationalResult );
   if ( Result.IsOK() )
     {
       if ( OperationalResult.IsOK() )
	 {
	   PvPayloadType Type;
	   // -- Processing on Buffer!!!!
	   
	   // --
	   
	   FrameRate->GetValue( FrameRateVal );
	   Bandwidth->GetValue( BandwidthVal );
	   
	   // Display Width and Height of Image
	   uint32_t Width = 0, Height = 0;
	   // Tyoe of info in buffer
	   Type = Buffer->GetPayloadType();
	   
	   //cout << fixed << setprecision( 1 );
	   //cout << Doodle[ DoodleIndex ];
	   //cout << " BlockID: " << uppercase << hex << setfill( '0' ) << 
	   //  setw( 16 ) << Buffer->GetBlockID();
	   
	   if ( Type == PvPayloadTypeImage)
	     {
	       
	       // Parameters for storing metadata
	       PvConfigurationWriter ConfigWriter;
	       
	       //uint64_t timestamp = Buffer->GetReceptionTime();
	       //std::time_t timestamp = std::time(nullptr);
	       //cout << "Timestamp: " << timestamp << endl;
	       //meta_fname = SSTR( timestamp ) + ".hdr";
	       
	       // Store Metadata first
	       //   Store Device related info
	       //ConfigWriter.Store( Device, DEVICE_CONFIGURATION_TAG );
	       //   Store Stream related info
	       //ConfigWriter.Store( Stream, STREAM_CONFIGURATION_TAG );
	       //   CUSP String
	       //ConfigWriter.Store( CUSP_COPYRIGHT, STRING_INFORMATION_TAG );
	       // Write Metadata to the file
	       //ConfigWriter.Save( meta_fname.c_str() );
	       //sleep(2);
	       
	       // Get Image specific Buffer interface
	       PvImage *Image = Buffer->GetImage();
	       // Read Width and Height
	       Width = Image->GetWidth();
	       Height = Image->GetHeight();
	       logger->debug("Image Width: "+SSTR( Width ) + "Image Height: "+ SSTR( Height ));
	       //cout << " W: " << dec << Width << "H: " << Height;
	       //cout << "Saving Image ... ";
	      
	       // Tell device to Stop sending Images
	       Stop->Execute();
	       // Disable Streaming                                                               
	       //cout << "Disabling Streaming " << endl;
	       //Device->StreamDisable();
	       
	      
	       // Allocate memory of the size of the image
	       //Image->Alloc( Width, Height, PvPixelMono8 );
	       
	       // Fill the buffer
	       //uint8_t *DataPtr = Image->GetDataPointer();
	       //for ( uint32_t y = 0; y < Image->GetHeight(); y++ )
	       //  {
	       //    uint32_t HCount = y;
	       //   for ( uint32_t x = 0; x < Image->GetWidth(); x++ )
	       //	{
	       //	  *( DataPtr++ ) = ( HCount++ ) % 256;
	       //	}
	       //}
	       
	       PvBufferWriter BuffWriter;
	       
	       // For RAW, use PvBufferFormatRaw
	       // For TIFF, use PvBufferFormatTIFF
	       // For BMP, use PvBufferFormatBMP
	       fname = BASEPATH + mac_addr +  "-" + SSTR( timestamp ) + ".raw";
	       BuffWriter.Store( Buffer, fname.c_str(), PvBufferFormatRaw );
	       logger->info("Image Captured: "+SSTR(fname));
	       // Sleep before restarting
	       //sleep(2);
	       //delete &BuffWriter;
	       //delete &Image;
	       //delete &ConfigWriter;
	       // Enable Streaming                                                               
	       //cout << "Enable Streaming " << endl;
	       //Device->StreamEnable();
	       // Tell device to Start sending Images
	       //cout << "Sending Acquisition Start Command " << endl;
	       //Start->Execute();
	     }
	   else
	     {
	       logger->warn("Buffer Does not Contain Image");
	       //cout << "Buffer Does not Contain Image";
	     }
	   //cout << " " << FrameRateVal << "FPS: " << ( BandwidthVal / 1000000.0) << " Mb/s \r";
	   logger->debug("FPS: "+SSTR( FrameRateVal ) + "Bandwidth: " +SSTR( BandwidthVal/ 1000000.0) + " MB/s");
	 }
       else
	 {
	   // Operational Result is non OK
	   logger->error("Operational result is non OK: "+std::string(OperationalResult.GetCodeString().GetAscii()));
	   //cout << Doodle[ DoodleIndex ] << " " << OperationalResult.GetCodeString().GetAscii() << "\r";
	   quick_exit (EXIT_SUCCESS);
	 }
       
       // Release buffer back to pipeline
       logger->debug("Releasing buffer back to pipeline");
       Pipeline->ReleaseBuffer( Buffer );
     }
     else
     {
     //Retrieve Buffer failure
     //cout << Doodle[ DoodleIndex ] << " " << Result.GetCodeString().GetAscii() << "\r";
       logger->error("Error retrieving the Buffer");
       quick_exit (EXIT_SUCCESS);
     }
   //}
   logger->debug("Stopping");
   //PvGetChar(); // Flush Keybuffer for next stop
   
   // Tell device to Stop sending Images
   logger->debug("Sending Acquisition Stop Command");
   Stop->Execute();
   
   // Disable Streaming
   logger->debug("Disabling Stream");
   Device->StreamDisable();
   
   // Stop Pipeline
   logger->debug("Stopping Pipeline");
   Pipeline->Stop();
 }
