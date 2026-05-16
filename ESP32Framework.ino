#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>

#define FW_ENABLE_OTA true
#define FW_ENABLE_WEB_TERMINAL true
#define FW_ENABLE_CREDENTIAL false
#define FW_ENABLE_MESH false
#define FW_ENABLE_ESPNOW false
#define FW_ENABLE_URL false

#if FW_ENABLE_OTA
#include "index.h"
#endif

AsyncWebServer server(80);
DynamicJsonDocument gRuntimeJson(2048);

static unsigned long bootMillis = 0;
static uint32_t powerOnCounter = 1;

#if FW_ENABLE_WEB_TERMINAL
static constexpr size_t WEB_TERM_BUFFER_SIZE = 4092;
static char gWebTermTxBuffer[WEB_TERM_BUFFER_SIZE + 1] = {0};
static char gWebTermRxBuffer[WEB_TERM_BUFFER_SIZE + 1] = {0};
static size_t gWebTermTxLen = 0;
static size_t gWebTermRxLen = 0;
#endif

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

#if FW_ENABLE_WEB_TERMINAL
size_t appendToTxBuffer(const uint8_t* data, size_t len) {
  if (!data || len == 0) return 0;
  size_t room = WEB_TERM_BUFFER_SIZE - gWebTermTxLen;
  size_t wlen = (len > room) ? room : len;
  memcpy(gWebTermTxBuffer + gWebTermTxLen, data, wlen);
  gWebTermTxLen += wlen;
  gWebTermTxBuffer[gWebTermTxLen] = '\0';
  return wlen;
}

size_t WebPrint(const String& text) {
  return appendToTxBuffer(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
}

size_t WebPrintln(const String& text) {
  size_t written = WebPrint(text);
  if (gWebTermTxLen < WEB_TERM_BUFFER_SIZE) {
    gWebTermTxBuffer[gWebTermTxLen++] = '\n';
    gWebTermTxBuffer[gWebTermTxLen] = '\0';
    written += 1;
  }
  return written;
}

String WebRead() {
  String out(gWebTermRxBuffer);
  gWebTermRxLen = 0;
  gWebTermRxBuffer[0] = '\0';
  return out;
}

void setupWebTerminalRoutes() {
  server.on("/api/webterm/poll", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument payload(4300);
    payload["ok"] = true;
    payload["data"] = String(gWebTermTxBuffer);
    payload["len"] = gWebTermTxLen;

    String out;
    serializeJson(payload, out);

    gWebTermTxLen = 0;
    gWebTermTxBuffer[0] = '\0';

    request->send(200, "application/json", out);
  });

  server.on("/api/webterm/write", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
            [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
              if (index == 0) {
                gWebTermRxLen = 0;
                gWebTermRxBuffer[0] = '\0';
              }

              if (len > 0) {
                size_t room = WEB_TERM_BUFFER_SIZE - gWebTermRxLen;
                size_t wlen = (len > room) ? room : len;
                memcpy(gWebTermRxBuffer + gWebTermRxLen, data, wlen);
                gWebTermRxLen += wlen;
                gWebTermRxBuffer[gWebTermRxLen] = '\0';
              }

              if ((index + len) >= total) {
                request->send(200, "application/json", "{\"ok\":true}");
              }
            });
}
#endif

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

  server.on(
      "/api/update", HTTP_POST,
      [](AsyncWebServerRequest* request) {
        bool ok = !Update.hasError();
        request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true,\"message\":\"Firmware updated, rebooting\"}" : "{\"ok\":false,\"message\":\"Update failed\"}");
        if (ok) {
          delay(250);
          ESP.restart();
        }
      },
      [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        (void)request;
        if (index == 0) {
          if (!filename.endsWith(".bin")) {
            Update.abort();
            return;
          }
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
          }
        }

        if (len > 0) {
          if (Update.write(data, len) != len) {
            Update.printError(Serial);
          }
        }

        if (final) {
          if (!Update.end(true)) {
            Update.printError(Serial);
          }
        }
      });
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
#if FW_ENABLE_WEB_TERMINAL
  setupWebTerminalRoutes();
  WebPrintln("[WebTerm] ready");
#endif

  server.begin();
}

void loop() {
#if FW_ENABLE_WEB_TERMINAL
  String cmd = WebRead();
  if (cmd.length() > 0) {
    WebPrint("echo> ");
    WebPrintln(cmd);
  }
#endif
}
