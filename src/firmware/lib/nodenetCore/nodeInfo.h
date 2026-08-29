#pragma once

#include <cstdint>

#include <ArduinoJson.h>

enum class HardwareType : uint8_t
{
        FILTER = 0,
        FOCUSER = 1,
        ROTATOR = 2,
        IO8 = 3,
        IO16 = 4,
        ASCOMBRIDGE = 5,
        NODENET_SOC = 6,
        UNDEFINED = 255
};

struct NodeInfo {
  uint8_t addr;  
  char deviceId[12];  
  char instrumentName[30];  
  HardwareType hardwareType;
  JsonObject features;  // JSON object with features
  bool isMaster;
  unsigned long lastSeen;
  bool available = false;
  JsonDocument lastStatus; // Last status received from this node
};