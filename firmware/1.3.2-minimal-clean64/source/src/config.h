#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

struct Config {
  static constexpr const char* path = "/sdpro-minimal.json";
  static constexpr const char* pendingPath = "/sdpro-minimal.tmp";
  static constexpr const char* legacyPath = "/config.json";
  String ssid;
  String password;
  int brightness = 60;

  bool load() {
    if (loadFrom(path)) return true;
    loadFrom(legacyPath);
    return false;
  }

  bool save() const {
    JsonDocument doc;
    doc["ssid"] = ssid;
    doc["password"] = password;
    doc["brightness"] = brightness;
    if (doc.overflowed()) return false;
    File file = LittleFS.open(pendingPath, "w");
    if (!file) return false;
    size_t written = serializeJson(doc, file);
    file.flush();
    bool ok = written == measureJson(doc) && !file.getWriteError();
    file.close();
    // LittleFS replaces the destination atomically. A failed save keeps the old copy.
    if (ok && matches(pendingPath) && LittleFS.rename(pendingPath, path)) return matches(path);
    LittleFS.remove(pendingPath);
    return false;
  }

 private:
  bool matches(const char* filename) const {
    Config saved;
    return saved.loadFrom(filename) && saved.ssid == ssid && saved.password == password &&
           saved.brightness == brightness;
  }

  bool loadFrom(const char* filename) {
    File file = LittleFS.open(filename, "r");
    if (!file) return false;
    JsonDocument filter;
    filter["ssid"] = true;
    filter["password"] = true;
    filter["brightness"] = true;
    JsonDocument doc;
    auto error = deserializeJson(doc, file, DeserializationOption::Filter(filter));
    file.close();
    if (error || !doc.is<JsonObject>() || !doc["brightness"].is<int>()) return false;
    JsonString storedSsid = doc["ssid"].as<JsonString>();
    JsonString storedPassword = doc["password"].as<JsonString>();
    int storedBrightness = doc["brightness"].as<int>();
    if (storedSsid.isNull() || storedPassword.isNull() ||
        storedSsid.size() > 32 || storedPassword.size() > 64 ||
        strlen(storedSsid.c_str()) != storedSsid.size() ||
        strlen(storedPassword.c_str()) != storedPassword.size() ||
        storedBrightness < 0 || storedBrightness > 100) return false;
    ssid = storedSsid.c_str();
    password = storedPassword.c_str();
    if (ssid != storedSsid.c_str() || password != storedPassword.c_str()) return false;
    brightness = constrain(storedBrightness, 1, 100);
    return true;
  }
};
