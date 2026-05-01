#include "wifi_ap.h"
#include "sensor_data.h"
#include "dashboard.h"
#include "app_config.h"
#include "terminal.h"
#include "../config.h"

#include <WiFi.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

static AsyncWebServer server(WEBSERVER_PORT);
static AsyncWebSocket ws(WEBSOCKET_PATH);

static volatile bool s_lora_rx_event = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Return 503 if USB MSC is active (SD not accessible via firmware)
static bool sd_available(AsyncWebServerRequest* req) {
    bool active;
    { SensorLock lock; active = g_sensors.usb_msc_active; }
    if (active) {
        req->send(503, "application/json", "{\"error\":\"SD in USB MSC mode\"}");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// WebSocket event handler
// ---------------------------------------------------------------------------
static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WS] Client #%u connected\n", client->id());
        SensorLock lock;
        g_sensors.sys_clients = server->count();
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WS] Client #%u disconnected\n", client->id());
        terminal_client_disconnected(client->id());
        SensorLock lock;
        g_sensors.sys_clients = server->count();
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len &&
            info->opcode == WS_TEXT) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, (char*)data, len);
            if (err) return;

            const char* type_str = doc["type"] | "";

            if (strcmp(type_str, "terminal") == 0) {
                // Terminal commands are NOT mutex-protected (SD access is separate)
                String cmd = doc["cmd"] | "";
                int    seq = doc["seq"] | 0;
                String resp = terminal_exec(client->id(), cmd, seq);
                client->text(resp);
                return;
            }

            SensorLock lock;

            if (strcmp(type_str, "lora_tx") == 0) {
                g_sensors.lora_tx_pending = true;
                g_sensors.lora_tx_payload = doc["payload"] | "";

            } else if (strcmp(type_str, "lora_config") == 0) {
                g_sensors.lora_cfg_pending = true;
                g_sensors.lora_cfg_sf    = doc["sf"]    | LORA_SF;
                g_sensors.lora_cfg_bw    = doc["bw"]    | LORA_BW_KHZ;
                g_sensors.lora_cfg_power = doc["power"] | LORA_POWER_DBM;

            } else if (strcmp(type_str, "display_brightness") == 0) {
                g_sensors.lora_brightness_pending = true;
                g_sensors.display_brightness = doc["value"] | 200;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Static file serving from SD
// ---------------------------------------------------------------------------
static void serveSDFile(AsyncWebServerRequest* req, const char* path,
                        const char* contentType) {
    if (!SD.exists(path)) {
        req->send(404, "text/plain", "Not found");
        return;
    }
    req->send(SD, path, contentType);
}

// ---------------------------------------------------------------------------
// File manager helpers
// ---------------------------------------------------------------------------
static String fileSizeStr(size_t bytes) {
    if (bytes < 1024)       return String(bytes) + " B";
    if (bytes < 1024*1024)  return String(bytes/1024) + " KB";
    return String(bytes/(1024*1024)) + " MB";
}

static void handleFilesList(AsyncWebServerRequest* req) {
    if (!sd_available(req)) return;
    String path = req->hasParam("path") ? req->getParam("path")->value() : "/";
    if (!path.startsWith("/")) path = "/" + path;

    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        req->send(404, "application/json", "{\"error\":\"not a directory\"}");
        return;
    }

    JsonDocument doc;
    JsonArray arr = doc["files"].to<JsonArray>();

    File entry = dir.openNextFile();
    while (entry) {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"] = String(entry.name());
        obj["dir"]  = entry.isDirectory();
        obj["size"] = entry.isDirectory() ? 0 : (int)entry.size();
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    doc["path"] = path;

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

static void handleFileDelete(AsyncWebServerRequest* req) {
    if (!sd_available(req)) return;
    if (!req->hasParam("path")) {
        req->send(400, "application/json", "{\"error\":\"missing path\"}");
        return;
    }
    String path = req->getParam("path")->value();
    if (SD.remove(path)) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"delete failed\"}");
    }
}

static void handleMkdir(AsyncWebServerRequest* req) {
    if (!sd_available(req)) return;
    if (!req->hasParam("path", true)) {
        req->send(400, "application/json", "{\"error\":\"missing path\"}");
        return;
    }
    String path = req->getParam("path", true)->value();
    if (SD.mkdir(path)) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"mkdir failed\"}");
    }
}

static void handleRename(AsyncWebServerRequest* req) {
    if (!sd_available(req)) return;
    if (!req->hasParam("from", true) || !req->hasParam("to", true)) {
        req->send(400, "application/json", "{\"error\":\"missing from/to\"}");
        return;
    }
    String from = req->getParam("from", true)->value();
    String to   = req->getParam("to",   true)->value();
    if (SD.rename(from, to)) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"rename failed\"}");
    }
}

