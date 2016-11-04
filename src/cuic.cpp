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

// For casting int to string
#define SSTR( x ) static_cast< std::ostringstream & >(( std::ostringstream() << std::dec << x ) ).str()

// Globals
#define BUFFER_COUNT ( 16 )
#define DEVICE_CONFIGURATION_TAG ( "DeviceConfiguration" )
#define STREAM_CONFIGURATION_TAG ( "StreamConfiguration" )
#define STRING_INFORMATION_TAG ( "Copyright" )
#define CUSP_COPYRIGHT ( "CUSP 2016" )

PV_INIT_SIGNAL_HANDLER();

// -- Function Prototypes
int findDevices();
const PvDeviceInfo *SelectDevice();
PvDevice *ConnectToDevice(const PvDeviceInfo *DeviceInfo);
PvStream *OpenStream( const PvDeviceInfo *DeviceInfo );
void ConfigureStream( PvDevice *Device, PvStream *Stream);
PvPipeline *CreatePipeline( PvDevice *Device, PvStream *Stream);
void AcquireImages( PvDevice *Device, PvStream *Stream, PvPipeline *pipeline);
// -- 

int main()
{
  const PvDeviceInfo *DeviceInfo = NULL;
  PvDevice *Device = NULL;
  PvStream *Stream = NULL;
  PvPipeline *Pipeline = NULL;

  PV_SAMPLE_INIT();
  cout << "---- ----- ----" << endl;

  findDevices();
  DeviceInfo = SelectDevice();
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
	      cout << "Closing Stream " << endl;
	      Stream->Close();
	      PvStream::Free( Stream );
	    }
	  // Disconnect the Device
	  cout << "Disconnecting the Device " << endl;
	  Device->Disconnect();
	  PvDevice::Free( Device );	  
	}
    }
  

  cout << "---- ---- ----" << endl;

  PV_SAMPLE_TERMINATE();

  return 0;
}


int findDevices()
{
  PvResult Result;
  //const PvDeviceInfo* lLastDeviceInfo = NULL;

  // Find all devices on the network
  PvSystem System;
  Result = System.Find();
  if ( !Result.IsOK() )
    {
      cout << "PvSystem err in findDevices: "<< Result.GetCodeString().GetAscii();
      return -1;
    }

  // Go through all interfaces
  uint32_t InterfaceCount = System.GetInterfaceCount();
  cout << "Interfaces on the system: "<< InterfaceCount << endl << endl;

  for (uint32_t x = 0; x < InterfaceCount; x++)
    {
      //cout << "Scanning Interface "<< x << endl;
      
      // Get Pointer to the interfaace
      const PvInterface* Interface = System.GetInterface( x );
      cout << "Interface Id: " << x << endl;
      
      // For Network Adapter
      const PvNetworkAdapter* NIC = dynamic_cast<const PvNetworkAdapter*>( Interface );
      if ( NIC != NULL)
	{
	  cout << "Interface Name: " << NIC->GetName().GetAscii() << endl;
	  cout << " - Physical Address: " << NIC->GetMACAddress().GetAscii() << endl;
	  cout << " - IP Address: " << NIC->GetIPAddress().GetAscii() << endl << endl;
	}

      // For USB Controller
      const PvUSBHostController* USB = dynamic_cast<const PvUSBHostController*>( Interface );
      if ( USB != NULL)
	{
	  cout << "Interface Name: " << USB->GetName().GetAscii() << endl << endl;
	}
      
      

      // Enumerate all deices on every Interface
      uint32_t DeviceCount = Interface->GetDeviceCount();
      for ( uint32_t y = 0; y < DeviceCount; y++ )
	{
	  const PvDeviceInfo *DeviceInfo = Interface->GetDeviceInfo( y );
	  cout << " - Device: " << y << endl;
	  cout << " | .. Id: " << DeviceInfo->GetDisplayID().GetAscii() << endl;
	  cout << " | .. Serial Number: " << DeviceInfo->GetSerialNumber().GetAscii() << endl;
	  cout << " | .. Vendor Name: " << DeviceInfo->GetVendorName().GetAscii() << endl;
	  cout << " | .. Model Name: " << DeviceInfo->GetModelName().GetAscii() << endl;

	  const PvDeviceInfoGEV* DeviceInfoGEV = dynamic_cast<const PvDeviceInfoGEV*>( DeviceInfo );
	  const PvDeviceInfoU3V* DeviceInfoU3V = dynamic_cast<const PvDeviceInfoU3V*>( DeviceInfo );
	  const PvDeviceInfoUSB* DeviceInfoUSB = dynamic_cast<const PvDeviceInfoUSB*>( DeviceInfo );

	  if ( DeviceInfoGEV != NULL)
	    {
	      // GigE Vision devices
	      cout << " | .. MAC address: " << DeviceInfoGEV->GetMACAddress().GetAscii() << endl;
	      cout << " | .. IP Address: " << DeviceInfoGEV->GetIPAddress().GetAscii() << endl;
	      cout << " | .. Manufacturer Info: " << DeviceInfoGEV->GetManufacturerInfo().GetAscii() << endl;
	      if (DeviceInfoGEV->IsConfigurationValid() != true)
		{
		  cout << "Does not have a valid IP configuration. Provide the IP in the same subnet as the interface" << endl;
		}
	      cout << endl;
	    }

	  else if (DeviceInfoU3V != NULL)
	    {
	      // USB3 Device
	      cout << " | .. GUID: " << DeviceInfoU3V->GetDeviceGUID().GetAscii() << endl;
	      cout << " | .. Speed: " << DeviceInfoU3V->GetSpeed() << endl;
	      if (DeviceInfoU3V->IsSuperSpeedSupported() != true)
		{
		  cout << "Superspeed is not supported. It could be due to: " << endl;
		  cout << "1. Camera is not connected to USB 3 port (look for SS port)" << endl;
		  cout << "2. USB3 drivers not installed" << endl;
		  cout << "The device, if works, will not run at its maximum FPS" << endl;
		}
	      cout << endl;
	    }
	  
	  else if (DeviceInfoUSB != NULL)
	    {
	      // USB Device
	      cout << "No Support for USB 2.0 devices" << endl << endl;
	    }
	  else 
	    {
	      cout << "No Device Found."<< endl << endl;
	    }
	}
    }
  return 0;
}



