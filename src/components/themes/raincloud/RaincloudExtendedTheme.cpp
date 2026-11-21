#include "RaincloudExtendedTheme.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "RecentBooksStore.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"

void RaincloudExtendedTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                                 const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                                 bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                                 std::function<bool()> storeCoverBuffer, int progress) const {
  const Lyra3CoversTheme coverTheme;
  coverTheme.drawRecentBookCover(renderer, rect, recentBooks, selectorIndex, coverRendered, coverBufferStored,
                                 bufferRestored, std::move(storeCoverBuffer), progress);

  // The selected tile's title sits in the gray box under its cover (layout of
  // Lyra3CoversTheme::drawRecentBookCover, whose selection padding is 8 px).
  constexpr int hPaddingInSelection = 8;
  const int tileWidth = (rect.width - 2 * Lyra3CoversMetrics::values.contentSidePadding) / 3;

  // Reading progress of the first book: a thin bar along the bottom of its cover, in a white frame so it
  // reads on any cover. Drawn after the cover snapshot, so the stored cover buffer never contains it.
  if (progress >= 0 && !recentBooks.empty()) {
    constexpr int barHeight = 4;
    constexpr int inset = 6;
    constexpr int frame = 2;
    const int coverX = Lyra3CoversMetrics::values.contentSidePadding + hPaddingInSelection;
    const int coverWidth = tileWidth - 2 * hPaddingInSelection;
    const int barX = coverX + inset;
    const int barWidth = coverWidth - 2 * inset;
    const int barY = rect.y + hPaddingInSelection + Lyra3CoversMetrics::values.homeCoverHeight - inset - barHeight;
    if (barWidth > 2) {
      renderer.fillRect(barX - frame, barY - frame, barWidth + 2 * frame, barHeight + 2 * frame, false);
      renderer.drawRect(barX, barY, barWidth, barHeight);
      renderer.fillRect(barX + 1, barY + 1, (barWidth - 2) * progress / 100, barHeight - 2);
    }
  }

  const int shown = std::min(static_cast<int>(recentBooks.size()), Lyra3CoversMetrics::values.homeRecentBooksCount);
  if (selectorIndex < 0 || selectorIndex >= shown) return;
  const int tileX = Lyra3CoversMetrics::values.contentSidePadding + tileWidth * selectorIndex;
  const int titleY = rect.y + Lyra3CoversMetrics::values.homeCoverHeight + hPaddingInSelection;
  addSelectionTextRect(Rect{tileX, titleY, tileWidth, rect.y + rect.height - titleY});
}
