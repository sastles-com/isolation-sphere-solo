/**
 * @file Settings.cpp
 * @brief Settings実装
 */

#include "Settings.h"

#include <Arduino.h>
#include <Preferences.h>

namespace sastle {

namespace {
constexpr const char* kNamespace = "solo";   // STA 資格情報と同じ namespace
constexpr const char* kKeyBrightness = "bri";

bool s_ready = false;
bool s_dirty = false;
uint32_t s_dirtyAtMs = 0;
uint8_t s_brightness = 0;
bool s_hasBrightness = false;

/// 1 回だけ開いて読み、以降は RAM 上の値で応答する
void writeBrightness(uint8_t percent) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        Serial.println("[Settings] NVS open failed (brightness not saved)");
        return;
    }
    prefs.putUChar(kKeyBrightness, percent);
    prefs.end();
    Serial.printf("[Settings] saved brightness=%u%%\n", (unsigned)percent);
}
}  // namespace

void Settings::begin() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        Serial.println("[Settings] NVS not available (using config.json defaults)");
        s_ready = true;  // 読めなくても動作は継続する
        return;
    }
    if (prefs.isKey(kKeyBrightness)) {
        s_brightness = prefs.getUChar(kKeyBrightness, 50);
        if (s_brightness > 100) {
            s_brightness = 100;
        }
        s_hasBrightness = true;
    }
    prefs.end();
    s_ready = true;
    if (s_hasBrightness) {
        Serial.printf("[Settings] restored brightness=%u%%\n", (unsigned)s_brightness);
    } else {
        Serial.println("[Settings] no saved settings yet (using config.json defaults)");
    }
}

uint8_t Settings::brightness(uint8_t fallback) {
    return s_hasBrightness ? s_brightness : fallback;
}

void Settings::setBrightness(uint8_t percent) {
    if (percent > 100) {
        percent = 100;
    }
    if (s_hasBrightness && s_brightness == percent) {
        return;  // 値が変わっていなければ書かない
    }
    s_brightness = percent;
    s_hasBrightness = true;
    s_dirty = true;
    s_dirtyAtMs = millis();
}

void Settings::tick() {
    if (!s_ready || !s_dirty) {
        return;
    }
    if (millis() - s_dirtyAtMs < kSaveDelayMs) {
        return;  // まだ操作中かもしれないので待つ
    }
    s_dirty = false;
    writeBrightness(s_brightness);
}

void Settings::flush() {
    if (!s_ready || !s_dirty) {
        return;
    }
    s_dirty = false;
    writeBrightness(s_brightness);
}

}  // namespace sastle
