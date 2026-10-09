#pragma once
// Test-side controls for the stand-ins (see fakes.cpp).
#include <ctime>
#include <string>
void fakesReset();                 // everything back to power-on defaults
void setEpoch(time_t utc);         // "NTP synced": time() = utc, advancing with millis()
void mqttInject(const std::string &topic, const std::string &payload);   // broker -> device
