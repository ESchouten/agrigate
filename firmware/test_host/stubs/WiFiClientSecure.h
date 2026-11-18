// Minimal stand-ins so main.cpp can be syntax-checked on a PC (not the real libraries).
#pragma once
#include "WiFi.h"
struct WiFiClientSecure : WiFiClient { void setInsecure(); void setHandshakeTimeout(unsigned long); using WiFiClient::connect; int connect(const char*, uint16_t, int32_t); };
