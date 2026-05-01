#pragma once
#include <Arduino.h>

struct AppConfig {
    String ap_ssid     = "dashIO";
    String ap_password = "dashio123";
    String ap_ip       = "192.168.4.1";
    int    brightness  = 200;
    String sta_ssid    = "";
    String sta_password = "";
    String wifi_mode   = "ap";  // "ap" or "dual"
};

extern AppConfig g_config;

void config_load();   // load from /dashIO/config.json (or defaults if absent)
void config_save();   // write to /dashIO/config.json
