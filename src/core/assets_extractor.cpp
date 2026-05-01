#include "assets_extractor.h"
#include "web_assets.h"

#include <SD.h>
#include <Arduino.h>

// Write a single asset from PROGMEM to SD card in chunks to avoid large RAM allocation
static const size_t CHUNK = 512;

static bool writeAsset(const WebAsset& asset) {
    File f = SD.open(asset.path, FILE_WRITE);
    if (!f) {
        Serial.printf("[assets] cannot open for write: %s\n", asset.path);
        return false;
    }

    uint8_t buf[CHUNK];
    size_t written = 0;
    while (written < asset.size) {
        size_t chunk = min(CHUNK, asset.size - written);
        memcpy_P(buf, asset.data + written, chunk);
        f.write(buf, chunk);
        written += chunk;
    }
    f.close();

    Serial.printf("[assets] wrote %s (%zu bytes)\n", asset.path, asset.size);
    return true;
}

bool assets_checkAndExtract() {
    // Check if assets already exist
    if (SD.exists("/dashIO/index.html")) {
        Serial.println("[assets] Assets present — skipping extraction");
        return true;
    }

    Serial.println("[assets] First boot — extracting web assets to SD...");

    // Ensure /dashIO directory exists
    if (!SD.exists("/dashIO")) {
        SD.mkdir("/dashIO");
    }

    int ok = 0, fail = 0;
    for (int i = 0; i < WEB_ASSET_COUNT; i++) {
        WebAsset asset;
        memcpy_P(&asset, &WEB_ASSETS[i], sizeof(WebAsset));
        if (writeAsset(asset)) ok++;
        else fail++;
    }

    Serial.printf("[assets] Extraction done: %d ok, %d failed\n", ok, fail);
    return (fail == 0);
}
