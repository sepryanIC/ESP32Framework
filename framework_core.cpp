#include "framework_core.h"

#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "config.h"
#include "index.h"
#include "web_terminal.h"

AsyncWebServer server(80);
DynamicJsonDocument gRuntimeJson(2048);

static unsigned long bootMillis = 0;
static uint32_t powerOnCounter = 1;

#if FW_ENABLE_WEB_TERMINAL
static char gWebTermTxBuffer[WEB_TERM_BUFFER_SIZE + 1] = {0};
static char gWebTermRxBuffer[WEB_TERM_BUFFER_SIZE + 1] = {0};
static size_t gWebTermTxLen = 0;
static size_t gWebTermRxLen = 0;
#endif

#if FW_ENABLE_CREDENTIAL
static Preferences gPrefs;
static const char* PREF_NS = "fwcfg";
#endif

static String jsonOrDash(const char* key) {
  if (!gRuntimeJson.containsKey(key) || gRuntimeJson[key].isNull()) return "-";
  return gRuntimeJson[key].as<String>();
}

static void seedRuntimeJsonDefaults() {
  if (!gRuntimeJson.containsKey("firmwareVersion")) gRuntimeJson["firmwareVersion"] = "-";
  if (!gRuntimeJson.containsKey("frameworkVersion")) gRuntimeJson["frameworkVersion"] = "-";
  if (!gRuntimeJson.containsKey("hardwareVersion")) gRuntimeJson["hardwareVersion"] = "-";
  if (!gRuntimeJson.containsKey("deviceId")) gRuntimeJson["deviceId"] = String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!gRuntimeJson.containsKey("temperature")) gRuntimeJson["temperature"] = "-";
  if (!gRuntimeJson.containsKey("voltage")) gRuntimeJson["voltage"] = "-";
}

static String buildStatusJson() {
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

#if FW_ENABLE_CREDENTIAL
static void seedCredentialDefaults() {
  if (!gRuntimeJson.containsKey("credential")) {
    JsonObject c = gRuntimeJson.createNestedObject("credential");
    c["staEnabled"] = false;
    c["apEnabled"] = true;
    c["staSsid"] = "";
    c["staPassword"] = "";
    c["apSsid"] = "ESP32-Framework";
    c["apPassword"] = "12345678";
    c["apChannel"] = 1;
    c["apBroadcast"] = true;
  }
}

static void loadCredentialFromPrefs() {
  seedCredentialDefaults();
  String raw = gPrefs.getString("credential", "");
  if (raw.isEmpty()) return;
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, raw) == DeserializationError::Ok) gRuntimeJson["credential"] = doc.as<JsonVariant>();
}

static void saveCredentialToPrefs() {
  String raw;
  serializeJson(gRuntimeJson["credential"], raw);
  gPrefs.putString("credential", raw);
}

static void applyCredentialMode() {
  Serial.println("[CRED] applyCredentialMode()");
  JsonObject c = gRuntimeJson["credential"].as<JsonObject>();
  bool sta = c["staEnabled"] | false;
  bool ap = c["apEnabled"] | false;

  Serial.printf("[CRED] staEnabled=%d apEnabled=%d\n", sta, ap);
  if (!sta && !ap) {
    Serial.println("[CRED] Mode -> WIFI_OFF");
    WiFi.mode(WIFI_OFF);
    return;
  }
  wifi_mode_t targetMode = sta && ap ? WIFI_AP_STA : (sta ? WIFI_STA : WIFI_AP);
  Serial.printf("[CRED] Mode -> %d\n", (int)targetMode);
  WiFi.mode(targetMode);

  if (sta) {
    const char* ssid = c["staSsid"] | "";
    const char* pass = c["staPassword"] | "";
    Serial.printf("[CRED] STA begin SSID=%s\n", ssid);
    if (strlen(ssid) > 0) WiFi.begin(ssid, pass);
  }
  if (ap) {
    int channel = c["apChannel"] | 1;
    if (sta && WiFi.channel() > 0) channel = WiFi.channel();
    const char* ssid = c["apSsid"] | "ESP32-Framework";
    const char* pass = c["apPassword"] | "12345678";
    bool hidden = !(c["apBroadcast"] | true);
    Serial.printf("[CRED] AP start SSID=%s CH=%d hidden=%d\n", ssid, channel, hidden);
    bool apOk = WiFi.softAP(ssid, pass, channel, hidden);
    Serial.printf("[CRED] AP start result=%d\n", apOk);
  } else {
    WiFi.softAPdisconnect(true);
  }
}

