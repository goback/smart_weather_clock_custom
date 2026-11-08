// SD PRO minimal firmware based on SmartClock-OSS v1.3.2.
// The tested display wiring, boot recovery and firmware updater are retained.

#include <Arduino.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <TFT_eSPI.h>
#include <Updater.h>
#include <algorithm>
#include "build_info.h"
#include "config.h"
#include "firmware_validation.h"
#include "legacy_assets.h"
#include "pins.h"
#include "web_index.h"

#define FW_VERSION "1.3.2-minimal-clean64"

TFT_eSPI& clockDisplay() {
  // Delay construction until the update server is already available.
  static TFT_eSPI panel;
  return panel;
}

ESP8266WebServer server(80);
DNSServer dns;
Config cfg;
LegacyCleanup cleanup;
uint32_t heapLow = UINT32_MAX;

// "smartclock-1a2b3c": unique per unit, used for mDNS and as the DHCP hostname so
// the router can list the clock by name (mDNS does not cross subnets).
String deviceHost() {
  char name[20];
  snprintf(name, sizeof(name), "smartclock-%06x", (unsigned)(ESP.getChipId() & 0xffffff));
  return name;
}

enum Mode { MODE_AP, MODE_RUN } mode = MODE_AP;
bool recoveryMode = false, recoveryDisplay = true, recoveryLinkShown = false;
bool storageReady = false, storageMountFailed = false, displayReady = false;
bool optionalStarted = false, bootHealthy = false, powerCyclesCleared = false;
uint32_t bootStarted = 0;
// RTC user memory starts after eboot's reserved first 128 bytes (word offset 32).
struct BootMarker { uint32_t magic, state; };
constexpr uint32_t BOOT_MAGIC = 0x53434c4b;
// STARTING: a reset during optional startup forces recovery next boot.
// RECOVERY_DISPLAY: a reset while recovery starts the panel skips the panel next boot.
enum BootState : uint32_t { BOOT_CLEAN = 0, BOOT_STARTING = 1, BOOT_RECOVERY_DISPLAY = 2 };
void markBoot(BootState state) {
  BootMarker marker{BOOT_MAGIC, state};
  ESP.rtcUserMemoryWrite(32, reinterpret_cast<uint32_t*>(&marker), sizeof(marker));
}

// Three consecutive boots that each end within 10 s (quick power cycles, or a crash
// loop) force recovery. RTC memory is lost on power-off, so the count lives in the
// EEPROM sector. The whole sector is loaded so stock-firmware bytes in it survive, and
// EEPROM.end() only erases/writes when a byte actually changed.
constexpr int CYCLE_ADDR = 0xff0;
constexpr uint32_t CYCLE_MAGIC = 0x53435943;
constexpr uint8_t CYCLE_LIMIT = 3;
constexpr uint32_t CYCLE_WINDOW_MS = 10000;
void writeCycles(uint8_t count) {
  for (int i = 0; i < 4; ++i) EEPROM.write(CYCLE_ADDR + i, uint8_t(CYCLE_MAGIC >> (8 * i)));
  EEPROM.write(CYCLE_ADDR + 4, count);
}
uint8_t readCycles() {
  uint32_t magic = 0;
  for (int i = 0; i < 4; ++i) magic |= uint32_t(EEPROM.read(CYCLE_ADDR + i)) << (8 * i);
  return magic == CYCLE_MAGIC ? EEPROM.read(CYCLE_ADDR + 4) : 0;
}
bool countPowerCycle() {   // true when this boot completes the recovery sequence
  EEPROM.begin(SPI_FLASH_SEC_SIZE);
  uint8_t count = std::min<uint8_t>(readCycles(), CYCLE_LIMIT) + 1;
  bool trigger = count >= CYCLE_LIMIT;
  writeCycles(trigger ? 0 : count);
  EEPROM.end();
  return trigger;
}
void clearPowerCycles() {
  EEPROM.begin(SPI_FLASH_SEC_SIZE);
  if (readCycles() != 0) writeCycles(0);
  EEPROM.end();
}