static void handleUpload(AsyncWebServerRequest* req, const String& filename,
                         size_t index, uint8_t* data, size_t len, bool final) {
    // Check MSC mode at start of upload
    if (index == 0) {
        bool active; { SensorLock lock; active = g_sensors.usb_msc_active; }
        if (active) { req->send(503, "application/json", "{\"error\":\"SD in USB MSC mode\"}"); return; }
    }
    static File uploadFile;
    if (index == 0) {
        String path = req->hasParam("path") ? req->getParam("path")->value() : "/";
        if (!path.endsWith("/")) path += "/";
        path += filename;
        Serial.printf("[upload] %s\n", path.c_str());
        uploadFile = SD.open(path, FILE_WRITE);
    }
    if (uploadFile) uploadFile.write(data, len);
    if (final && uploadFile) {
        uploadFile.close();
        req->send(200, "application/json", "{\"ok\":true}");
    }
}

// ---------------------------------------------------------------------------
// Config & Admin endpoints
// ---------------------------------------------------------------------------
static void handleGetConfig(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["ap_ssid"]     = g_config.ap_ssid;
    doc["ap_password"] = g_config.ap_password;
    doc["ap_ip"]       = g_config.ap_ip;
    doc["brightness"]  = g_config.brightness;
    doc["sta_ssid"]    = g_config.sta_ssid;
    doc["wifi_mode"]   = g_config.wifi_mode;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

static void handleSetConfig(AsyncWebServerRequest* req, uint8_t* data,
                            size_t len, size_t, size_t) {
    JsonDocument doc;
    if (deserializeJson(doc, (char*)data, len)) {
        req->send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }
    if (doc["ap_ssid"].is<const char*>())     g_config.ap_ssid     = doc["ap_ssid"].as<String>();
    if (doc["ap_password"].is<const char*>()) g_config.ap_password = doc["ap_password"].as<String>();
    if (doc["brightness"].is<int>())          g_config.brightness  = doc["brightness"];
    config_save();
    req->send(200, "application/json", "{\"ok\":true,\"restart\":true}");
    delay(500);
    WiFi.softAPdisconnect(true);
    WiFi.softAP(g_config.ap_ssid.c_str(), g_config.ap_password.c_str());
    Serial.printf("[WiFi] AP restarted: %s\n", g_config.ap_ssid.c_str());
}

// ---------------------------------------------------------------------------
// WiFi STA endpoints
// ---------------------------------------------------------------------------
static void handleWifiScan(AsyncWebServerRequest* req) {
    int n = WiFi.scanNetworks(false, true);  // sync scan, show hidden=true
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n && i < 20; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["ssid"]   = WiFi.SSID(i);
        obj["rssi"]   = WiFi.RSSI(i);
        obj["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

static void handleWifiConnect(AsyncWebServerRequest* req, uint8_t* data,
                              size_t len, size_t, size_t) {
    JsonDocument doc;
    if (deserializeJson(doc, (char*)data, len)) {
        req->send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }
    String ssid = doc["ssid"] | "";
    String pass = doc["password"] | "";
    if (ssid.isEmpty()) {
        req->send(400, "application/json", "{\"error\":\"missing ssid\"}");
        return;
    }

    // Switch to AP+STA mode and connect
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(g_config.ap_ssid.c_str(), g_config.ap_password.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());

    // Wait up to 10 seconds
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        String ip = WiFi.localIP().toString();
        g_config.sta_ssid     = ssid;
        g_config.sta_password = pass;
        g_config.wifi_mode    = "dual";
        config_save();
        { SensorLock lock; g_sensors.sys_sta_ip = ip; }
        Serial.printf("[WiFi] STA connected: %s IP: %s\n", ssid.c_str(), ip.c_str());
        String out = "{\"ok\":true,\"ip\":\"" + ip + "\"}";
        req->send(200, "application/json", out);
    } else {
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        WiFi.softAP(g_config.ap_ssid.c_str(), g_config.ap_password.c_str());
        { SensorLock lock; g_sensors.sys_sta_ip = ""; }
        req->send(200, "application/json", "{\"ok\":false,\"error\":\"Connection failed\"}");
    }
}

static void handleWifiDisconnect(AsyncWebServerRequest* req) {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(g_config.ap_ssid.c_str(), g_config.ap_password.c_str());
    g_config.wifi_mode = "ap";
    g_config.sta_ssid  = "";
    config_save();
    { SensorLock lock; g_sensors.sys_sta_ip = ""; }
    req->send(200, "application/json", "{\"ok\":true}");
}

static void handleWifiStatus(AsyncWebServerRequest* req) {
    bool connected = (WiFi.status() == WL_CONNECTED);
    String ip  = connected ? WiFi.localIP().toString() : "";
    String sta_ssid = connected ? WiFi.SSID() : "";
    { SensorLock lock; g_sensors.sys_sta_ip = ip; }
    JsonDocument doc;
    doc["mode"]          = g_config.wifi_mode;
    doc["sta_connected"] = connected;
    doc["sta_ip"]        = ip;
    doc["sta_ssid"]      = sta_ssid;
    doc["ap_ip"]         = WiFi.softAPIP().toString();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// USB MSC endpoint
// ---------------------------------------------------------------------------
static void handleUsbMsc(AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"ok\":true}");
    delay(200);
    SensorLock lock;
    g_sensors.usb_msc_requested = true;
}

// ---------------------------------------------------------------------------
// Charging mode endpoint
// ---------------------------------------------------------------------------
static void handleCharging(AsyncWebServerRequest* req, uint8_t* data,
                           size_t len, size_t, size_t) {
    JsonDocument doc;
    if (deserializeJson(doc, (char*)data, len)) {
        req->send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }
    bool enabled = doc["enabled"] | false;
    {
        SensorLock lock;
        g_sensors.charging_mode_requested = true;
        g_sensors.charging_mode_value     = enabled;
    }
    req->send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void wifiAP_init() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(g_config.ap_ssid.c_str(), g_config.ap_password.c_str());
    Serial.printf("[WiFi] AP started — SSID: %s  IP: %s\n",
                  g_config.ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());

    // Reconnect STA if configured
    if (g_config.wifi_mode == "dual" && g_config.sta_ssid.length() > 0) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(g_config.ap_ssid.c_str(), g_config.ap_password.c_str());
        WiFi.begin(g_config.sta_ssid.c_str(), g_config.sta_password.c_str());
        Serial.printf("[WiFi] Reconnecting STA: %s\n", g_config.sta_ssid.c_str());
    }

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // Static assets from SD card
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        serveSDFile(req, "/dashIO/index.html", "text/html");
    });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* req) {
        serveSDFile(req, "/dashIO/style.css", "text/css");
    });
    server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* req) {
        serveSDFile(req, "/dashIO/app.js", "application/javascript");
    });
    server.on("/chart.min.js", HTTP_GET, [](AsyncWebServerRequest* req) {
        serveSDFile(req, "/dashIO/chart.min.js", "application/javascript");
    });
    server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest* req) {
        serveSDFile(req, "/dashIO/favicon.svg", "image/svg+xml");
    });

    // --- File Manager API ---
    server.on("/api/files",  HTTP_GET,    handleFilesList);
    server.on("/api/file",   HTTP_GET,    [](AsyncWebServerRequest* req) {
        if (!sd_available(req)) return;
        if (!req->hasParam("path")) { req->send(400); return; }
        String path = req->getParam("path")->value();
        if (!SD.exists(path)) { req->send(404); return; }
        req->send(SD, path, "application/octet-stream");
    });
    server.on("/api/file",   HTTP_DELETE, handleFileDelete);
    server.on("/api/mkdir",  HTTP_POST,   handleMkdir);
    server.on("/api/rename", HTTP_POST,   handleRename);
    server.on("/api/upload", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        handleUpload);

    // --- Config & Admin API ---
    server.on("/api/config", HTTP_GET,  handleGetConfig);
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        handleSetConfig);
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", "{\"ok\":true}");
        delay(500);
        ESP.restart();
    });

    // --- WiFi STA API ---
    server.on("/api/wifi/scan",   HTTP_GET,  handleWifiScan);
    server.on("/api/wifi/status", HTTP_GET,  handleWifiStatus);
    server.on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        handleWifiConnect);
    server.on("/api/wifi/disconnect", HTTP_POST, handleWifiDisconnect);

    // --- USB MSC ---
    server.on("/api/usb/msc", HTTP_POST, handleUsbMsc);

    // --- Charging mode ---
    server.on("/api/charging", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        handleCharging);

    // 404 fallback
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.println("[WiFi] Web server started");
}

void wifiAP_notifyLoRaRx() {
    s_lora_rx_event = true;
}

int wifiAP_clientCount() {
    return ws.count();
}

// ---------------------------------------------------------------------------
// WebSocket broadcast task (Core 0)
// ---------------------------------------------------------------------------
void wifiAP_task(void* pvParameters) {
    TickType_t lastBroadcast = xTaskGetTickCount();

    for (;;) {
        // Periodic sensor broadcast
        if (xTaskGetTickCount() - lastBroadcast >= pdMS_TO_TICKS(WS_BROADCAST_MS)) {
            lastBroadcast = xTaskGetTickCount();

            if (ws.count() > 0) {
                String json = dashboard_buildSensorJson();
                ws.textAll(json);
            }
            ws.cleanupClients();
        }

        // Immediate LoRa RX event push
        if (s_lora_rx_event && ws.count() > 0) {
            s_lora_rx_event = false;
            String json = dashboard_buildLoRaRxJson();
            if (json.length() > 0) {
                ws.textAll(json);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