static void setupCredentialRoutes() {
  server.on("/api/credential", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument out(1400);
    out["ok"] = true;
    out["credential"] = gRuntimeJson["credential"];
    String body;
    serializeJson(out, body);
    request->send(200, "application/json", body);
  });

  server.on("/api/credential", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
            [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
              static String body;
              if (index == 0) body = "";
              body.concat(reinterpret_cast<const char*>(data), len);
              if ((index + len) < total) return;

              DynamicJsonDocument doc(1400);
              auto err = deserializeJson(doc, body);
              if (err || !doc.containsKey("credential")) {
                request->send(400, "application/json", "{\"ok\":false,\"message\":\"invalid json\"}");
                return;
              }

              gRuntimeJson["credential"] = doc["credential"];
              saveCredentialToPrefs();
              applyCredentialMode();
              request->send(200, "application/json", "{\"ok\":true,\"message\":\"Credential saved\"}");
            });

  server.on("/api/credential/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
    Serial.println("[CRED][SCAN] request received");
    wifi_mode_t prevMode = WiFi.getMode();
    Serial.printf("[CRED][SCAN] prevMode=%d\n", (int)prevMode);

    // Force STA radio active during scan for better compatibility.
    WiFi.mode(WIFI_STA);
    Serial.println("[CRED][SCAN] forced WIFI_STA");
    WiFi.disconnect(false, true);
    delay(150);
    WiFi.scanDelete();

    int n = WiFi.scanNetworks(false, true);
    Serial.printf("[CRED][SCAN] scan result n=%d\n", n);
    DynamicJsonDocument doc(4096);
    doc["ok"] = (n >= 0);
    doc["count"] = (n > 0) ? n : 0;
    doc["scanStatus"] = n;

    JsonArray arr = doc.createNestedArray("networks");
    if (n > 0) {
      for (int i = 0; i < n && i < 25; i++) {
        Serial.printf("[CRED][SCAN] #%d SSID=%s BSSID=%s RSSI=%d\n", i, WiFi.SSID(i).c_str(), WiFi.BSSIDstr(i).c_str(), WiFi.RSSI(i));
        JsonObject o = arr.createNestedObject();
        o["ssid"] = WiFi.SSID(i);
        o["bssid"] = WiFi.BSSIDstr(i);
        o["rssi"] = WiFi.RSSI(i);
      }
    }

    String out;
    serializeJson(doc, out);
    WiFi.scanDelete();

    // Restore configured credential mode after scan.
    applyCredentialMode();
    Serial.println("[CRED][SCAN] mode restored via applyCredentialMode");
    if (prevMode == WIFI_MODE_NULL) {
      WiFi.mode(WIFI_OFF);
      Serial.println("[CRED][SCAN] restored WIFI_OFF");
    }

    Serial.printf("[CRED][SCAN] response bytes=%u\n", (unsigned)out.length());
    request->send(200, "application/json", out);
  });
}
#endif

