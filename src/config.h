#ifndef CONFIG_H
#define CONFIG_H

#include <sstream>
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

//std::shared_ptr<spdlog::logger> confinitialize();
bool StoreConfiguration(PvDevice *Device, PvStream *Stream, std::string mac_addr);
bool RestoreConfiguration(PvDevice *Device, PvStream *Stream, std::string mac_addr);

#endif
