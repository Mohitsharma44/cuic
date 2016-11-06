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
#include <unistd.h>

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

#include "spdlog/spdlog.h"
#include <iostream>
#include <memory>

// For casting int to string
#define SSTR( x ) static_cast< std::ostringstream & >(( std::ostringstream() << std::dec << x ) ).str()

// Globals
#define BUFFER_COUNT ( 16 )
#define DEVICE_CONFIGURATION_TAG ( "DeviceConfiguration" )
#define STREAM_CONFIGURATION_TAG ( "StreamConfiguration" )
#define STRING_INFORMATION_TAG ( "Copyright" )
#define CUSP_COPYRIGHT ( "CUSP 2016" )
#define IFNAME ("eth0")

PV_INIT_SIGNAL_HANDLER();

// -- Function Prototypes
std::shared_ptr<spdlog::logger> initialize();
std::pair<int, int> findInterface();
const PvDeviceInfo *SelectDevice( int interface_id, int device_id );
PvDevice *ConnectToDevice(const PvDeviceInfo *DeviceInfo);
PvStream *OpenStream( const PvDeviceInfo *DeviceInfo );
void ConfigureStream( PvDevice *Device, PvStream *Stream);
PvPipeline *CreatePipeline( PvDevice *Device, PvStream *Stream);
void AcquireImages( PvDevice *Device, PvStream *Stream, PvPipeline *pipeline);
// -- 

std::shared_ptr<spdlog::logger> initialize()
{
  // -- Setting up stdout and file logger
  std::vector<spdlog::sink_ptr> sinks;
  auto stdout_sink = spdlog::sinks::stdout_sink_mt::instance();
  auto color_sink = std::make_shared<spdlog::sinks::ansicolor_sink>(stdout_sink);  
  sinks.push_back(color_sink);
  sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_st>("logs/cuicLogger", "log", 23, 59));
  auto combined_logger = std::make_shared<spdlog::logger>("cuicLogger", sinks.begin(), sinks.end());
  //register it to access globally
  spdlog::register_logger(combined_logger);
  // Force flush to file when encountered error
  combined_logger->flush_on(spdlog::level::err);
  return combined_logger;
}

std::shared_ptr<spdlog::logger> logger;