// Every deliberate reboot clears both recovery triggers so it cannot be mistaken
// for a crash or a power-cycle sequence.
void safeRestart() {
  markBoot(BOOT_CLEAN);
  clearPowerCycles();
  delay(500);
  ESP.restart();
}

// ---------------------------- Device password -------------------------------
// One password protects the setup network (WPA2) and the settings page. It lives in
// the EEPROM sector, not LittleFS, so recovery can still read it. Generated on first
// boot from the hardware RNG; shown on the setup screen, the recovery screen, and on
// request from the login page ("Show on clock"), so knowing it needs sight of the clock.
constexpr int PASS_ADDR = 0xfc0;            // 4-byte magic + up to 31 chars + NUL
constexpr uint32_t PASS_MAGIC = 0x53435057;
constexpr size_t PASS_MIN = 8, PASS_MAX = 31;
String devicePassword;

bool validPassword(const String& p) {
  if (p.length() < PASS_MIN || p.length() > PASS_MAX) return false;
  for (unsigned i = 0; i < p.length(); ++i) if (p[i] < 0x21 || p[i] > 0x7e) return false;   // printable, no spaces
  return true;
}

bool storePassword(const String& p) {
  EEPROM.begin(SPI_FLASH_SEC_SIZE);
  for (int i = 0; i < 4; ++i) EEPROM.write(PASS_ADDR + i, uint8_t(PASS_MAGIC >> (8 * i)));
  for (size_t i = 0; i <= PASS_MAX; ++i) EEPROM.write(PASS_ADDR + 4 + i, i < p.length() ? p[i] : 0);
  return EEPROM.end();
}

void loadPassword() {
  EEPROM.begin(SPI_FLASH_SEC_SIZE);
  uint32_t magic = 0;
  for (int i = 0; i < 4; ++i) magic |= uint32_t(EEPROM.read(PASS_ADDR + i)) << (8 * i);
  String p;
  if (magic == PASS_MAGIC)
    for (size_t i = 0; i < PASS_MAX; ++i) { char c = EEPROM.read(PASS_ADDR + 4 + i); if (!c) break; p += c; }
  EEPROM.end();
  if (validPassword(p)) { devicePassword = p; return; }
  // 10 characters from a 31-letter alphabet without 0/o, 1/i/l: ~49 bits.
  static const char alphabet[] = "23456789abcdefghjkmnpqrstuvwxyz";
  p = "";
  for (int i = 0; i < 10; ++i) p += alphabet[ESP.random() % (sizeof(alphabet) - 1)];
  storePassword(p);
  devicePassword = p;
}

void setBacklight(int percent) {
  if (!displayReady) return;
  percent = constrain(percent, 0, 100);
  // Stock: analogWrite(5, 1023 - brightness*10). Active-low => low duty = bright.
  int duty = BL_PWM_RANGE - (percent * BL_PWM_RANGE) / 100;
  analogWrite(PIN_BACKLIGHT, duty);
}

void displayBegin() {
  displayReady = true;
  pinMode(PIN_BACKLIGHT, OUTPUT);
  analogWriteRange(BL_PWM_RANGE);
  setBacklight(0);                   // off during init (avoid flash of noise)
  clockDisplay().init();
  clockDisplay().setRotation(0);                // stock boots rotation 0, offset (0,0)
  clockDisplay().fillScreen(TFT_BLACK);
  setBacklight(cfg.brightness);
}

void centerText(const String& s, int y, uint8_t font, uint16_t color) {
  clockDisplay().setTextColor(color, TFT_BLACK);
  clockDisplay().setTextDatum(MC_DATUM);
  clockDisplay().drawString(s, TFT_W / 2, y, font);
}

