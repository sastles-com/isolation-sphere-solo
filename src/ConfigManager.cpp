#include "ConfigManager.h"

// グローバルデバッグフラグ (main.cppで定義)
extern bool g_debugEnabled;

namespace sastle {

ConfigManager::ConfigManager() : doc(8192) {
}

ConfigManager::~ConfigManager() {
}

bool ConfigManager::loadConfig(const char* path) {
    Serial.printf("Loading config from: %s\n", path);
    
    String jsonStr;
    if (!FileManager::readFile(path, jsonStr)) {
        Serial.println("Failed to read config file");
        return false;
    }
    
    Serial.printf("Config file size: %d bytes\n", jsonStr.length());
    
    return parseJSON(jsonStr);
}

bool ConfigManager::parseJSON(const String& jsonStr) {
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        return false;
    }
    
    Serial.println("Config parsed successfully");
    return true;
}

bool ConfigManager::saveConfig(const char* path) {
    String output;
    serializeJsonPretty(doc, output);
    
    if (FileManager::writeFile(path, output)) {
        Serial.printf("Config saved to: %s\n", path);
        return true;
    } else {
        Serial.println("Failed to save config");
        return false;
    }
}

SystemConfig ConfigManager::getSystemConfig() {
    SystemConfig config;
    config.debug = doc["system"]["debug"] | false;
    config.PSRAM = doc["system"]["PSRAM"] | false;
    
    // グローバルデバッグフラグを設定
    g_debugEnabled = config.debug;
    
    return config;
}

OTAConfig ConfigManager::getOTAConfig() {
    OTAConfig config;
    config.enabled = doc["system"]["ota"]["enabled"] | true;
    config.username = doc["system"]["ota"]["username"] | "admin";
    config.password = doc["system"]["ota"]["password"] | "";
    config.listen_port = doc["system"]["ota"]["listen_port"] | 3232;
    return config;
}

PathsConfig ConfigManager::getPathsConfig() {
    PathsConfig config;
    config.config = doc["system"]["paths"]["config"] | "/littlefs/config.json";
    config.layout = doc["system"]["paths"]["layout"] | "/littlefs/led_layouts-5strip.csv";
    config.logs = doc["system"]["paths"]["logs"] | "/littlefs/logs/";
    return config;
}

SoloConfig ConfigManager::getSoloConfig() {
    SoloConfig config;
    config.video_path = getSoloVideoPath();
    config.http_port = getSoloHttpPort();
    config.ap_ssid = getSoloApSsid();
    config.ap_password = getSoloApPassword();
    config.ap_ip = getSoloApIp();
    return config;
}

ImageConfig ConfigManager::getImageConfig() {
    ImageConfig config;
    config.width = doc["image"]["width"] | 320;
    config.height = doc["image"]["height"] | 160;
    config.format = doc["image"]["format"] | "RGB565";
    config.type = doc["image"]["type"] | "JPEG";
    return config;
}

SphereConfig ConfigManager::getSphereConfig() {
    SphereConfig config;
    config.id = doc["sphere"]["id"] | "sphere001";
    config.LED_enabled = doc["sphere"]["features"]["LED"] | false;
    config.IMU_type = doc["sphere"]["features"]["IMU"] | "";
    
    // LCD config
    config.lcd.width = doc["sphere"]["features"]["LCD"]["width"] | 128;
    config.lcd.height = doc["sphere"]["features"]["LCD"]["height"] | 128;
    config.lcd.rotation = doc["sphere"]["features"]["LCD"]["rotation"] | 0;
    config.lcd.offset[0] = doc["sphere"]["features"]["LCD"]["offset"][0] | 0;
    config.lcd.offset[1] = doc["sphere"]["features"]["LCD"]["offset"][1] | 0;
    config.lcd.color_depth = doc["sphere"]["features"]["LCD"]["color_depth"] | 16;
    config.lcd.switch_enabled = doc["sphere"]["features"]["LCD"]["switch"] | true;
    config.lcd.debug = doc["sphere"]["features"]["LCD"]["debug"] | true;
    
    return config;
}

void ConfigManager::printConfig() {
    Serial.println("\n=== Configuration ===");

    SystemConfig sys = getSystemConfig();
    Serial.printf("System:\n");
    Serial.printf("  PSRAM: %s, Debug: %s\n",
                  sys.PSRAM ? "enabled" : "disabled",
                  sys.debug ? "enabled" : "disabled");

    SoloConfig solo = getSoloConfig();
    Serial.printf("\nSolo:\n");
    Serial.printf("  Video: %s\n", solo.video_path.c_str());
    Serial.printf("  AP:    %s (ip %s, http %u)\n",
                  solo.ap_ssid.c_str(), solo.ap_ip.c_str(), (unsigned)solo.http_port);

    OTAConfig ota = getOTAConfig();
    Serial.printf("\nOTA: %s\n", ota.enabled ? "enabled" : "disabled");
    Serial.printf("  Username: %s, Port: %d\n", ota.username.c_str(), ota.listen_port);

    ImageConfig img = getImageConfig();
    Serial.printf("\nImage: %dx%d %s (%s)\n",
                  img.width, img.height, img.format.c_str(), img.type.c_str());

    SphereConfig sphere = getSphereConfig();
    Serial.printf("\nSphere: %s\n", sphere.id.c_str());
    Serial.printf("  LED: %s, IMU: %s\n",
                  sphere.LED_enabled ? "enabled" : "disabled",
                  sphere.IMU_type.c_str());
    Serial.printf("  LCD: %dx%d, Depth: %d, Debug: %s\n",
                  sphere.lcd.width, sphere.lcd.height,
                  sphere.lcd.color_depth,
                  sphere.lcd.debug ? "enabled" : "disabled");

    PathsConfig paths = getPathsConfig();
    Serial.printf("\nPaths:\n");
    Serial.printf("  Config: %s\n", paths.config.c_str());
    Serial.printf("  Layout: %s\n", paths.layout.c_str());

    Serial.printf("\nParams (startup defaults):\n");
    Serial.printf("  Brightness: %d%%\n", getParamBrightness());

    Serial.println("====================\n");
}

} // namespace sastle
