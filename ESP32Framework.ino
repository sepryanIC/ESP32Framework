#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#endif
#include <ElegantOTA.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>

#define FW_ENABLE_OTA true
#define FW_ENABLE_WEB_TERMINAL false
#define FW_ENABLE_CREDENTIAL false
#define FW_ENABLE_MESH false
#define FW_ENABLE_ESPNOW false
#define FW_ENABLE_URL false

#if FW_ENABLE_OTA
#include "index.h"
#endif

AsyncWebServer server(80);

// Global runtime JSON; user can inject values from other modules.
DynamicJsonDocument gRuntimeJson(2048);

static unsigned long bootMillis = 0;
static uint32_t powerOnCounter = 1;

String jsonOrDash(const char* key) {
  if (!gRuntimeJson.containsKey(key) || gRuntimeJson[key].isNull()) return "-";
  return gRuntimeJson[key].as<String>();
}

void seedRuntimeJsonDefaults() {
  if (!gRuntimeJson.containsKey("firmwareVersion")) gRuntimeJson["firmwareVersion"] = "-";
  if (!gRuntimeJson.containsKey("frameworkVersion")) gRuntimeJson["frameworkVersion"] = "-";
  if (!gRuntimeJson.containsKey("hardwareVersion")) gRuntimeJson["hardwareVersion"] = "-";
  if (!gRuntimeJson.containsKey("deviceId")) gRuntimeJson["deviceId"] = String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!gRuntimeJson.containsKey("temperature")) gRuntimeJson["temperature"] = "-";
  if (!gRuntimeJson.containsKey("voltage")) gRuntimeJson["voltage"] = "-";
}

String buildStatusJson() {
  DynamicJsonDocument payload(2048);
  payload["firmwareVersion"] = jsonOrDash("firmwareVersion");
  payload["frameworkVersion"] = jsonOrDash("frameworkVersion");
  payload["hardwareVersion"] = jsonOrDash("hardwareVersion");
  payload["deviceId"] = jsonOrDash("deviceId");
  payload["runTimeMinutes"] = (millis() - bootMillis) / 60000;
  payload["powerOn"] = powerOnCounter;
  payload["temperature"] = jsonOrDash("temperature");
  payload["voltage"] = jsonOrDash("voltage");
  payload["heapUsed"] = ESP.getHeapSize() - ESP.getFreeHeap();
  payload["heapTotal"] = ESP.getHeapSize();
  payload["flashUsed"] = ESP.getSketchSize();
  payload["flashTotal"] = ESP.getFreeSketchSpace() + ESP.getSketchSize();

  String out;
  serializeJson(payload, out);
  return out;
}

#if FW_ENABLE_OTA
void setupOtaRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json", buildStatusJson());
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"ok\":true,\"message\":\"Rebooting\"}");
    delay(250);
    ESP.restart();
  });

  server.on("/api/rollback", HTTP_POST, [](AsyncWebServerRequest* request) {
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && defined(ESP_OK)
    esp_err_t rb = esp_ota_mark_app_invalid_rollback_and_reboot();
    bool ok = (rb == ESP_OK);
    request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true,\"message\":\"Rollback triggered\"}" : "{\"ok\":false,\"message\":\"Rollback failed\"}");
#else
    request->send(400, "application/json", "{\"ok\":false,\"message\":\"Rollback not supported by this core/bootloader\"}");
#endif
  });

  ElegantOTA.begin(&server);
}
#endif

void setup() {
  Serial.begin(115200);
  bootMillis = millis();
  seedRuntimeJsonDefaults();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-Framework", "12345678");

#if FW_ENABLE_OTA
  setupOtaRoutes();
#endif

  server.begin();
}

void loop() {
#if FW_ENABLE_OTA
  ElegantOTA.loop();
#endif
}
