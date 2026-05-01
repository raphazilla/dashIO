#include "app_config.h"
#include <SD.h>
#include <ArduinoJson.h>

AppConfig g_config;

static const char* CONFIG_PATH = "/dashIO/config.json";

void config_load() {
    if (!SD.exists(CONFIG_PATH)) {
        Serial.println("[config] No config.json — using defaults");
        return;
    }

    File f = SD.open(CONFIG_PATH, FILE_READ);
    if (!f) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[config] Parse error: %s — using defaults\n", err.c_str());
        return;
    }

    g_config.ap_ssid      = doc["ap_ssid"]      | "dashIO";
    g_config.ap_password  = doc["ap_password"]  | "dashio123";
    g_config.ap_ip        = doc["ap_ip"]        | "192.168.4.1";
    g_config.brightness   = doc["brightness"]   | 200;
    g_config.sta_ssid     = doc["sta_ssid"]     | "";
    g_config.sta_password = doc["sta_password"] | "";
    g_config.wifi_mode    = doc["wifi_mode"]    | "ap";

    Serial.printf("[config] Loaded: SSID=%s mode=%s\n",
                  g_config.ap_ssid.c_str(), g_config.wifi_mode.c_str());
}

void config_save() {
    File f = SD.open(CONFIG_PATH, FILE_WRITE);
    if (!f) {
        Serial.println("[config] Cannot write config.json");
        return;
    }

    JsonDocument doc;
    doc["ap_ssid"]      = g_config.ap_ssid;
    doc["ap_password"]  = g_config.ap_password;
    doc["ap_ip"]        = g_config.ap_ip;
    doc["brightness"]   = g_config.brightness;
    doc["sta_ssid"]     = g_config.sta_ssid;
    doc["sta_password"] = g_config.sta_password;
    doc["wifi_mode"]    = g_config.wifi_mode;

    serializeJsonPretty(doc, f);
    f.close();
    Serial.println("[config] Saved config.json");
}
