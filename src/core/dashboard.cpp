#include "dashboard.h"
#include "sensor_data.h"

#include <ArduinoJson.h>

// Build full sensor JSON snapshot (sent every WS_BROADCAST_MS)
String dashboard_buildSensorJson() {
    JsonDocument doc;

    {
        SensorLock lock;

        doc["type"] = "sensors";

        auto imu = doc["imu"].to<JsonObject>();
        imu["ax"] = serialized(String(g_sensors.imu_ax, 3));
        imu["ay"] = serialized(String(g_sensors.imu_ay, 3));
        imu["az"] = serialized(String(g_sensors.imu_az, 3));
        imu["gx"] = serialized(String(g_sensors.imu_gx, 3));
        imu["gy"] = serialized(String(g_sensors.imu_gy, 3));
        imu["gz"] = serialized(String(g_sensors.imu_gz, 3));
        imu["ok"] = g_sensors.imu_ok;

        auto bat = doc["battery"].to<JsonObject>();
        bat["voltage"]  = serialized(String(g_sensors.bat_voltage, 2));
        bat["percent"]  = g_sensors.bat_percent;
        bat["charging"] = g_sensors.bat_charging;
        bat["ok"]       = g_sensors.bat_ok;

        auto audio = doc["audio"].to<JsonObject>();
        audio["level"] = g_sensors.audio_level;
        audio["peak"]  = g_sensors.audio_peak;
        audio["ok"]    = g_sensors.audio_ok;

        auto lora = doc["lora"].to<JsonObject>();
        lora["ok"]     = g_sensors.lora_ok;
        lora["status"] = g_sensors.lora_status;
        lora["freq"]   = serialized(String(g_sensors.lora_freq, 1));
        lora["sf"]     = g_sensors.lora_sf;
        lora["bw"]     = serialized(String(g_sensors.lora_bw, 0));
        lora["power"]  = g_sensors.lora_power;
        lora["rssi"]   = serialized(String(g_sensors.lora_rssi, 1));
        lora["snr"]    = serialized(String(g_sensors.lora_snr, 1));

        auto gps = doc["gps"].to<JsonObject>();
        gps["fix"]        = g_sensors.gps_fix;
        gps["lat"]        = serialized(String(g_sensors.gps_lat, 6));
        gps["lon"]        = serialized(String(g_sensors.gps_lon, 6));
        gps["satellites"] = g_sensors.gps_satellites;
        gps["hdop"]       = serialized(String(g_sensors.gps_hdop, 1));

        auto sys = doc["system"].to<JsonObject>();
        sys["uptime"]  = g_sensors.sys_uptime;
        sys["heap"]    = g_sensors.sys_heap;
        sys["clients"] = g_sensors.sys_clients;
        sys["sta_ip"]  = g_sensors.sys_sta_ip;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

// Build LoRa RX event JSON (sent immediately on packet arrival)
String dashboard_buildLoRaRxJson() {
    SensorLock lock;

    if (g_sensors.lora_log.empty()) return "";

    const LoRaPacket& pkt = g_sensors.lora_log.back();

    JsonDocument doc;
    doc["type"]    = "lora_rx";
    doc["ts"]      = pkt.timestamp;
    doc["rssi"]    = serialized(String(pkt.rssi, 1));
    doc["snr"]     = serialized(String(pkt.snr, 1));
    doc["payload"] = pkt.payload;
    doc["text"]    = pkt.text;

    String out;
    serializeJson(doc, out);
    return out;
}