// ---------------------------- Sign-in ---------------------------------------
// A session is a random token in a cookie (HttpOnly, SameSite=Strict), kept in RAM:
// a reboot signs everyone out. Recovery mode needs no sign-in, so a lost password
// can never lock the owner out; entering it takes power cycling the clock.
constexpr int SESSIONS = 4;
String sessions[SESSIONS];
uint8_t loginFails = 0;
uint32_t loginLockedUntil = 0, revealUntil = 0, revealAt = 0;

// Ends "Show on clock" now: the loop sees the deadline has passed and restores the
// face (or the setup/recovery screen). 0 means "not showing", so never store 0.
void endReveal() {
  if (revealUntil) revealUntil = millis() ? millis() : 1;
}

String requestToken() {
  String c = server.header("Cookie");
  int i = c.indexOf("sc=");
  if (i < 0) return "";
  int end = c.indexOf(';', i);
  return c.substring(i + 3, end < 0 ? c.length() : end);
}

bool sameSecret(const String& a, const String& b) {   // constant time for equal lengths
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (unsigned i = 0; i < a.length(); ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

bool requestAuthorized() {
  if (recoveryMode) return true;
  String t = requestToken();
  if (t.length() != 32) return false;
  for (const String& s : sessions) if (sameSecret(s, t)) return true;
  return false;
}

// Guard for handlers: answers 401 and returns false when not signed in.
bool requireAuth() {
  if (requestAuthorized()) return true;
  server.send(401, "text/plain", "Sign in first");
  return false;
}

bool verifyDevicePassword(const String& candidate) {
  if (millis() < loginLockedUntil) {
    server.send(429, "text/plain", "Too many attempts; wait a minute");
    return false;
  }
  if (!sameSecret(candidate, devicePassword)) {
    delay(800);   // slows guessing; with the lockout, ~5 tries a minute
    if (++loginFails >= 5) { loginFails = 0; loginLockedUntil = millis() + 60000; }
    server.send(403, "text/plain", "Wrong password");
    return false;
  }
  loginFails = 0;
  return true;
}

void handleLogin() {
  if (!verifyDevicePassword(server.arg("password"))) return;
  endReveal();   // signed in: the password has done its job, take it off the clock
  char token[33];
  for (int i = 0; i < 4; ++i) snprintf(token + i * 8, 9, "%08x", (unsigned)ESP.random());
  static int next = 0;
  sessions[next] = token;
  next = (next + 1) % SESSIONS;
  server.sendHeader("Set-Cookie", String("sc=") + token + "; Path=/; HttpOnly; SameSite=Strict");
  server.send(200, "text/plain", "OK");
}

void handleLogout() {
  String t = requestToken();
  for (String& s : sessions) if (t.length() && sameSecret(s, t)) s = "";
  server.sendHeader("Set-Cookie", "sc=; Path=/; Max-Age=0; SameSite=Strict");
  server.send(200, "text/plain", "OK");
}

void restartAP();
void handlePasswordChange() {
  if (recoveryMode) {
    server.send(503, "text/plain", "Password changes are unavailable in recovery");
    return;
  }
  if (!requireAuth() || !verifyDevicePassword(server.arg("current"))) return;
  String next = server.arg("password");
  if (!validPassword(next)) {
    server.send(400, "text/plain", "Use 8 to 31 printable ASCII characters, no spaces");
    return;
  }
  if (!sameSecret(next, server.arg("confirmation"))) {
    server.send(400, "text/plain", "The new passwords do not match");
    return;
  }
  if (!storePassword(next)) {
    server.send(500, "text/plain", "Could not save the password");
    return;
  }
  devicePassword = std::move(next);
  String keep = requestToken();
  for (String& session : sessions) if (!sameSecret(session, keep)) session = "";
  endReveal();
  server.send(200, "text/plain", "OK");
  restartAP();
}

void drawPasswordScreen();
// "Show on clock": puts the device password on the panel for 60 s (once per 10 s).
void handleReveal() {
  if (!displayReady) { server.send(503, "text/plain", "The screen is not available"); return; }
  if (revealAt && millis() - revealAt < 10000) { server.send(429, "text/plain", "Already shown"); return; }
  revealAt = millis();
  revealUntil = millis() + 60000;
  drawPasswordScreen();
  server.send(200, "text/plain", "OK");
}

bool settingsWritable() {
  if (storageReady && !recoveryMode) return true;
  server.send(503, "text/plain", "Settings unavailable in recovery or while starting");
  return false;
}

void handleConnect() {
  if (!requireAuth() || !settingsWritable()) return;
  Config next = cfg;
  next.ssid = server.arg("ssid");
  next.password = server.arg("password");
  if (next.ssid.isEmpty() || next.ssid.length() > 32 || next.password.length() > 64 ||
      strlen(next.ssid.c_str()) != next.ssid.length() ||
      strlen(next.password.c_str()) != next.password.length()) {
    server.send(400, "text/plain", "Invalid network name or password length");
    return;
  }
  if ((next.ssid != cfg.ssid || next.password != cfg.password) && !next.save()) {
    server.send(503, "text/plain", "WiFi settings could not be saved");
    return;
  }
  server.send(200, "text/plain", "Saved. Restarting...");
  safeRestart();
}

void handleBrightness() {
  if (!requireAuth() || !settingsWritable()) return;
  const String value = server.arg("value");
  int percent = 0;
  if (value.length() && value.length() <= 3) {
    char* end;
    percent = strtol(value.c_str(), &end, 10);
    if (*end) percent = 0;
  }
  if (percent < 1 || percent > 100) {
    server.send(400, "text/plain", "Brightness must be 1-100");
    return;
  }
  const bool preview = server.arg("preview") == "1";
  if (!preview && percent != cfg.brightness) {
    Config next = cfg;
    next.brightness = percent;
    if (!next.save()) {
      setBacklight(cfg.brightness);
      server.send(503, "text/plain", "Brightness could not be saved");
      return;
    }
    cfg.brightness = percent;
  }
  setBacklight(percent);
  server.send(200, "text/plain", "OK");
}

void handleScanWifi() {
  if (!requireAuth() || !settingsWritable()) return;
  const int count = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray names = doc.to<JsonArray>();
  for (int i = 0; i < count && names.size() < 24; ++i) {
    const String name = WiFi.SSID(i);
    if (!name.isEmpty()) names.add(name);
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// The page is stored gzipped in flash; every browser accepts gzip.
void sendIndex() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", reinterpret_cast<PGM_P>(INDEX_HTML_GZ), INDEX_HTML_GZ_LEN);
}

void handleVersion() { server.send(200, "text/plain", "SmartClock-OSS V" FW_VERSION); }

// Firmware-only OTA. Stage bytes in free sketch space; never write U_FS.
FirmwareValidation firmware;
bool otaStarted = false, otaReady = false, otaFailed = false;
unsigned otaFiles = 0;
String otaError;

void failOta(const char* message) {
  otaFailed = true;
  otaReady = false;
  otaError = message;
  // A byte is always withheld, so end(false) cannot commit a rejected image.
  if (Update.isRunning()) Update.end(false);
  otaStarted = false;
}

void handleOtaUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (!requestAuthorized()) { failOta("Sign in first"); return; }
    if (++otaFiles != 1 || otaFailed) { failOta("Upload exactly one firmware image"); return; }
    firmware.reset();
    otaError = "";
  } else if (up.status == UPLOAD_FILE_WRITE && !otaFailed) {
    const uint8_t* data = up.buf;
    size_t size = up.currentSize;
    if (!otaStarted) {
      size_t take = std::min(size, FirmwareValidation::prefixSize - firmware.received);
      firmware.consume(data, take);
      data += take; size -= take;
      if (firmware.received < FirmwareValidation::prefixSize) return;
      uint32_t freeSpace = ESP.getFreeSketchSpace();
      size_t capacity = freeSpace > 0x1000 ? (freeSpace - 0x1000) & 0xfffff000 : 0;
      if (!firmware.headerValid(capacity)) { failOta("Unsupported image or invalid image length"); return; }
      if (!Update.begin(firmware.expectedSize, U_FLASH)) { failOta("Cannot stage firmware"); return; }
      otaStarted = true;
      if (Update.write(firmware.prefix, FirmwareValidation::prefixSize - 1) != FirmwareValidation::prefixSize - 1) {
        failOta("Firmware staging failed"); return;
      }
    }
    if (size) {
      if (size > firmware.expectedSize - firmware.received) { failOta("Image exceeds declared length"); return; }
      uint8_t previous = firmware.lastByte;
      firmware.consume(data, size);
      if (Update.write(&previous, 1) != 1 ||
          (size > 1 && Update.write(const_cast<uint8_t*>(data), size - 1) != size - 1)) {
        failOta("Firmware staging failed"); return;
      }
    }
  } else if (up.status == UPLOAD_FILE_END && !otaFailed) {
    if (!otaStarted || !firmware.complete()) { failOta("Incomplete image or CRC mismatch"); return; }
    otaReady = true; // Commit only after the complete HTTP request has been parsed.
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    failOta("Upload aborted");
    // The parser may not call the response handler for a disconnected client.
    otaFiles = 0;
    otaFailed = false;
  }
  yield();
}

void finishOta() {
  bool success = otaFiles == 1 && otaReady && !otaFailed;
  if (success) {
    success = Update.write(&firmware.lastByte, 1) == 1 && Update.end(false);
    if (!success) failOta("Firmware commit failed");
  }
  server.sendHeader("Connection", "close");
  server.send(success ? 200 : 400, "text/plain",
              success ? "OK" : (otaError.length() ? otaError : String("No complete firmware uploaded")));
  if (success) safeRestart();
  otaStarted = otaReady = otaFailed = false;
  otaFiles = 0;
  otaError = "";
}

// ---------------------------- WiFi ------------------------------------------
// The router's (or SDK's) reason for the last disconnect during a try, e.g. 15 = the
// 4-way handshake timed out. 0 = none. The settings page explains the codes.
volatile int staReason = 0;
int staFailReason = 0;
WiFiEventHandler staDisconnected;

void startAP() {
  mode = MODE_AP;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false); // serviceSta() owns retries (see below).
  WiFi.mode(WIFI_AP_STA); // Keep recovery accessible even when the router disappears.
  WiFi.hostname(deviceHost());   // DHCP hostname: lets the router name the clock
  staDisconnected = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) { staReason = e.reason; });
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  restartAP();
  dns.start(53, "*", WiFi.softAPIP());
}

