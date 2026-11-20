#pragma once

#include "components/themes/raincloud/RaincloudTheme.h"

class GfxRenderer;

// Raincloud background with the Lyra 3 Covers home tiles.
class RaincloudExtendedTheme : public RaincloudTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
};
