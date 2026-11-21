#include "RaincloudExtendedTheme.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "RecentBooksStore.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"

void RaincloudExtendedTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                                 const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                                 bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                                 std::function<bool()> storeCoverBuffer) const {
  const Lyra3CoversTheme coverTheme;
  coverTheme.drawRecentBookCover(renderer, rect, recentBooks, selectorIndex, coverRendered, coverBufferStored,
                                 bufferRestored, std::move(storeCoverBuffer));

  // The selected tile's title sits in the gray box under its cover (layout of
  // Lyra3CoversTheme::drawRecentBookCover, whose selection padding is 8 px).
  constexpr int hPaddingInSelection = 8;
  const int shown = std::min(static_cast<int>(recentBooks.size()), Lyra3CoversMetrics::values.homeRecentBooksCount);
  if (selectorIndex < 0 || selectorIndex >= shown) return;
  const int tileWidth = (rect.width - 2 * Lyra3CoversMetrics::values.contentSidePadding) / 3;
  const int tileX = Lyra3CoversMetrics::values.contentSidePadding + tileWidth * selectorIndex;
  const int titleY = rect.y + Lyra3CoversMetrics::values.homeCoverHeight + hPaddingInSelection;
  addSelectionTextRect(Rect{tileX, titleY, tileWidth, rect.y + rect.height - titleY});
}
