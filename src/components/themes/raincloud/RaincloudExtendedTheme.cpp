#include "RaincloudExtendedTheme.h"

#include <GfxRenderer.h>

#include "RecentBooksStore.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"

void RaincloudExtendedTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                                 const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                                 bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                                 std::function<bool()> storeCoverBuffer) const {
  const Lyra3CoversTheme coverTheme;
  coverTheme.drawRecentBookCover(renderer, rect, recentBooks, selectorIndex, coverRendered, coverBufferStored,
                                 bufferRestored, std::move(storeCoverBuffer));
}