// WPA2 with the device password; open in recovery, so recovery is always reachable.
void restartAP() {
  if (recoveryMode) WiFi.softAP("Smart Weather Clock");
  else WiFi.softAP("Smart Weather Clock", devicePassword.c_str());
}

// While the STA side searches for a missing router the ESP8266 hops channels and AP
// clients drop. Search in short windows so the setup/recovery AP stays usable.
String staSsid, staPass;
bool staTrying = false, staRetryNow = true;
uint32_t staTimer = 0;
int staFail = -1, staFailShown = -1;   // WiFi.status() when the last try gave up; -1 = none
constexpr uint32_t STA_TRY_MS = 30000, STA_RETRY_MS = 5UL * 60 * 1000;

void staConfigure(const String& ssid, const String& pass) {
  staSsid = ssid; staPass = pass;
  staTrying = false; staRetryNow = true; staFail = -1;
}

// Why the last try failed, for the setup screen and the settings page.
String staFailText() {
  switch (staFail) {
    case -1:                return "";
    case WL_NO_SSID_AVAIL:  return F("network not found");
    case WL_WRONG_PASSWORD: return F("wrong password");
    case WL_CONNECT_FAILED: return F("connection refused");
    default:                return F("no answer");
  }
}

void serviceSta() {
  if (staSsid.isEmpty()) return;
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    staTrying = false;
    staRetryNow = true;              // a dropped link retries immediately
    staFail = -1;
  } else if (staTrying) {
    if (now - staTimer >= STA_TRY_MS) {
      staFail = WiFi.status(); staFailReason = staReason;
      WiFi.disconnect(false); staTrying = false; staTimer = now;
    }
  } else if (staRetryNow || now - staTimer >= STA_RETRY_MS) {
    staRetryNow = false;
    staReason = 0;
    WiFi.begin(staSsid.c_str(), staPass.c_str());
    staTrying = true; staTimer = now;
  }
}

