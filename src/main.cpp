#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <USB.h>
#include <USBMSC.h>

#include "config.h"
#include "core/sensor_data.h"
#include "core/wifi_ap.h"
#include "core/dashboard.h"
#include "core/assets_extractor.h"
#include "core/app_config.h"
#include "modules/power_module.h"
#include "modules/imu_module.h"
#include "modules/audio_module.h"
#include "modules/lora_module.h"
#include "modules/gps_module.h"

// Global shared state (declared extern in sensor_data.h)
SensorData g_sensors;
SemaphoreHandle_t g_sensors_mutex = nullptr;

// ---------------------------------------------------------------------------
// USB Mass Storage
// ---------------------------------------------------------------------------
static USBMSC s_msc;

// NOTE: 'msc_read_cb' and 'msc_write_cb' are typedef'd in USBMSC.h — use different names
// SD.readRAW / SD.writeRAW operate one sector (512 bytes) at a time
static int32_t sd_read_sectors(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize) {
    uint32_t count = bufsize / 512;
    uint8_t* ptr = (uint8_t*)buf;
    for (uint32_t i = 0; i < count; i++) {
        if (!SD.readRAW(ptr + i * 512, lba + i)) return -1;
    }
    return bufsize;
}

static int32_t sd_write_sectors(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t bufsize) {
    uint32_t count = bufsize / 512;
    for (uint32_t i = 0; i < count; i++) {
        if (!SD.writeRAW(buf + i * 512, lba + i)) return -1;
    }
    return bufsize;
}

static void usbmsc_start() {
    uint32_t sectors;
    { SensorLock lock; sectors = g_sensors.sd_sector_count; }
    if (sectors == 0) {
        Serial.println("[USB MSC] Unknown sector count, aborting");
        return;
    }

    Serial.printf("[USB MSC] Starting with %lu sectors\n", sectors);

    // Do NOT call SD.end() — FATFS diskio driver remains registered.
    // All firmware SD access is blocked via usb_msc_active flag.

    s_msc.vendorID("dashIO");
    s_msc.productID("SD Card");
    s_msc.productRevision("0.3");
    s_msc.onRead(sd_read_sectors);
    s_msc.onWrite(sd_write_sectors);
    s_msc.mediaPresent(true);
    s_msc.begin(sectors, 512);

    USB.onEvent([](void*, esp_event_base_t, int32_t event_id, void*) {
        if (event_id == ARDUINO_USB_SUSPEND_EVENT) {
            Serial.println("[USB MSC] Ejected — restarting");
            delay(500);
            ESP.restart();
        }
    });

    USB.begin();
    { SensorLock lock; g_sensors.usb_msc_active = true; }
}

// ---------------------------------------------------------------------------
// Display — double-buffered with M5Canvas (no flicker)
// ---------------------------------------------------------------------------
static M5Canvas s_canvas(&M5Cardputer.Display);
static bool     s_canvas_ready = false;

// Current page: 0=Network, 1=LoRa, 2=System
static uint8_t  s_display_page    = 0;
static uint32_t s_page_changed_ms = 0;

static const uint32_t PAGE_ROTATE_MS = 4000;
static const uint32_t DISPLAY_FG     = 0x07FF;  // Cyan
static const uint32_t DISPLAY_BG     = TFT_BLACK;

static void canvas_init() {
    s_canvas.createSprite(240, 135);
    s_canvas.setTextWrap(false);
    s_canvas_ready = true;
}

static void canvas_divider(int y) {
    s_canvas.drawFastHLine(0, y, 240, 0x2945);
}

static void canvas_header(const char* page_name) {
    s_canvas.fillSprite(DISPLAY_BG);

    s_canvas.setTextColor(DISPLAY_FG);
    s_canvas.setTextSize(2);
    s_canvas.setCursor(4, 4);
    s_canvas.print("dashIO");

    s_canvas.setTextSize(1);
    s_canvas.setTextColor(0x8410);
    s_canvas.setCursor(240 - (strlen(page_name) * 6) - 4, 8);
    s_canvas.print(page_name);

    for (int i = 0; i < 3; i++) {
        uint16_t col = (i == s_display_page) ? DISPLAY_FG : 0x2945;
        s_canvas.fillCircle(116 + i * 8, 10, 2, col);
    }

    canvas_divider(22);
}

