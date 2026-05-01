#include "audio_module.h"
#include "../core/sensor_data.h"

#include <M5Cardputer.h>
#include <cmath>

static const int SAMPLE_RATE   = 16000;
static const int SAMPLE_BUFFER = 256;
static int16_t   s_buf[SAMPLE_BUFFER];
static int       s_peak_hold   = 0;
static int       s_peak_decay  = 0;

bool audio_init() {
    auto cfg = M5Cardputer.Mic.config();
    cfg.sample_rate = SAMPLE_RATE;
    cfg.noise_filter_level = 128;
    M5Cardputer.Mic.config(cfg);
    bool ok = M5Cardputer.Mic.begin();
    Serial.printf("[Audio] Mic init: %s\n", ok ? "ok" : "fail");

    SensorLock lock;
    g_sensors.audio_ok = ok;
    return ok;
}

void audio_update() {
    if (!M5Cardputer.Mic.isEnabled()) return;
    if (!M5Cardputer.Mic.record(s_buf, SAMPLE_BUFFER, SAMPLE_RATE)) return;

    // Compute RMS
    int64_t sum = 0;
    for (int i = 0; i < SAMPLE_BUFFER; i++) {
        sum += (int64_t)s_buf[i] * s_buf[i];
    }
    int rms = (int)sqrtf((float)(sum / SAMPLE_BUFFER));

    // Peak hold with decay
    if (rms > s_peak_hold) {
        s_peak_hold  = rms;
        s_peak_decay = 0;
    } else {
        s_peak_decay++;
        if (s_peak_decay > 20) s_peak_hold = max(0, s_peak_hold - 200);
    }

    SensorLock lock;
    g_sensors.audio_level = rms;
    g_sensors.audio_peak  = s_peak_hold;
}

void audio_stop() {
    M5Cardputer.Mic.end();
}