void drawKey(int y) {
  centerText("Key: " + devicePassword.substring(0, 16), y, 2, TFT_YELLOW);
  if (devicePassword.length() > 16) centerText(devicePassword.substring(16), y + 17, 2, TFT_YELLOW);
}

void drawBootScreen(bool redraw) {
  static uint32_t previousSecond = UINT32_MAX;
  const uint32_t elapsed = (millis() - bootStarted) / 1000;
  if (!redraw && elapsed == previousSecond) return;

  TFT_eSPI& panel = clockDisplay();
  if (redraw) {
    panel.fillScreen(TFT_BLACK);
    centerText("BOOT OK", 44, 4, TFT_GREEN);
    centerText("UPTIME", 76, 2, TFT_WHITE);
    if (mode == MODE_RUN) {
      centerText("WiFi connected", 155, 2, TFT_GREEN);
      centerText(WiFi.localIP().toString(), 187, 4, TFT_YELLOW);
    } else {
      centerText(staFail < 0 ? "Join setup WiFi" : "Home WiFi unavailable", 145, 2, TFT_WHITE);
      centerText("Smart Weather Clock", 164, 2, TFT_CYAN);
      drawKey(184);
      centerText(WiFi.softAPIP().toString(), 225, 2, TFT_WHITE);
    }
    staFailShown = staFail;
  }

  char uptime[16];
  snprintf(uptime, sizeof(uptime), "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(elapsed / 3600),
           static_cast<unsigned long>(elapsed / 60 % 60),
           static_cast<unsigned long>(elapsed % 60));
  panel.setTextPadding(TFT_W - 20);
  centerText(uptime, 104, 4, TFT_WHITE);
  panel.setTextPadding(0);
  previousSecond = elapsed;
}

