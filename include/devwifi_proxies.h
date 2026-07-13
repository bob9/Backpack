#pragma once

#if defined(AAT_BACKPACK)

#include <cstdint>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

void WebAatAppendConfig(ArduinoJson::JsonDocument &json);
void WebAatInit(AsyncWebServer &server);

#endif /* defined(AAT_BACKPACK) */

#if defined(TARGET_TIMER_BACKPACK) && defined(PLATFORM_ESP32)

#include <ESPAsyncWebServer.h>

void WebTimerTestInit(AsyncWebServer &server);

#endif /* defined(TARGET_TIMER_BACKPACK) && defined(PLATFORM_ESP32) */
