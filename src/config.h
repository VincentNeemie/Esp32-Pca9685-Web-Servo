#pragma once
#include <Arduino.h>

// Loads WiFi credentials from .env (PlatformIO env variables)
#ifndef STA_SSID
#define STA_SSID getenv("STA_SSID")
#endif
#ifndef STA_PASS
#define STA_PASS getenv("STA_PASS")
#endif