void drawPasswordScreen() {
  clockDisplay().fillScreen(TFT_BLACK);
  centerText("Device password", 70, 4, TFT_WHITE);
  drawKey(122);
  centerText("Settings and setup WiFi", 172, 2, TFT_CYAN);
}

void drawRecoveryScreen() {
  clockDisplay().fillScreen(TFT_BLACK);
  centerText("Recovery", 53, 4, TFT_ORANGE);
  centerText("Smart Weather Clock", 104, 2, TFT_CYAN);
  centerText(WiFi.softAPIP().toString(), 145, 4, TFT_YELLOW);
  if (storageMountFailed) centerText("Storage not mounted", 199, 2, TFT_RED);
}

void startOptionalServices() {
  optionalStarted = true;
  markBoot(BOOT_STARTING);
  LittleFS.setConfig(LittleFSConfig(false));
  storageReady = LittleFS.begin();
  if (!storageReady) {
    storageMountFailed = recoveryMode = true;
    restartAP();
    markBoot(BOOT_CLEAN);
    return;
  }
  cleanup.begin(cfg.load());
  displayBegin();
  staConfigure(cfg.ssid, cfg.password);
  drawBootScreen(true);
}

void handleStatus() {
  JsonDocument doc;
  const bool signedIn = requestAuthorized();
  const bool linked = WiFi.status() == WL_CONNECTED;
  doc["version"] = FW_VERSION;
  doc["build"] = FW_BUILD;
  doc["uptime"] = (millis() - bootStarted) / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["heapLow"] = heapLow;
  doc["signedIn"] = signedIn;
  doc["storageReady"] = storageReady;
  doc["cleanup"] = cleanup.status;
  doc["cleanupRemoved"] = cleanup.removed;
  doc["cleanupSkipped"] = cleanup.skipped;
  FSInfo space;
  if (storageReady && LittleFS.info(space) && space.usedBytes <= space.totalBytes) {
    doc["storageTotalBytes"] = space.totalBytes;
    doc["storageFreeBytes"] = space.totalBytes - space.usedBytes;
  }
  doc["recovery"] = recoveryMode;
  doc["connected"] = linked;
  doc["apIp"] = WiFi.softAPIP().toString();
  if (linked) doc["ip"] = WiFi.localIP().toString();
  if (signedIn) {
    doc["ssid"] = cfg.ssid;
    doc["brightness"] = cfg.brightness;
    if (!linked && staFail >= 0) {
      doc["wifiError"] = staFailText();
      doc["wifiCode"] = staFailReason;
    }
  }
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

// Stream directory metadata in small batches. Leave space for the network
// stack even if the volume has many files or deeply nested directories.
bool streamFileEntries(const String& directory, unsigned depth, String& chunk, bool& first) {
  if (depth > 8) return false;
  Dir entries = LittleFS.openDir(directory);
  if (!entries.rewind()) return false;
  bool complete = true;
  while (server.client().connected() && entries.next()) {
    yield();
    const String name = entries.fileName();
    String path;
    const size_t length = directory.length() + name.length() + (directory == "/" ? 0 : 1);
    if (name.isEmpty() || length > 1024 || !path.reserve(length)) {
      complete = false;
      continue;
    }
    path = directory;
    if (directory != "/") path += '/';
    path += name;
    if (entries.isDirectory()) {
      if (!streamFileEntries(path, depth + 1, chunk, first)) complete = false;
    } else if (entries.isFile()) {
      JsonDocument row;
      row["path"] = path;
      row["bytes"] = entries.fileSize();
      const size_t rowBytes = measureJson(row);
      String json;
      if (row.overflowed() || !json.reserve(rowBytes)) return false;
      serializeJson(row, json);
      if (json.length() != rowBytes || !chunk.reserve(chunk.length() + rowBytes + 1)) return false;
      if (!first) chunk += ',';
      chunk += json;
      first = false;
      if (chunk.length() >= 768) {
        server.sendContent(chunk);
        chunk.remove(0);
      }
    }
  }
  return complete;
}

void handleStorageFiles() {
  server.sendHeader("Cache-Control", "no-store");
  // Recovery allows OTA without a session. File paths still require normal login.
  if (recoveryMode || !storageReady) {
    server.send(503, "text/plain", "File storage is unavailable");
    return;
  }
  if (!requireAuth()) return;
  if (!server.chunkedResponseModeStart(200, "application/json")) {
    server.send(505, "text/plain", "HTTP/1.1 is required");
    return;
  }
  server.sendContent_P(PSTR("{\"files\":["));
  String chunk;
  bool first = true;
  const bool complete = streamFileEntries("/", 0, chunk, first);
  if (chunk.length()) server.sendContent(chunk);
  server.sendContent_P(complete ? PSTR("],\"complete\":true}") : PSTR("],\"complete\":false}"));
  server.chunkedResponseFinalize();
}

void setupRoutes() {
  server.on("/", HTTP_GET, sendIndex);
  server.on("/generate_204", HTTP_GET, sendIndex);
  server.on("/hotspot-detect.html", HTTP_GET, sendIndex);
  server.on("/connecttest.txt", HTTP_GET, sendIndex);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/api/storage/files", HTTP_GET, handleStorageFiles);
  server.on("/version", HTTP_GET, handleVersion);
  server.on("/scanwifi", HTTP_GET, handleScanWifi);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/brightness", HTTP_POST, handleBrightness);
  server.on("/api/auth/login", HTTP_POST, handleLogin);
  server.on("/api/auth/logout", HTTP_POST, handleLogout);
  server.on("/api/auth/password", HTTP_POST, handlePasswordChange);
  server.on("/api/auth/reveal", HTTP_POST, handleReveal);
  server.on("/update_ota", HTTP_POST, finishOta, handleOtaUpload);
  server.collectHeaders("Cookie");
  server.onNotFound([]{ server.send(404, "text/plain", "Not found"); });
}

// Recovery: AP + OTA + optional build-time WiFi only. The panel is shown so the
// device does not look dead, unless the panel itself reset the previous boot.
void recoveryLoop() {
  serviceSta();
  if (!displayReady && recoveryDisplay && millis() - bootStarted >= 3000) {
    markBoot(BOOT_RECOVERY_DISPLAY);
    displayBegin();
    drawRecoveryScreen();
    markBoot(BOOT_CLEAN);
    recoveryLinkShown = WiFi.status() == WL_CONNECTED;
  }
  if (revealUntil && (int32_t)(millis() - revealUntil) >= 0) { revealUntil = 0; if (displayReady) drawRecoveryScreen(); }
  if (displayReady && recoveryLinkShown != (WiFi.status() == WL_CONNECTED)) {
    recoveryLinkShown = !recoveryLinkShown;
    drawRecoveryScreen();
  }
  delay(1);
}

void setup() {
  // No Serial.begin(): nothing is printed, and leaving it out drops the UART driver.
  BootMarker marker{};
  BootState previous = BOOT_CLEAN;
  if (ESP.rtcUserMemoryRead(32, reinterpret_cast<uint32_t*>(&marker), sizeof(marker)) &&
      marker.magic == BOOT_MAGIC &&
      (marker.state == BOOT_STARTING || marker.state == BOOT_RECOVERY_DISPLAY))
    previous = BootState(marker.state);
  markBoot(BOOT_CLEAN);
  // Only a power-on or external reset is part of the power-cycle gesture. Crashes,
  // watchdog resets and our own restarts clear the count; the RTC marker above is
  // what catches a crash during startup.
  const rst_info* reset = ESP.getResetInfoPtr();
  bool powerOn = reset && (reset->reason == REASON_DEFAULT_RST || reset->reason == REASON_EXT_SYS_RST);
  bool powerCycled = false;
  if (powerOn) powerCycled = countPowerCycle();
  else clearPowerCycles();
  recoveryMode = powerCycled || previous != BOOT_CLEAN;
  recoveryDisplay = previous != BOOT_RECOVERY_DISPLAY;
  loadPassword();   // before startAP(): it is the setup network's WPA2 key
  startAP();
  setupRoutes();
  server.begin();
  bootStarted = millis();
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < heapLow) heapLow = freeHeap;
  if (otaFiles || otaStarted) { delay(1); return; }

  if (!powerCyclesCleared && millis() - bootStarted >= CYCLE_WINDOW_MS) {
    clearPowerCycles();
    powerCyclesCleared = true;
  }
  if (recoveryMode) { recoveryLoop(); return; }
  if (!optionalStarted) {
    if (millis() - bootStarted >= 3000) startOptionalServices();
    delay(1);
    return;
  }
  if (!bootHealthy && millis() - bootStarted >= 30000) {
    markBoot(BOOT_CLEAN);
    bootHealthy = true;
  }

  serviceSta();
  cleanup.step();
  const Mode currentMode = WiFi.status() == WL_CONNECTED ? MODE_RUN : MODE_AP;
  const bool linkChanged = mode != currentMode;
  mode = currentMode;
  bool redraw = linkChanged || (mode == MODE_AP && staFail != staFailShown);
  if (revealUntil && (int32_t)(millis() - revealUntil) >= 0) {
    revealUntil = 0;
    redraw = true;
  }
  if (!revealUntil) drawBootScreen(redraw);
  delay(20);
}
