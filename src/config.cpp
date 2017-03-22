#define PV_GUI_NOT_AVAILABLE
#include "config.h"

#define DEVICE_CONFIGURATION_TAG ( "DeviceConfiguration" )
#define STREAM_CONFIGURATION_TAG ( "StreamConfiguration" )
#define STRING_INFORMATION_TAG ( "StringInformation" )
#define COPYRIGHT ( "This is a test string" )
#define BASEPATH ( "/opt/pleora/ebus_sdk/Ubuntu-14.04-x86_64/share/samples/cuic/src/" )

#define SSTR( x ) static_cast< std::ostringstream & >(( std::ostringstream() << std::dec << x ) ).str()
/*
std::shared_ptr<spdlog::logger> confinitialize()
{
  // -- Setting up stdout and file logger
  std::vector<spdlog::sink_ptr> sinks;
  auto stdout_sink = spdlog::sinks::stdout_sink_mt::instance();
  auto color_sink = std::make_shared<spdlog::sinks::ansicolor_sink>(stdout_sink);  
  sinks.push_back(color_sink);
  sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_st>("logs/cuic_config", "log", 23, 59));
  auto combined_logger = std::make_shared<spdlog::logger>("CuicConfig", sinks.begin(), sinks.end());
  //register it to access globally
  spdlog::register_logger(combined_logger);
  // Force flush to file when encountered error
  combined_logger->flush_on(spdlog::level::err);
  return combined_logger;
}

std::shared_ptr<spdlog::logger> configlogger;
*/
// Write device configuration to a file //
bool StoreConfiguration(PvDevice *Device, PvStream *Stream, std::string mac_addr)
{
  //configlogger->info("Writing Device Configuration to: "+ SSTR(FILENAME));
  PvString mac;
  mac_addr += ".pvxml";
  mac = (PvString)mac_addr.c_str();
  PvConfigurationWriter Writer;
  Writer.Store( Device, DEVICE_CONFIGURATION_TAG );
  Writer.Store( Stream, STREAM_CONFIGURATION_TAG );
  Writer.Store( COPYRIGHT, STRING_INFORMATION_TAG );
  Writer.Save( mac );
  return true;
}

bool RestoreConfiguration(PvDevice *Device, PvStream *Stream, std::string mac_addr)
{
  std::string fname;
  PvString config_file;
  //configlogger->info("Resotring Device Configuration from: "+ SSTR(FILENAME));
  PvConfigurationReader Reader;
  fname = BASEPATH + mac_addr;
  fname += ".pvxml";
  cout << mac_addr << endl;
  PvString mac, lString;
  config_file = (PvString)fname.c_str();
  Reader.Load( config_file );
  Reader.Restore( DEVICE_CONFIGURATION_TAG, Device );
  Reader.Restore( STREAM_CONFIGURATION_TAG, Stream );
  Reader.Restore( STRING_INFORMATION_TAG, lString );
  return true;
}
