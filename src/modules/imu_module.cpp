#include "imu_module.h"
#include "../core/sensor_data.h"

#include <M5Cardputer.h>

bool imu_init() {
    // IMU is accessed via M5Unified's M5 global (M5Cardputer wraps M5Unified)
    auto imu_type = M5.Imu.getType();
    bool ok = (imu_type != m5::imu_none);
    Serial.printf("[IMU] Type: %d  OK: %s\n", (int)imu_type, ok ? "yes" : "no");

    SensorLock lock;
    g_sensors.imu_ok = ok;
    return ok;
}

void imu_update() {
    if (!M5.Imu.update()) return;

    auto data = M5.Imu.getImuData();

    SensorLock lock;
    g_sensors.imu_ax = data.accel.x;
    g_sensors.imu_ay = data.accel.y;
    g_sensors.imu_az = data.accel.z;
    g_sensors.imu_gx = data.gyro.x;
    g_sensors.imu_gy = data.gyro.y;
    g_sensors.imu_gz = data.gyro.z;
}
