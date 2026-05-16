#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;
extern DynamicJsonDocument gRuntimeJson;

void frameworkSetup();
void frameworkLoop();
