#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>
#include "config.h"

// LoRa packet entry for packet log
struct LoRaPacket {
    uint32_t timestamp;  // millis()
    float    rssi;
    float    snr;
    String   payload;    // hex string
    String   text;       // decoded as ASCII where printable
};

// Shared sensor data — written by sensor task, read by WebSocket task
struct SensorData {
    // IMU (BMI270)
    float imu_ax = 0.0f;
    float imu_ay = 0.0f;
    float imu_az = 0.0f;
    float imu_gx = 0.0f;
    float imu_gy = 0.0f;
    float imu_gz = 0.0f;
    bool  imu_ok = false;

    // Battery / Power (AXP2101)
    float    bat_voltage  = 0.0f;
    int      bat_percent  = 0;
    bool     bat_charging = false;
    bool     bat_ok       = false;

    // Audio (Mic)
    int  audio_level = 0;   // RMS 0-32767
    int  audio_peak  = 0;
    bool audio_ok    = false;

    // LoRa (SX1262)
    bool   lora_ok     = false;
    String lora_status = "init";  // "idle", "rx", "tx", "error"
    float  lora_freq   = LORA_FREQ_MHZ;
    int    lora_sf     = LORA_SF;
    float  lora_bw     = LORA_BW_KHZ;
    int    lora_power  = LORA_POWER_DBM;
    float  lora_rssi   = 0.0f;
    float  lora_snr    = 0.0f;
    std::vector<LoRaPacket> lora_log;

    // GPS
    bool   gps_fix       = false;
    double gps_lat       = 0.0;
    double gps_lon       = 0.0;
    int    gps_satellites = 0;
    float  gps_hdop      = 99.9f;

    // System
    uint32_t sys_uptime   = 0;  // seconds
    uint32_t sys_heap     = 0;  // bytes free
    int      sys_clients  = 0;  // WiFi AP clients
    String   sys_sta_ip   = ""; // STA mode IP (empty if not connected)

    // SD card sector count (cached at boot for USB MSC)
    uint32_t sd_sector_count = 0;

    // Pending LoRa TX from web command
    bool   lora_tx_pending  = false;
    String lora_tx_payload  = "";

    // Pending LoRa config change from web command
    bool  lora_cfg_pending = false;
    int   lora_cfg_sf      = LORA_SF;
    float lora_cfg_bw      = LORA_BW_KHZ;
    int   lora_cfg_power   = LORA_POWER_DBM;

    // Display brightness command
    bool lora_brightness_pending = false;
    int  display_brightness      = 200;

    // USB MSC mode
    bool usb_msc_requested = false;
    bool usb_msc_active    = false;

    // Charging mode
    bool charging_mode_requested = false;
    bool charging_mode_value     = false;
    bool charging_mode           = false;
};

// Global shared state + mutex
extern SensorData g_sensors;
extern SemaphoreHandle_t g_sensors_mutex;

// RAII guard for the mutex
struct SensorLock {
    SensorLock()  { xSemaphoreTake(g_sensors_mutex, portMAX_DELAY); }
    ~SensorLock() { xSemaphoreGive(g_sensors_mutex); }
};