// --- Page 0: Network ---
static void display_page_network(const SensorData& s) {
    canvas_header("Network");

    s_canvas.setTextColor(TFT_WHITE);
    s_canvas.setTextSize(1);
    s_canvas.setCursor(4, 30);
    s_canvas.print("SSID");
    s_canvas.setTextColor(DISPLAY_FG);
    s_canvas.setTextSize(2);
    s_canvas.setCursor(4, 42);
    s_canvas.print(WIFI_AP_SSID);

    s_canvas.setTextColor(TFT_WHITE);
    s_canvas.setTextSize(1);
    s_canvas.setCursor(4, 66);
    s_canvas.print("AP: http://");
    s_canvas.setTextColor(DISPLAY_FG);
    s_canvas.setCursor(4 + 11*6, 66);
    s_canvas.print(WIFI_AP_IP);

    // Show STA IP if connected
    if (s.sys_sta_ip.length() > 0) {
        s_canvas.setTextColor(TFT_WHITE);
        s_canvas.setCursor(4, 78);
        s_canvas.print("LAN: ");
        s_canvas.setTextColor(TFT_GREEN);
        s_canvas.setCursor(4 + 5*6, 78);
        s_canvas.print(s.sys_sta_ip.c_str());
        canvas_divider(90);
    } else {
        canvas_divider(80);
    }

    s_canvas.setTextColor(TFT_WHITE);
    s_canvas.setTextSize(1);
    s_canvas.setCursor(4, 98);
    s_canvas.printf("Clients: ");
    s_canvas.setTextColor(s.sys_clients > 0 ? TFT_GREEN : 0x8410);
    s_canvas.setTextSize(2);
    s_canvas.setCursor(4, 108);
    s_canvas.printf("%d", s.sys_clients);
}

// --- Page 1: LoRa ---
static void display_page_lora(const SensorData& s) {
    canvas_header("Radio");

    uint16_t statusColor = s.lora_ok ? TFT_GREEN : TFT_RED;
    s_canvas.setTextColor(statusColor);
    s_canvas.setTextSize(2);
    s_canvas.setCursor(4, 28);
    s_canvas.print(s.lora_status.c_str());

    s_canvas.setTextColor(TFT_WHITE);
    s_canvas.setTextSize(1);
    s_canvas.setCursor(4, 52);
    s_canvas.printf("Freq  %.1f MHz   SF%d", s.lora_freq, s.lora_sf);
    s_canvas.setCursor(4, 64);
    s_canvas.printf("BW    %.0f kHz   %d dBm", s.lora_bw, s.lora_power);

    canvas_divider(78);

    s_canvas.setTextColor(0x8410);
    s_canvas.setTextSize(1);
    s_canvas.setCursor(4, 86);
    s_canvas.print("Last packet");
    s_canvas.setTextColor(TFT_YELLOW);
    s_canvas.setCursor(4, 98);
    if (s.lora_ok && s.lora_rssi != 0.0f) {
        s_canvas.printf("RSSI %.1f dBm   SNR %.1f dB", s.lora_rssi, s.lora_snr);
    } else {
        s_canvas.print("no packets yet");
    }
}

// --- Page 2: System ---
static void display_page_system(const SensorData& s) {
    canvas_header("System");

    uint16_t batColor = s.bat_percent > 50 ? TFT_GREEN
                      : s.bat_percent > 20 ? TFT_YELLOW : TFT_RED;
    s_canvas.setTextColor(batColor);
    s_canvas.setTextSize(2);
    s_canvas.setCursor(4, 28);
    s_canvas.printf("%d%%", s.bat_percent);
    s_canvas.setTextSize(1);
    s_canvas.setTextColor(TFT_WHITE);
    s_canvas.setCursor(4 + 48, 36);
    s_canvas.printf("%.2fV %s", s.bat_voltage, s.bat_charging ? "[CHG]" : "");

    canvas_divider(52);

    uint32_t up = s.sys_uptime;
    s_canvas.setTextColor(0x8410);
    s_canvas.setTextSize(1);
    s_canvas.setCursor(4, 60);
    s_canvas.print("Uptime");
    s_canvas.setTextColor(DISPLAY_FG);
    s_canvas.setCursor(50, 60);
    s_canvas.printf("%02lu:%02lu:%02lu", up/3600, (up%3600)/60, up%60);

    s_canvas.setTextColor(0x8410);
    s_canvas.setCursor(4, 74);
    s_canvas.print("Heap");
    s_canvas.setTextColor(TFT_WHITE);
    s_canvas.setCursor(50, 74);
    s_canvas.printf("%.1f KB free", s.sys_heap / 1024.0f);

    s_canvas.setTextColor(0x8410);
    s_canvas.setCursor(4, 88);
    s_canvas.print("IMU");
    s_canvas.setTextColor(s.imu_ok ? TFT_GREEN : TFT_RED);
    s_canvas.setCursor(50, 88);
    s_canvas.print(s.imu_ok ? "ok" : "err");

    s_canvas.setTextColor(0x8410);
    s_canvas.setCursor(100, 88);
    s_canvas.print("GPS");
    s_canvas.setTextColor(s.gps_fix ? TFT_GREEN : TFT_YELLOW);
    s_canvas.setCursor(130, 88);
    s_canvas.print(s.gps_fix ? "fix" : "searching");
}