const PvDeviceInfo *SelectDevice()
{
  PvSystem System;
  PvResult Result;
 
  int device_id = 0, interface_id = 0;

  Result = System.Find();
  if ( !Result.IsOK() )
    {
      cout << "PvSystem err in connectToDevice: " << Result.GetCodeString().GetAscii();
    }
  //cout << "Enter the Interface id to connect to " << endl;
  //cin >> interface_id;
  //cout << "Enter the Device id to connect to " << endl;
  //cin >> device_id;
  
  const PvInterface* Interface = System.GetInterface( interface_id );
  const PvDeviceInfo *DeviceInfo = Interface->GetDeviceInfo( device_id );

  return DeviceInfo;
}


PvDevice *ConnectToDevice( const PvDeviceInfo *DeviceInfo)
{
  PvSystem System;
  PvDevice *Device;
  PvResult Result;
  
  
  Result = System.Find();
  cout << "Connecting to Device: " << DeviceInfo->GetDisplayID().GetAscii() << endl;
  
  // Creates and connects to the device controller
  Device = PvDevice::CreateAndConnect( DeviceInfo, &Result);
  if ( Device == NULL )
    {
      cout << "Unable to connect to: " << DeviceInfo->GetDisplayID().GetAscii() << endl;
    }
  else
    {
      cout << "Connected to: " << DeviceInfo->GetDisplayID().GetAscii() << endl;
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
  cout << "Opening Stream to device " << endl;
  Stream = PvStream::CreateAndOpen( DeviceInfo->GetConnectionID(), &Result );
  if ( Stream == NULL)
    {
      cout << "Cannot Open Stream " << Result.GetCodeString().GetAscii() << endl;
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
  cout << "Starting Pipeline" << endl;
  Pipeline->Start();

  // Get Stream Parameters
  PvGenParameterArray *StreamParams = Stream->GetParameters();

  // Map sone GenICam stream stats counters
  PvGenFloat *FrameRate = dynamic_cast<PvGenFloat *>( StreamParams->Get( "AcquisitionRate" ));
  PvGenFloat *Bandwidth = dynamic_cast<PvGenFloat *>( StreamParams->Get( "Bandwidth" ));

  // Enable Streaming and Start Acquisition
  cout << "Enabling Streaming and Starting Acquisition" << endl;
  Device->StreamEnable();
  Start->Execute();
  
  // Acquire Images until a Keypress
  char Doodle[] = "|\\-|-/";
  int DoodleIndex = 0;
  double FrameRateVal = 0.0;
  double BandwidthVal = 0.0;
  std::string meta_fname;
  std::string fname;

  cout << endl << " < Press any key to Stop Streaming> " << endl;
  int co = 0;
  while ( !PvKbHit() )
    {
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
		  cout << " W: " << dec << Width << "H: " << Height;
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
		  
		  // For RAW, use PvBufferFormatRAW
		  // For TIFF, use PvBufferFormatTIFF
		  // For BMP, use PvBufferFormatBMP
		  // changing from co to timestamp
		  fname = "/mnt/ramdisk/" + SSTR( timestamp ) + ".tiff";
		  BuffWriter.Store( Buffer, fname.c_str(), PvBufferFormatTIFF );
		  
		  // Sleep before restarting
		  sleep(0.2);	
		  //delete &BuffWriter;
		  //delete &Image;
		  //delete &ConfigWriter;
                  // Enable Streaming                                                               
                  //cout << "Enable Streaming " << endl;
                  //Device->StreamEnable();
		  // Tell device to Start sending Images
                  //cout << "Sending Acquisition Start Command " << endl;
                  Start->Execute();
		  

		}
		  else
		    {
		      cout << "Buffer Does not Contain Image";
		    }
	      cout << " " << FrameRateVal << "FPS: " << ( BandwidthVal / 1000000.0) << " Mb/s \r";
	    }
	  else
	    {
	      // Operational Result is non OK
	      cout << Doodle[ DoodleIndex ] << " " << OperationalResult.GetCodeString().GetAscii() << "\r";
	    }
	  
	  // Release buffer back to pipeline
	  Pipeline->ReleaseBuffer( Buffer );
	}
      else
	{
	  //Retrieve Buffer failure
	  cout << Doodle[ DoodleIndex ] << " " << Result.GetCodeString().GetAscii() << "\r";
	}
      
      ++DoodleIndex %=6;
      co++;
    }
  cout << "Exiting While" << endl;
  PvGetChar(); // Flush Keybuffer for next stop
  cout << endl << endl;

  // Tell device to Stop sending Images
  cout << "Sending Acquisition Stop Command " << endl;
  Stop->Execute();
  
  // Disable Streaming
  cout << "Disabling Streaming " << endl;
  Device->StreamDisable();

  // Stop Pipeline
  cout << "Stopping Pipeline " << endl;
  Pipeline->Stop();
}
