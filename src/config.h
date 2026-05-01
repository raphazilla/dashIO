#pragma once

// ============================================================
// dashIO - Configuration
// M5Stack Cardputer ADV + Cap LoRa-1262 868MHz
// ============================================================

#define DASHIO_VERSION "0.3.0"

// --- WiFi AP ---
#define WIFI_AP_SSID     "dashIO"
#define WIFI_AP_PASSWORD "dashio123"
#define WIFI_AP_IP       "192.168.4.1"
#define WEBSERVER_PORT   80
#define WEBSOCKET_PATH   "/ws"

// --- SD Card (SPI) ---
#define SD_SCK_PIN   40
#define SD_MISO_PIN  39
#define SD_MOSI_PIN  14
#define SD_CS_PIN    12

// --- LoRa SX1262 (Cap LoRa-1262) ---
#define LORA_NSS_PIN   5
#define LORA_IRQ_PIN   4
#define LORA_RST_PIN   3
#define LORA_BUSY_PIN  6
#define LORA_FREQ_MHZ  868.0f
#define LORA_SF        7
#define LORA_BW_KHZ    125.0f
#define LORA_CR        5      // 4/5
#define LORA_POWER_DBM 20
#define LORA_PREAMBLE  8

// --- GPS (via Cap LoRa-1262 header) ---
#define GPS_RX_PIN  15
#define GPS_TX_PIN  13
#define GPS_BAUD    9600

// --- IR Emitter ---
#define IR_TX_PIN  44

// --- Tasks ---
#define SENSOR_TASK_CORE      1
#define SENSOR_TASK_STACK     8192
#define SENSOR_TASK_PRIORITY  2
#define WEBSERVER_TASK_CORE   0
#define WEBSERVER_TASK_STACK  8192
#define WEBSERVER_TASK_PRIORITY 2

// --- Timing ---
#define SENSOR_READ_MS    100   // Sensor polling interval
#define WS_BROADCAST_MS   200   // WebSocket broadcast interval
#define LORA_PACKET_LOG   50    // Max packets in memory log