// --- Charging mode display ---
static void display_charging(const SensorData& s) {
    if (!s_canvas_ready) return;
    s_canvas.fillSprite(DISPLAY_BG);

    uint16_t batColor = s.bat_percent > 50 ? TFT_GREEN
                      : s.bat_percent > 20 ? TFT_YELLOW : TFT_RED;

    s_canvas.setTextColor(DISPLAY_FG);
    s_canvas.setTextSize(1);
    s_canvas.setCursor(4, 8);
    s_canvas.print("CHARGING MODE");

    s_canvas.setTextColor(batColor);
    s_canvas.setTextSize(4);
    s_canvas.setCursor(40, 40);
    s_canvas.printf("%d%%", s.bat_percent);

    s_canvas.setTextColor(TFT_WHITE);
    s_canvas.setTextSize(2);
    s_canvas.setCursor(80, 100);
    s_canvas.printf("%.2fV", s.bat_voltage);

    s_canvas.pushSprite(0, 0);
}

// --- USB MSC display ---
static void display_usb_msc() {
    if (!s_canvas_ready) return;
    s_canvas.fillSprite(DISPLAY_BG);
    s_canvas.setTextColor(DISPLAY_FG);
    s_canvas.setTextSize(2);
    s_canvas.setCursor(4, 20);
    s_canvas.print("USB DRIVE");
    s_canvas.setTextColor(TFT_WHITE);
    s_canvas.setTextSize(1);
    s_canvas.setCursor(4, 55);
    s_canvas.print("SD card exposed via USB.");
    s_canvas.setCursor(4, 70);
    s_canvas.print("Safely eject to resume.");
    s_canvas.pushSprite(0, 0);
}

static void displayStatus(const char* line1, const char* line2 = nullptr,
                          uint16_t color = TFT_GREEN) {
    if (!s_canvas_ready) {
        M5Cardputer.Display.fillScreen(DISPLAY_BG);
        M5Cardputer.Display.setTextColor(color);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setCursor(4, 20);
        M5Cardputer.Display.print(line1);
        if (line2) {
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(TFT_WHITE);
            M5Cardputer.Display.setCursor(4, 50);
            M5Cardputer.Display.print(line2);
        }
        return;
    }
    s_canvas.fillSprite(DISPLAY_BG);
    s_canvas.setTextColor(color);
    s_canvas.setTextSize(2);
    s_canvas.setCursor(4, 20);
    s_canvas.print(line1);
    if (line2) {
        s_canvas.setTextSize(1);
        s_canvas.setTextColor(TFT_WHITE);
        s_canvas.setCursor(4, 50);
        s_canvas.print(line2);
    }
    s_canvas.pushSprite(0, 0);
}

