#include "FileManager.h"

namespace sastle {

bool FileManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
        return false;
    }
    Serial.println("LittleFS mounted successfully");
    printInfo();
    return true;
}

void FileManager::end() {
    LittleFS.end();
}

bool FileManager::readFile(const char* path, String &output) {
    File file = LittleFS.open(path, "r");
    if (!file) {
        Serial.printf("Failed to open file for reading: %s\n", path);
        return false;
    }

    output = "";
    while (file.available()) {
        output += (char)file.read();
    }
    file.close();
    
    Serial.printf("Read %d bytes from %s\n", output.length(), path);
    return true;
}

bool FileManager::writeFile(const char* path, const String &content) {
    File file = LittleFS.open(path, "w");
    if (!file) {
        Serial.printf("Failed to open file for writing: %s\n", path);
        return false;
    }

    size_t bytesWritten = file.print(content);
    file.close();
    
    if (bytesWritten == content.length()) {
        Serial.printf("Wrote %d bytes to %s\n", bytesWritten, path);
        return true;
    } else {
        Serial.printf("Write failed: %d/%d bytes written\n", bytesWritten, content.length());
        return false;
    }
}

void FileManager::ls(const char* dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\n", dirname);

    File root = LittleFS.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels) {
                ls(file.path(), levels - 1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

bool FileManager::exists(const char* path) {
    return LittleFS.exists(path);
}

bool FileManager::remove(const char* path) {
    if (LittleFS.remove(path)) {
        Serial.printf("Removed file: %s\n", path);
        return true;
    } else {
        Serial.printf("Failed to remove file: %s\n", path);
        return false;
    }
}

size_t FileManager::totalBytes() {
    return LittleFS.totalBytes();
}

size_t FileManager::usedBytes() {
    return LittleFS.usedBytes();
}

void FileManager::printInfo() {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    Serial.println("=== LittleFS Info ===");
    Serial.printf("Total: %d bytes (%.2f MB)\n", total, total / 1024.0 / 1024.0);
    Serial.printf("Used:  %d bytes (%.2f MB)\n", used, used / 1024.0 / 1024.0);
    Serial.printf("Free:  %d bytes (%.2f MB)\n", total - used, (total - used) / 1024.0 / 1024.0);
    Serial.printf("Usage: %.1f%%\n", (used * 100.0) / total);
    Serial.println("====================");
}

} // namespace sastle
