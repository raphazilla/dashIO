#include "power_module.h"
#include "../core/sensor_data.h"

#include <M5Cardputer.h>

void power_init() {
    // M5Cardputer.begin() already initializes Power (AXP2101)
    SensorLock lock;
    g_sensors.bat_ok = true;
}

void power_update() {
    float voltage  = M5Cardputer.Power.getBatteryVoltage() / 1000.0f;
    int   percent  = M5Cardputer.Power.getBatteryLevel();
    bool  charging = M5Cardputer.Power.isCharging();

    SensorLock lock;
    g_sensors.bat_voltage  = voltage;
    g_sensors.bat_percent  = percent;
    g_sensors.bat_charging = charging;
}