#if FW_ENABLE_WEB_TERMINAL
static size_t appendToTxBuffer(const uint8_t* data, size_t len) {
  if (!data || len == 0) return 0;
  size_t room = WEB_TERM_BUFFER_SIZE - gWebTermTxLen;
  size_t wlen = (len > room) ? room : len;
  memcpy(gWebTermTxBuffer + gWebTermTxLen, data, wlen);
  gWebTermTxLen += wlen;
  gWebTermTxBuffer[gWebTermTxLen] = '\0';
  return wlen;
}
size_t WebPrint(const String& text) { return appendToTxBuffer(reinterpret_cast<const uint8_t*>(text.c_str()), text.length()); }
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
static void setupWebTerminalRoutes() {
  server.on("/api/webterm/poll", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument payload(4300);
    payload["ok"] = true;
    payload["data"] = String(gWebTermTxBuffer);
    payload["len"] = gWebTermTxLen;
    String out; serializeJson(payload, out);
    gWebTermTxLen = 0; gWebTermTxBuffer[0] = '\0';
    request->send(200, "application/json", out);
  });
  server.on("/api/webterm/write", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
            [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
              if (index == 0) { gWebTermRxLen = 0; gWebTermRxBuffer[0] = '\0'; }
              if (len > 0) {
                size_t room = WEB_TERM_BUFFER_SIZE - gWebTermRxLen;
                size_t wlen = (len > room) ? room : len;
                memcpy(gWebTermRxBuffer + gWebTermRxLen, data, wlen);
                gWebTermRxLen += wlen; gWebTermRxBuffer[gWebTermRxLen] = '\0';
              }
              if ((index + len) >= total) request->send(200, "application/json", "{\"ok\":true}");
            });
}
#endif

#if FW_ENABLE_OTA
static void setupOtaRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) { request->send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) { request->send(200, "application/json", buildStatusJson()); });
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* request) { request->send(200, "application/json", "{\"ok\":true,\"message\":\"Rebooting\"}"); delay(250); ESP.restart(); });
  server.on("/api/rollback", HTTP_POST, [](AsyncWebServerRequest* request) {
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && defined(ESP_OK)
    esp_err_t rb = esp_ota_mark_app_invalid_rollback_and_reboot();
    bool ok = (rb == ESP_OK);
    request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true,\"message\":\"Rollback triggered\"}" : "{\"ok\":false,\"message\":\"Rollback failed\"}");
#else
    request->send(400, "application/json", "{\"ok\":false,\"message\":\"Rollback not supported by this core/bootloader\"}");
#endif
  });
  server.on("/api/update", HTTP_POST,
            [](AsyncWebServerRequest* request) {
              bool ok = !Update.hasError();
              request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true,\"message\":\"Firmware updated, rebooting\"}" : "{\"ok\":false,\"message\":\"Update failed\"}");
              if (ok) { delay(250); ESP.restart(); }
            },
            [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
              (void)request;
              if (index == 0) { if (!filename.endsWith(".bin")) { Update.abort(); return; } if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial); }
              if (len > 0 && Update.write(data, len) != len) Update.printError(Serial);
              if (final && !Update.end(true)) Update.printError(Serial);
            });
}
#endif

void frameworkSetup() {
  Serial.begin(115200);
  bootMillis = millis();
  seedRuntimeJsonDefaults();

#if FW_ENABLE_CREDENTIAL
  gPrefs.begin(PREF_NS, false);
  seedCredentialDefaults();
  loadCredentialFromPrefs();
  applyCredentialMode();
#else
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-Framework", "12345678");
#endif

#if FW_ENABLE_OTA
  setupOtaRoutes();
#endif
#if FW_ENABLE_WEB_TERMINAL
  setupWebTerminalRoutes();
  WebPrintln("[WebTerm] ready");
#endif
#if FW_ENABLE_CREDENTIAL
  setupCredentialRoutes();
#endif

  server.begin();
}

void frameworkLoop() {
#if FW_ENABLE_WEB_TERMINAL
  String cmd = WebRead();
  if (cmd.length() > 0) {
    WebPrint("echo> ");
    WebPrintln(cmd);
  }
#endif
}
