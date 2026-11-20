#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Lyra with a home-screen background: the current book's cover, otherwise a
// random image from the sleep folder, drawn lightened behind the home screen.
// The decoded background is cached in PSRAM so redraws skip the SD card.
class RaincloudTheme : public LyraTheme {
 public:
  bool isHomeHeaderRect(const GfxRenderer& renderer, Rect rect) const override;
  void drawHomeBackground(GfxRenderer& renderer, const std::vector<RecentBook>& recentBooks) const override;
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
  mutable bool psramUnavailableLogged = false;
};
