#pragma once

#include <LittleFS.h>

struct LegacyAsset {
  const char* path;
  size_t bytes;
};

// Paths and sizes from this device's read-only inventory. Folders and any
// other files are outside this cleanup.
constexpr LegacyAsset legacyAssets[] = {
  {"/0.gif", 27141},
  {"/1.gif", 90718},
  {"/2.gif", 81351},
  {"/3.gif", 28650},
  {"/4.gif", 59468},
  {"/clock.jpg", 10524},
  {"/code.jpg", 91523},
  {"/FlipClock/0.gif", 21811},
  {"/FlipClock/1.gif", 22350},
  {"/FlipClock/2.gif", 19454},
  {"/FlipClock/3.gif", 19304},
  {"/FlipClock/4.gif", 22372},
  {"/FlipClock/5.gif", 18449},
  {"/FlipClock/6.gif", 18167},
  {"/FlipClock/7.gif", 18505},
  {"/FlipClock/8.gif", 20088},
  {"/FlipClock/9.gif", 21571},
  {"/ico/01d.jpg", 5957},
  {"/ico/01n.jpg", 3399},
  {"/ico/02d.jpg", 6076},
  {"/ico/02n.jpg", 5516},
  {"/ico/03d.jpg", 5523},
  {"/ico/04d.jpg", 4599},
  {"/ico/09d.jpg", 6774},
  {"/ico/10d.jpg", 6472},
  {"/ico/11d.jpg", 6530},
  {"/ico/13d.jpg", 6568},
  {"/ico/50d.jpg", 4947},
  {"/ico/humidity.jpg", 1472},
  {"/ico/temperature.jpg", 1216},
  {"/ico2/01d.jpg", 3855},
  {"/ico2/01n.jpg", 2212},
  {"/ico2/02d.jpg", 4051},
  {"/ico2/02n.jpg", 3398},
  {"/ico2/03d.jpg", 3651},
  {"/ico2/04d.jpg", 3067},
  {"/ico2/09d.jpg", 4286},
  {"/ico2/10d.jpg", 4293},
  {"/ico2/11d.jpg", 3990},
  {"/ico2/13d.jpg", 4188},
  {"/ico2/50d.jpg", 3298},
  {"/index.html", 10794},
  {"/indexkey.html", 1822},
  {"/indexwifi.html", 3083},
  {"/javascript.js", 23595},
  {"/micross20.vlw", 57430},
  {"/NotoSans16.vlw", 44667},
  {"/NotoSansKR18.vlw", 138849},
  {"/photo-setting.html", 14996},
  {"/style.css", 5985},
  {"/Univers120.vlw", 46447},
  {"/Univers40.vlw", 6259},
  {"/Univers80.vlw", 22707},
  {"/weather/01d.jpg", 8277},
  {"/weather/01n.jpg", 16211},
  {"/weather/02d.jpg", 9072},
  {"/weather/02n.jpg", 10263},
  {"/weather/03d.jpg", 9374},
  {"/weather/04d.jpg", 7421},
  {"/weather/09d.jpg", 10794},
  {"/weather/10d.jpg", 9942},
  {"/weather/11d.jpg", 10183},
  {"/weather/13d.jpg", 10930},
  {"/weather/50d.jpg", 7688},
};

constexpr size_t legacyAssetCount = sizeof(legacyAssets) / sizeof(legacyAssets[0]);
static_assert(legacyAssetCount == 64, "Expected the 64 reviewed files");

struct LegacyCleanup {
  static constexpr const char* markerPath = "/.sdpro-clean64.done";
  const char* status = "waiting";
  size_t checked = 0, removed = 0, skipped = 0, failed = 0;
  bool running = false;

  void begin(bool configVerified) {
    if (LittleFS.exists(markerPath)) {
      status = "complete";
    } else if (configVerified) {
      status = "running";
      running = true;
    } else {
      status = "blocked";
    }
  }

  void step() {
    if (!running) return;
    if (checked == legacyAssetCount) {
      running = false;
      // After a successful pass, later boots cannot touch newly added files.
      if (!failed) {
        File marker = LittleFS.open(markerPath, "w");
        if (marker) marker.close();
      }
      status = !failed && LittleFS.exists(markerPath) ? "complete" : "retry";
      return;
    }

    const LegacyAsset& asset = legacyAssets[checked++];
    if (!LittleFS.exists(asset.path)) return;
    File file = LittleFS.open(asset.path, "r");
    if (!file) { ++failed; return; }
    const bool match = file.isFile() && file.size() == asset.bytes;
    file.close();
    if (!match) { ++skipped; return; }
    if (LittleFS.remove(asset.path)) ++removed;
    else ++failed;
  }
};