static void displayRunning() {
    if (!s_canvas_ready) return;

    SensorData snap;
    bool usb_active, charging;
    {
        SensorLock lock;
        snap       = g_sensors;
        usb_active = g_sensors.usb_msc_active;
        charging   = g_sensors.charging_mode;
    }

    if (usb_active) {
        display_usb_msc();
        return;
    }

    if (charging) {
        display_charging(snap);
        return;
    }

    // Auto-rotate pages
    uint32_t now_ms = millis();
    if (now_ms - s_page_changed_ms >= PAGE_ROTATE_MS) {
        s_display_page = (s_display_page + 1) % 3;
        s_page_changed_ms = now_ms;
    }

    if (M5Cardputer.Keyboard.isKeyPressed(KEY_TAB)) {
        s_display_page = (s_display_page + 1) % 3;
        s_page_changed_ms = now_ms;
    }

    switch (s_display_page) {
        case 0: display_page_network(snap); break;
        case 1: display_page_lora(snap);    break;
        case 2: display_page_system(snap);  break;
    }

    s_canvas.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// Sensor task — Core 1
// ---------------------------------------------------------------------------
static void sensorTask(void* pvParameters) {
    imu_init();
    audio_init();
    power_init();
    gps_init();
    lora_init();

    TickType_t lastSensor  = xTaskGetTickCount();
    TickType_t lastDisplay = xTaskGetTickCount();

    for (;;) {
        TickType_t now = xTaskGetTickCount();

        if (now - lastSensor >= pdMS_TO_TICKS(SENSOR_READ_MS)) {
            lastSensor = now;

            // Skip sensor updates if USB MSC is active (SD locked)
            bool usb_active;
            { SensorLock lock; usb_active = g_sensors.usb_msc_active; }

            if (!usb_active) {
                imu_update();
                audio_update();
                power_update();
                gps_update();
                lora_update();
            }

            {
                SensorLock lock;
                g_sensors.sys_uptime  = millis() / 1000;
                g_sensors.sys_heap    = esp_get_free_heap_size();
                g_sensors.sys_clients = wifiAP_clientCount();
            }

            // Display brightness command
            bool bright_pending = false;
            int  bright_val     = 200;
            {
                SensorLock lock;
                if (g_sensors.lora_brightness_pending) {
                    bright_pending = true;
                    bright_val     = g_sensors.display_brightness;
                    g_sensors.lora_brightness_pending = false;
                }
            }
            if (bright_pending) {
                M5Cardputer.Display.setBrightness(bright_val);
            }

            // Charging mode command
            bool charge_req = false, charge_val = false;
            {
                SensorLock lock;
                if (g_sensors.charging_mode_requested) {
                    charge_req = true;
                    charge_val = g_sensors.charging_mode_value;
                    g_sensors.charging_mode_requested = false;
                    g_sensors.charging_mode = charge_val;
                }
            }
            if (charge_req) {
                if (charge_val) {
                    M5Cardputer.Display.setBrightness(10);
                } else {
                    int br;
                    { SensorLock lock; br = g_sensors.display_brightness; }
                    M5Cardputer.Display.setBrightness(br);
                }
            }

            // USB MSC command
            bool msc_req = false;
            {
                SensorLock lock;
                if (g_sensors.usb_msc_requested && !g_sensors.usb_msc_active) {
                    msc_req = true;
                    g_sensors.usb_msc_requested = false;
                }
            }
            if (msc_req) {
                // Show USB drive screen immediately
                display_usb_msc();
                // Brief pause to let web response flush
                vTaskDelay(pdMS_TO_TICKS(500));
                usbmsc_start();
            }
        }

        // Refresh TFT every 250ms
        if (now - lastDisplay >= pdMS_TO_TICKS(250)) {
            lastDisplay = now;
            displayRunning();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ---------------------------------------------------------------------------
// WebServer task — Core 0
// ---------------------------------------------------------------------------
static void webserverTask(void* pvParameters) {
    wifiAP_init();
    wifiAP_task(nullptr);  // loops forever
}

// ---------------------------------------------------------------------------
// Setup & Loop
// ---------------------------------------------------------------------------
void setup() {
    M5Cardputer.begin(true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(200);
    canvas_init();

    Serial.begin(115200);
    delay(200);
    Serial.println("\n[dashIO] Boot v" DASHIO_VERSION);

    g_sensors_mutex = xSemaphoreCreateMutex();

    // Mount SD card
    displayStatus("dashIO", "Mounting SD...", TFT_CYAN);
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[SD] Mount failed");
        displayStatus("SD Error", "Insert SD card", TFT_RED);
        delay(3000);
    } else {
        Serial.println("[SD] Mounted OK");
        // Cache sector count for USB MSC (before any async access)
        uint64_t card_size = SD.cardSize();
        { SensorLock lock; g_sensors.sd_sector_count = (uint32_t)(card_size / 512); }
        Serial.printf("[SD] Card: %.1f MB, %lu sectors\n",
                      card_size / (1024.0 * 1024.0), card_size / 512);

        config_load();
        displayStatus("dashIO", "Checking assets...", TFT_CYAN);
        assets_checkAndExtract();
    }

    displayStatus("dashIO", "Starting tasks...", TFT_CYAN);
    delay(300);

    xTaskCreatePinnedToCore(webserverTask, "WebServer",
                            WEBSERVER_TASK_STACK, nullptr,
                            WEBSERVER_TASK_PRIORITY, nullptr,
                            WEBSERVER_TASK_CORE);

    xTaskCreatePinnedToCore(sensorTask, "Sensors",
                            SENSOR_TASK_STACK, nullptr,
                            SENSOR_TASK_PRIORITY, nullptr,
                            SENSOR_TASK_CORE);

    Serial.println("[dashIO] Ready");
}

void loop() {
    M5Cardputer.update();
    vTaskDelay(pdMS_TO_TICKS(100));
}