int main(int, char*[])
{
  logger = initialize();
  std::pair<int, int> inter;
  int interface_id, device_count;
  const PvDeviceInfo *DeviceInfo = NULL;
  PvDevice *Device = NULL;
  PvStream *Stream = NULL;
  PvPipeline *Pipeline = NULL;

  PV_SAMPLE_INIT();
  // Get interface_id and device count in that interface
  inter = findInterface();
  interface_id = inter.first;
  device_count = inter.second;
  cout << interface_id << device_count << endl;
  
  // Connect to all devices found on particular interface
  while ( !PvKbHit() )
    {
      for (int i=0; i<device_count; i++)
	{
	  DeviceInfo = SelectDevice( interface_id, i );
	  
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
  PV_SAMPLE_TERMINATE();
  
  return 0;
}


std::pair<int, int> findInterface()
{
  int gigEInterfaceId, deviceCount;
  logger->info("Finding Devices on Interface");
  PvResult Result;
  //const PvDeviceInfo* lLastDeviceInfo = NULL;

  // Find all devices on the network
  PvSystem System;
  Result = System.Find();
  if ( !Result.IsOK() )
    {
      logger->error(std::string("PvSystem err in findDevices: ") + Result.GetCodeString().GetAscii() );
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
	  cout << x << deviceCount << endl;
	}
    }
  // Return a specific interface id
  return std::make_pair(gigEInterfaceId, deviceCount);
}


const PvDeviceInfo *SelectDevice( int interface_id, int device_id )
{
  PvSystem System;
  PvResult Result;
 
  //int device_id = 1;//, interface_id = 2;

  Result = System.Find();
  if ( !Result.IsOK() )
    {
      logger->error(std::string("PvSystem err in connectToDevice: ") + Result.GetCodeString().GetAscii() );
      //cout << "PvSystem err in connectToDevice: " << Result.GetCodeString().GetAscii();
    }
  const PvInterface* Interface = System.GetInterface( interface_id );
  const PvDeviceInfo *DeviceInfo = Interface->GetDeviceInfo( device_id );
  const PvDeviceInfoGEV* DeviceInfoGEV = dynamic_cast<const PvDeviceInfoGEV*>( DeviceInfo );
  logger->info("Device "+SSTR(device_id)+" : "+SSTR(DeviceInfoGEV->GetDisplayID().GetAscii()) );
  /*
  uint32_t DeviceCount = Interface->GetDeviceCount();
  for ( uint32_t y = 0; y < DeviceCount; y++ )
    {
      const PvDeviceInfo *DeviceInfo = Interface->GetDeviceInfo( y );
      cout << DeviceInfo;
      //cout << " - Device: " << y << endl;
      //cout << " | .. Id: " << DeviceInfo->GetDisplayID().GetAscii() << endl;
      //cout << " | .. Serial Number: " << DeviceInfo->GetSerialNumber().GetAscii() << endl;
      //cout << " | .. Vendor Name: " << DeviceInfo->GetVendorName().GetAscii() << endl;
      //cout << " | .. Model Name: " << DeviceInfo->GetModelName().GetAscii() << endl;
      
      const PvDeviceInfoGEV* DeviceInfoGEV = dynamic_cast<const PvDeviceInfoGEV*>( DeviceInfo );
      const PvDeviceInfoU3V* DeviceInfoU3V = dynamic_cast<const PvDeviceInfoU3V*>( DeviceInfo );
      const PvDeviceInfoUSB* DeviceInfoUSB = dynamic_cast<const PvDeviceInfoUSB*>( DeviceInfo );
      
      if ( DeviceInfoGEV != NULL)
	{
	  logger->info("Device "+SSTR(y)+" : "+SSTR(DeviceInfoGEV->GetDisplayID().GetAscii()) );
	  // GigE Vision devices
	  
	  //cout << " | .. MAC address: " << DeviceInfoGEV->GetMACAddress().GetAscii() << endl;
	  //cout << " | .. IP Address: " << DeviceInfoGEV->GetIPAddress().GetAscii() << endl;
	  //cout << " | .. Manufacturer Info: " << DeviceInfoGEV->GetManufacturerInfo().GetAscii() << endl;
	  
	  if (DeviceInfoGEV->IsConfigurationValid() != true)
	    {
	      logger->error("Does not have a valid IP configuration. Provide the IP in the same subnet as the interface");
	    }
	}
      else 
	{
	  logger->error("No Device Found");
	}
    }
  */
  //cout << "Enter the Interface id to connect to " << endl;
  //cin >> interface_id;
  //cout << "Enter the Device id to connect to " << endl;
  //cin >> device_id;
  //const PvDeviceInfo *DeviceInfo = Interface->GetDeviceInfo( device_id );
  return DeviceInfo;
}


PvDevice *ConnectToDevice( const PvDeviceInfo *DeviceInfo)
{
  PvSystem System;
  PvDevice *Device;
  PvResult Result;
  
  
  Result = System.Find();
  logger->info(std::string("Connecting to Device: ") + DeviceInfo->GetDisplayID().GetAscii());
  //cout << "Connecting to Device: " << DeviceInfo->GetDisplayID().GetAscii() << endl;
  
  // Creates and connects to the device controller
  Device = PvDevice::CreateAndConnect( DeviceInfo, &Result);
  if ( Device == NULL )
    {
      //cout << "Unable to connect to: " << DeviceInfo->GetDisplayID().GetAscii() << endl;
      logger->info(std::string("Unable to connect to: ") + DeviceInfo->GetDisplayID().GetAscii() );
    }
  else
    {
      //cout << "Connected to: " << DeviceInfo->GetDisplayID().GetAscii() << endl;
      //logger->info(std::string("Connected to: ") +DeviceInfo->GetDisplayID().GetAscii() );
      // REMOVE IT!!!!
      //cout << "Disconnecting from: " << DeviceInfo->GetDisplayID().GetAscii() << endl;
      //PvDevice::Free( Device );
        
      return Device;

    }
}


PvStream *OpenStream( const PvDeviceInfo *DeviceInfo )
{
  PvStream *Stream;
  PvResult Result;
  PvSystem System;

  Result = System.Find();
  // Open Stream to GigE or USB3
  logger->info("Opening Stream to device ");
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



void AcquireImages( PvDevice *Device, PvStream *Stream, PvPipeline *Pipeline )
{

  // Get Device Parameters to control streaming
  PvGenParameterArray *DeviceParams = Device->GetParameters();

  // Map Acquisition Start and Stop Commands
  PvGenCommand *Start = dynamic_cast<PvGenCommand *>( DeviceParams->Get( "AcquisitionStart" ));
  PvGenCommand *Stop = dynamic_cast<PvGenCommand *>( DeviceParams->Get( "AcquisitionStop" ));

  // Initialize Pipeline
  logger->info("Starting Pipeling");
  Pipeline->Start();

  // Get Stream Parameters
  PvGenParameterArray *StreamParams = Stream->GetParameters();

  // Map sone GenICam stream stats counters
  PvGenFloat *FrameRate = dynamic_cast<PvGenFloat *>( StreamParams->Get( "AcquisitionRate" ));
  PvGenFloat *Bandwidth = dynamic_cast<PvGenFloat *>( StreamParams->Get( "Bandwidth" ));

  // Enable Streaming and Start Acquisition
  logger->info("Enabling Streaming and Starting Acquisition");
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
	  
	  cout << fixed << setprecision( 1 );
	  cout << Doodle[ DoodleIndex ];
	  cout << " BlockID: " << uppercase << hex << setfill( '0' ) << 
	    setw( 16 ) << Buffer->GetBlockID();
	  
	  if ( Type == PvPayloadTypeImage)
	    {
	      // Parameters for storing metadata
	      PvConfigurationWriter ConfigWriter;
	      
	      uint64_t timestamp = Buffer->GetReceptionTime();
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
	      
	      
	      // Get Image specific Buffer interface
	      PvImage *Image = Buffer->GetImage();
	      // Read Width and Height
	      Width = Image->GetWidth();
	      Height = Image->GetHeight();
	      logger->debug("Image Width: "+SSTR( Width ) + "Image Height: "+ SSTR( Height ));
	      //cout << " W: " << dec << Width << "H: " << Height;
	      //cout << "Saving Image ... ";
	      
	      // Tell device to Stop sending Images
	      //cout << "Sending Acquisition Stop Command " << endl;
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
	      // changing from co to timestamp
	      //fname = "/opt/pleora/ebus_sdk/Ubuntu-14.04-x86_64/share/samples/cuic/src/" + SSTR( timestamp ) + ".raw";
	      //BuffWriter.Store( Buffer, fname.c_str(), PvBufferFormatRaw );
	      
	      // Sleep before restarting
	      //sleep(0.2);	
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
	}
      
      // Release buffer back to pipeline
      logger->debug("Releasing buffer back to pipeline");
      Pipeline->ReleaseBuffer( Buffer );
    }
  else
    {
      //Retrieve Buffer failure
      cout << Doodle[ DoodleIndex ] << " " << Result.GetCodeString().GetAscii() << "\r";
    }
  
  //}
  logger->warn("Stopping");
  //PvGetChar(); // Flush Keybuffer for next stop
  
  // Tell device to Stop sending Images
  logger->warn("Sending Acquisition Stop Command");
  Stop->Execute();
  
  // Disable Streaming
  logger->warn("Disabling Stream");
  Device->StreamDisable();

  // Stop Pipeline
  logger->warn("Stopping Pipeline");
  Pipeline->Stop();
}
