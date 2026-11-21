#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Lyra with a home-screen background: the current book's cover, otherwise a
// random image from the sleep folder, drawn in grayscale behind the home screen.
// The decoded background is cached in PSRAM so redraws skip the SD card.
class RaincloudTheme : public LyraTheme {
 public:
  bool isHomeHeaderRect(const GfxRenderer& renderer, Rect rect) const override;
  void drawHomeBackground(GfxRenderer& renderer, const std::vector<RecentBook>& recentBooks) const override;
  void finishHomeScreen(GfxRenderer& renderer) const override;
  void fillSelectionBackground(const GfxRenderer& renderer, int x, int y, int width, int height) const override;

 private:
  struct PsramFree {
    void operator()(uint8_t* ptr) const;
  };

  mutable std::string backgroundBookPath;
  mutable std::string backgroundCoverPath;
  mutable std::string backgroundThumbPath;
  mutable std::string randomBackgroundPath;
  mutable bool randomBackgroundPicked = false;
  // 2 bits per screen pixel (background level 0..3).
  mutable std::unique_ptr<uint8_t[], PsramFree> backgroundCache;
  mutable size_t backgroundCacheCapacity = 0;
  mutable std::string cachedPath;
  mutable int cachedWidth = 0;
  mutable int cachedHeight = 0;
  mutable int cachedOrientation = -1;
  mutable uint8_t cachedCoverMode = 0xFF;  // sleep screen Cover Mode the cache was drawn with
  mutable bool cachedHasGray = false;      // the cached picture has gray levels
  // What the last home render drew: a cached picture with gray levels can get
  // the sleep screen's gray pass once the home screen is idle.
  mutable bool drawnFromGrayCache = false;
  mutable bool psramUnavailableLogged = false;
  // Usable random backgrounds from the last folder scan: the folder and the
  // file names, each followed by '\0'. Scanned once per boot and again only
  // when the SD content epoch moves (USB, file transfer) or a listed file fails.
  mutable std::string backgroundIndexDir;
  mutable std::string backgroundIndexNames;
  mutable uint16_t backgroundIndexCount = 0;
  mutable bool backgroundIndexValid = false;
  mutable uint32_t backgroundIndexEpoch = 0;

  // Home outline: drawHomeBackground keeps a copy of the drawn background (raw
  // framebuffer bytes) and clears the screen; finishHomeScreen lays it back in
  // only where it stays clear of the home UI. Both buffers live in PSRAM and are
  // reused (outlineMask is the 3-plane scratch of the compositor); without them
  // the background is simply drawn under the UI.
  mutable std::unique_ptr<uint8_t[], PsramFree> outlineBackground;
  mutable std::unique_ptr<uint8_t[], PsramFree> outlineMask;
  mutable size_t outlineBackgroundSize = 0;
  mutable size_t outlineMaskSize = 0;
  mutable bool outlinePending = false;

  void pickIndexedBackground(std::string& selectedPath) const;
  void drawBackgroundLayer(GfxRenderer& renderer, const std::vector<RecentBook>& recentBooks) const;
};
