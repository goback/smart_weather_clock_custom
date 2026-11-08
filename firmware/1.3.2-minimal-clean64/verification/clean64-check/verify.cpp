
#include "legacy_assets.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <utility>
using String = std::string;
constexpr size_t PASS_MIN=8, PASS_MAX=31;
bool recoveryMode=false, authorized=false, allowWrite=true;
uint8_t loginFails=0;
uint32_t loginLockedUntil=0, now=1;
int writes=0, apRestarts=0;
String devicePassword="old-password", stored=devicePassword;
String sessions[4]={"current-token","other-token"};
uint32_t millis() { return now; }
void delay(unsigned) {}
void restartAP() { ++apRestarts; }
void endReveal() {}
String requestToken() { return sessions[0]; }
bool storePassword(const String& p) { ++writes; if (allowWrite) stored=p; return allowWrite; }
struct {
  std::map<String,String> args{{"current","old-password"},{"password","new-Password!1"},{"confirmation","new-Password!1"}};
  int code=0;
  String arg(const char* key) { return args[key]; }
  void send(int status, const char*, const char*) { code=status; }
} server;
bool requireAuth() { if (authorized) return true; server.send(401,"text/plain","sign in"); return false; }
bool validPassword(const String& p) {
  if (p.length() < PASS_MIN || p.length() > PASS_MAX) return false;
  for (unsigned i = 0; i < p.length(); ++i) if (p[i] < 0x21 || p[i] > 0x7e) return false;   // printable, no spaces
  return true;
}

bool sameSecret(const String& a, const String& b) {   // constant time for equal lengths
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (unsigned i = 0; i < a.length(); ++i) diff |= a[i] ^ b[i];
  return diff == 0;
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

void finish(LegacyCleanup& c) { for (size_t i=0; i<=legacyAssetCount; ++i) c.step(); }
void seed() {
  LittleFS.files.clear();
  for (const auto& asset : legacyAssets) LittleFS.files[asset.path]={asset.bytes,true};
  LittleFS.files["/sdpro-minimal.json"]={68,true};
  LittleFS.files["/weather/custom.jpg"]={4096,true};
  LittleFS.files["/weather"]={0,false};
}
int main() {
  seed();
  LegacyCleanup blocked; blocked.begin(false); finish(blocked);
  assert(std::strcmp(blocked.status,"blocked")==0 && LittleFS.files.size()==67);
  LegacyCleanup pass; pass.begin(true); finish(pass);
  assert(pass.removed==64 && LittleFS.files.size()==4);
  assert(LittleFS.exists("/sdpro-minimal.json") && LittleFS.exists("/weather/custom.jpg") && LittleFS.exists("/weather"));
  LittleFS.files[legacyAssets[0].path]={legacyAssets[0].bytes,true};
  LegacyCleanup later; later.begin(true); finish(later);
  assert(LittleFS.exists(legacyAssets[0].path) && later.removed==0);
  seed();
  LittleFS.files[legacyAssets[0].path].size++;
  LittleFS.files[legacyAssets[1].path].regular=false;
  LittleFS.failRemove=legacyAssets[2].path;
  LegacyCleanup interrupted; interrupted.begin(true);
  for (int i=0; i<10; ++i) interrupted.step();
  LegacyCleanup retry; retry.begin(true); finish(retry);
  assert(retry.skipped==2 && retry.failed==1 && std::strcmp(retry.status,"retry")==0);
  LittleFS.failRemove.clear();
  LegacyCleanup resumed; resumed.begin(true); finish(resumed);
  assert(resumed.removed==1 && resumed.skipped==2 && LittleFS.exists(LegacyCleanup::markerPath));
  assert(LittleFS.exists(legacyAssets[0].path) && LittleFS.exists(legacyAssets[1].path));
  std::cout << "PASS: 64 listed files, settings gate, changed paths, extra files, reboot retry and completion marker\n";
  handlePasswordChange(); assert(server.code==401 && writes==0);
  authorized=true; recoveryMode=true;
  handlePasswordChange(); assert(server.code==503 && writes==0);
  recoveryMode=false; server.args["current"]="wrong";
  for (int i=0; i<5; ++i) { handlePasswordChange(); assert(server.code==403); }
  server.args["current"]="old-password";
  handlePasswordChange(); assert(server.code==429 && writes==0);
  now=61000; server.args["password"]="bad pass";
  handlePasswordChange(); assert(server.code==400 && writes==0);
  server.args["password"]="new-Password!2";
  handlePasswordChange(); assert(server.code==400 && writes==0);
  server.args["confirmation"]="new-Password!2"; allowWrite=false;
  handlePasswordChange(); assert(server.code==500 && apRestarts==0 && devicePassword=="old-password");
  allowWrite=true;
  handlePasswordChange();
  assert(server.code==200 && devicePassword=="new-Password!2" && stored==devicePassword && apRestarts==1);
  assert(sessions[0]=="current-token" && sessions[1].empty());
  std::cout << "PASS: authentication, current password, lockout, new password validation, save failure and success\n";
}
