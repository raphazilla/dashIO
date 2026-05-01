#include "lora_module.h"
#include "../core/sensor_data.h"
#include "../core/wifi_ap.h"
#include "../config.h"

#include <RadioLib.h>

// SX1262 instance — SPI is shared but CS is distinct from SD card
static SX1262 radio = new Module(LORA_NSS_PIN, LORA_IRQ_PIN, LORA_RST_PIN, LORA_BUSY_PIN);

static volatile bool s_rx_flag = false;

// Interrupt callback (IRAM-safe)
static void IRAM_ATTR onRxDone() {
    s_rx_flag = true;
}

// Convert bytes to hex string
static String toHex(const uint8_t* data, size_t len) {
    String hex;
    hex.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", data[i]);
        hex += buf;
    }
    return hex;
}

// Convert bytes to printable ASCII (replace non-printable with '.')
static String toText(const uint8_t* data, size_t len) {
    String text;
    text.reserve(len);
    for (size_t i = 0; i < len; i++) {
        text += (data[i] >= 0x20 && data[i] < 0x7F) ? (char)data[i] : '.';
    }
    return text;
}

bool lora_init() {
    Serial.println("[LoRa] Initializing SX1262...");

    int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                            RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_POWER_DBM,
                            LORA_PREAMBLE);

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LoRa] Init failed: %d\n", state);
        SensorLock lock;
        g_sensors.lora_ok     = false;
        g_sensors.lora_status = "error";
        return false;
    }

    radio.setDio1Action(onRxDone);
    radio.startReceive();

    SensorLock lock;
    g_sensors.lora_ok     = true;
    g_sensors.lora_status = "rx";
    g_sensors.lora_freq   = LORA_FREQ_MHZ;
    g_sensors.lora_sf     = LORA_SF;
    g_sensors.lora_bw     = LORA_BW_KHZ;
    g_sensors.lora_power  = LORA_POWER_DBM;

    Serial.println("[LoRa] Ready — listening on 868MHz");
    return true;
}

void lora_update() {
    // --- Handle incoming RX interrupt ---
    if (s_rx_flag) {
        s_rx_flag = false;

        uint8_t buf[256];
        size_t  len = 0;
        int state = radio.readData(buf, sizeof(buf));

        if (state == RADIOLIB_ERR_NONE) {
            float rssi = radio.getRSSI();
            float snr  = radio.getSNR();
            len = radio.getPacketLength();

            LoRaPacket pkt;
            pkt.timestamp = millis();
            pkt.rssi    = rssi;
            pkt.snr     = snr;
            pkt.payload = toHex(buf, len);
            pkt.text    = toText(buf, len);

            {
                SensorLock lock;
                g_sensors.lora_rssi   = rssi;
                g_sensors.lora_snr    = snr;
                g_sensors.lora_status = "rx";
                // Keep log bounded
                if (g_sensors.lora_log.size() >= LORA_PACKET_LOG) {
                    g_sensors.lora_log.erase(g_sensors.lora_log.begin());
                }
                g_sensors.lora_log.push_back(pkt);
            }

            wifiAP_notifyLoRaRx();
            Serial.printf("[LoRa] RX %d bytes  RSSI=%.1f  SNR=%.1f\n", (int)len, rssi, snr);
        }

        radio.startReceive();
    }

    // --- Handle TX command from web ---
    bool tx_pending = false;
    String tx_payload;
    {
        SensorLock lock;
        if (g_sensors.lora_tx_pending) {
            tx_pending = true;
            tx_payload = g_sensors.lora_tx_payload;
            g_sensors.lora_tx_pending = false;
            g_sensors.lora_status     = "tx";
        }
    }

    if (tx_pending) {
        radio.standby();
        int state = radio.transmit((uint8_t*)tx_payload.c_str(), tx_payload.length());
        if (state == RADIOLIB_ERR_NONE) {
            Serial.printf("[LoRa] TX ok: %s\n", tx_payload.c_str());
        } else {
            Serial.printf("[LoRa] TX failed: %d\n", state);
        }
        radio.startReceive();
        {
            SensorLock lock;
            g_sensors.lora_status = "rx";
        }
    }

    // --- Handle config change from web ---
    bool cfg_pending = false;
    int cfg_sf; float cfg_bw; int cfg_power;
    {
        SensorLock lock;
        if (g_sensors.lora_cfg_pending) {
            cfg_pending  = true;
            cfg_sf       = g_sensors.lora_cfg_sf;
            cfg_bw       = g_sensors.lora_cfg_bw;
            cfg_power    = g_sensors.lora_cfg_power;
            g_sensors.lora_cfg_pending = false;
        }
    }

    if (cfg_pending) {
        radio.standby();
        radio.setSpreadingFactor(cfg_sf);
        radio.setBandwidth(cfg_bw);
        radio.setOutputPower(cfg_power);
        radio.startReceive();
        {
            SensorLock lock;
            g_sensors.lora_sf     = cfg_sf;
            g_sensors.lora_bw     = cfg_bw;
            g_sensors.lora_power  = cfg_power;
            g_sensors.lora_status = "rx";
        }
        Serial.printf("[LoRa] Config updated SF=%d BW=%.0f PWR=%d\n", cfg_sf, cfg_bw, cfg_power);
    }
}
