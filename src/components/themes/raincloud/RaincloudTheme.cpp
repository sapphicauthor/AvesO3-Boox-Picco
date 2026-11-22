#include "RaincloudTheme.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <BitmapHelpers.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "util/SdContentEpoch.h"

namespace {
constexpr int kBackgroundCoverHeight = 226;
constexpr size_t kMaxBackgroundFileNameLen = 256;

// The cache holds the drawn background, one bit per screen pixel (1 = black).
size_t backgroundCacheBytes(const int width, const int height) { return (static_cast<size_t>(width) * height + 7) / 8; }

bool ditherBackground() {
  return SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
}
bool invertBackground() {
  return SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE;
}
}  // namespace

namespace {
bool isValidBmp(HalFile& file) {
  Bitmap bitmap(file);
  return bitmap.parseHeaders() == BmpReaderError::Ok;
}

bool isUsableBackground(HalFile& file, const char* name) {
  if (name[0] == '\0' || name[0] == '.') return false;
  if (!FsHelpers::hasBmpExtension(name)) return false;
  return isValidBmp(file);
}

// Append the full path of every usable background in dirPath to paths ('\0'-terminated each).
// Returns false only when the filename buffer cannot be allocated.
bool scanBackgroundFolder(const char* dirPath, std::string& paths, uint16_t& count) {
  auto dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return true;

  auto name = makeUniqueNoThrow<char[]>(kMaxBackgroundFileNameLen);
  if (!name) {
    LOG_ERR("THEME", "OOM: sleep image filename buffer");
    return false;
  }

  while (count < UINT16_MAX) {
    auto file = dir.openNextFile();
    if (!file) break;
    file.getName(name.get(), kMaxBackgroundFileNameLen);
    if (!isUsableBackground(file, name.get())) continue;
    paths.append(dirPath);
    paths.push_back('/');
    paths.append(name.get());
    paths.push_back('\0');
    count++;
  }
  return true;
}

// Draw a BMP over the screen following the sleep screen's Cover Mode: Crop fills
// the screen (centre crop), Fit shows the whole picture centred on white. When
// cache is set, the drawn black and white of every screen pixel is stored there
// as well.
bool renderBackgroundBmp(const GfxRenderer& renderer, const std::string& path, uint8_t* cache, const size_t cacheSize) {
  HalFile file;
  if (!Storage.openFileForRead("THEME", path, file)) {
    LOG_DBG("THEME", "Unable to open background image");
    return false;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_DBG("THEME", "Invalid background BMP");
    return false;
  }

  const int bmpWidth = bitmap.getWidth();
  const int bmpHeight = bitmap.getHeight();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  if (bmpWidth <= 0 || bmpHeight <= 0 || screenWidth <= 0 || screenHeight <= 0) return false;

  const size_t cacheBytes = backgroundCacheBytes(screenWidth, screenHeight);
  if (cache && cacheSize < cacheBytes) return false;

  // Source rectangle of the picture mapped onto a destination rectangle.
  float srcX0 = 0.0f;
  float srcY0 = 0.0f;
  float srcWidth = static_cast<float>(bmpWidth);
  float srcHeight = static_cast<float>(bmpHeight);
  int dstX0 = 0;
  int dstY0 = 0;
  int dstWidth = screenWidth;
  int dstHeight = screenHeight;
  if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::FIT) {
    const float scale = std::min(static_cast<float>(screenWidth) / bmpWidth, static_cast<float>(screenHeight) / bmpHeight);
    dstWidth = std::max(1, std::min(screenWidth, static_cast<int>(bmpWidth * scale)));
    dstHeight = std::max(1, std::min(screenHeight, static_cast<int>(bmpHeight * scale)));
    dstX0 = (screenWidth - dstWidth) / 2;
    dstY0 = (screenHeight - dstHeight) / 2;
  } else {
    const float screenAspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    const float bmpAspect = static_cast<float>(bmpWidth) / static_cast<float>(bmpHeight);
    if (screenAspect < bmpAspect) {
      srcWidth = static_cast<float>(bmpHeight) * screenAspect;
      srcX0 = (static_cast<float>(bmpWidth) - srcWidth) * 0.5f;
    } else {
      srcHeight = static_cast<float>(bmpWidth) / screenAspect;
      srcY0 = (static_cast<float>(bmpHeight) - srcHeight) * 0.5f;
    }
  }

  auto lumRow = makeUniqueNoThrow<uint8_t[]>(bmpWidth);
  auto rowBytes = makeUniqueNoThrow<uint8_t[]>(bitmap.getRowBytes());
  // Source column of every screen column (-1 outside the picture): the same for
  // every row, so it is worked out once instead of with a float divide per pixel.
  auto srcColumns = makeUniqueNoThrow<int[]>(screenWidth);
  if (!lumRow || !rowBytes || !srcColumns) {
    LOG_ERR("THEME", "OOM: background BMP row buffers");
    return false;
  }
  for (int x = 0; x < screenWidth; x++) {
    const bool inside = x >= dstX0 && x < dstX0 + dstWidth;
    srcColumns[x] =
        inside ? std::min(static_cast<int>((x - dstX0 + 0.5f) * srcWidth / dstWidth + srcX0), bmpWidth - 1) : -1;
  }

  // No filter: error diffusion straight from the picture's luminance to black
  // and white, after scaling, as CrossPoint dithers 1-bit cover thumbnails.
  // The Black & White filters use the sleep screen's rule (only white stays white).
  const bool dither = ditherBackground();
  const bool invert = invertBackground();
  std::unique_ptr<Atkinson1BitDitherer> ditherer;
  if (dither) {
    ditherer = makeUniqueNoThrow<Atkinson1BitDitherer>(screenWidth);
    if (!ditherer || !ditherer->isValid()) {
      LOG_ERR("THEME", "OOM: background ditherer");
      return false;
    }
  }
  // Inverted Black & White turns the white around a fitted picture black, as
  // the sleep screen's invertScreen() does.
  if (invert) renderer.fillRect(0, 0, screenWidth, screenHeight, true);
  if (cache) memset(cache, invert ? 0xFF : 0x00, cacheBytes);

  const bool topDown = bitmap.isTopDown();
  int screenY = topDown ? dstY0 : dstY0 + dstHeight - 1;
  const int step = topDown ? 1 : -1;

  for (int row = 0; row < bmpHeight; row++) {
    if (bitmap.readNextRowLuminance(lumRow.get(), rowBytes.get()) != BmpReaderError::Ok) {
      LOG_ERR("THEME", "Failed to read background BMP row %d", row);
      return false;
    }
    const int srcRow = topDown ? row : bmpHeight - 1 - row;

    while (screenY >= dstY0 && screenY < dstY0 + dstHeight) {
      const int srcY =
          std::min(static_cast<int>((screenY - dstY0 + 0.5f) * srcHeight / dstHeight + srcY0), bmpHeight - 1);
      if (srcY != srcRow) break;

      for (int x = 0; x < screenWidth; x++) {
        const int srcX = srcColumns[x];
        if (srcX < 0) continue;
        const uint8_t lum = lumRow[srcX];
        const bool black = dither ? ditherer->processPixel(lum, x) == 0 : (quantizeSimple(lum) < 3) != invert;
        renderer.drawPixel(x, screenY, black);
        if (cache) {
          const size_t cacheIndex = static_cast<size_t>(screenY) * screenWidth + x;
          const auto bit = static_cast<uint8_t>(0x80 >> (cacheIndex % 8));
          cache[cacheIndex / 8] = black ? cache[cacheIndex / 8] | bit : cache[cacheIndex / 8] & ~bit;
        }
      }
      if (ditherer) ditherer->nextRow();
      screenY += step;
    }
  }
  return true;
}

bool replayBackgroundCache(const GfxRenderer& renderer, const uint8_t* cache, const size_t cacheSize) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const size_t cacheBytes = backgroundCacheBytes(screenWidth, screenHeight);
  if (screenWidth <= 0 || cacheSize < cacheBytes) return false;

  for (size_t i = 0; i < cacheBytes; i++) {
    const uint8_t bits = cache[i];
    if (bits == 0) continue;
    for (int b = 0; b < 8; b++) {
      if (!(bits & (0x80 >> b))) continue;
      const size_t index = i * 8 + b;
      if (index >= static_cast<size_t>(screenWidth) * screenHeight) break;
      renderer.drawPixel(static_cast<int>(index % screenWidth), static_cast<int>(index / screenWidth), true);
    }
  }
  return true;
}

// Make buf hold at least `needed` bytes of PSRAM, keeping a big enough block.
template <typename Buffer>
bool reservePsram(Buffer& buf, size_t& capacity, const size_t needed) {
  if (buf && capacity >= needed) return true;
  buf.reset(static_cast<uint8_t*>(heap_caps_malloc(needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
  capacity = buf ? needed : 0;
  return static_cast<bool>(buf);
}

// Pixel rows are packed 8 per byte, most significant bit = leftmost pixel.
// Shift a packed row by one pixel: shiftedFromRight[x] = row[x + 1],
// shiftedFromLeft[x] = row[x - 1] (zero at the ends).
uint8_t fromRight(const uint8_t* row, const int b, const int widthBytes) {
  return static_cast<uint8_t>((row[b] << 1) | (b + 1 < widthBytes ? row[b + 1] >> 7 : 0));
}
uint8_t fromLeft(const uint8_t* row, const int b) {
  return static_cast<uint8_t>((row[b] >> 1) | (b > 0 ? row[b - 1] << 7 : 0));
}

// Bits [from, to) of a packed row.
uint8_t runMask(const int b, const int from, const int to) {
  const int lo = b * 8;
  const int s = from > lo ? from - lo : 0;
  const int e = to < lo + 8 ? to - lo : 8;
  return static_cast<uint8_t>((0xFF >> s) & (0xFF << (8 - e)));
}
bool anyInRun(const uint8_t* row, const int from, const int to) {
  for (int b = from / 8; b <= (to - 1) / 8; b++) {
    if (row[b] & runMask(b, from, to)) return true;
  }
  return false;
}
void setRun(uint8_t* row, const int from, const int to) {
  for (int b = from / 8; b <= (to - 1) / 8; b++) row[b] |= runMask(b, from, to);
}

// Open the free runs of row y that touch the screen edge, or touch an open pixel
// directly above/below (neighbor row) or already in this row. True if any changed.
bool openRow(const uint8_t* halo, uint8_t* open, const uint8_t* neighbor, const int y, const int width,
             const int height, const int widthBytes) {
  const uint8_t* h = halo + static_cast<size_t>(y) * widthBytes;
  uint8_t* o = open + static_cast<size_t>(y) * widthBytes;
  const bool edgeRow = y == 0 || y == height - 1;
  bool changed = false;
  int x = 0;
  while (x < width) {
    // skip halo pixels (whole bytes at a time when possible)
    if ((x & 7) == 0 && h[x / 8] == 0xFF) {
      x += 8;
      continue;
    }
    if (h[x / 8] & (0x80 >> (x & 7))) {
      x++;
      continue;
    }
    int end = x + 1;
    while (end < width) {
      if ((end & 7) == 0 && h[end / 8] == 0x00) {
        end += 8;
        continue;
      }
      if (h[end / 8] & (0x80 >> (end & 7))) break;
      end++;
    }
    if (end > width) end = width;
    const bool reach =
        edgeRow || x == 0 || end == width || anyInRun(o, x, end) || (neighbor && anyInRun(neighbor, x, end));
    if (reach) {
      // only report a change when some bit in the run was still closed
      for (int b = x / 8; b <= (end - 1) / 8; b++) {
        const uint8_t m = runMask(b, x, end);
        if ((o[b] & m) != m) {
          changed = true;
          o[b] |= m;
        }
      }
    }
    x = end;
  }
  return changed;
}

// Width of the white outline around every home-screen element.
constexpr int OUTLINE_PX = 3;

// Lay the background back in around a UI drawn on white: every UI pixel keeps
// an OUTLINE_PX white outline (a rounded octagon), and areas the UI encloses (covers,
// boxes, the holes of letters) stay white, so only the open space around the UI
// shows the image. fb and background are raw framebuffers (physical rows, 1 bit
// per pixel, MSB = leftmost, 0 = black). Works in physical space, so it is the
// same for every orientation. Bit-parallel: the outline grows 8 pixels per byte
// operation and the open space is flooded run by run with alternating sweeps.
// scratch must hold 3 * widthBytes * height bytes.
// Gives the text inside a gray selection box the home outline. The box's gray
// is an ordered dither whose dots never touch, while text strokes do: a black
// pixel with a black 8-neighbour is text, and every lone dot within the outline
// shape (3 px, corners cut, as composeOutlinedBackground) of text turns white.
// The box (x1, y1 exclusive) is in panel pixels; ink is a scratch plane of the
// framebuffer's size.
void outlineSelectionText(uint8_t* fb, uint8_t* ink, const int widthBytes, const int x0, const int y0, const int x1,
                          const int y1) {
  constexpr int OUTLINE_PX = 3;
  // Only pixels inside the box count: the gray around it may use another pattern.
  auto black = [&](const int x, const int y) {
    return x >= x0 && y >= y0 && x < x1 && y < y1 && !(fb[y * widthBytes + x / 8] & (0x80 >> (x & 7)));
  };
  auto isInk = [&](const int x, const int y) { return ink[y * widthBytes + x / 8] & (0x80 >> (x & 7)); };
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      const uint8_t bit = 0x80 >> (x & 7);
      uint8_t& cell = ink[y * widthBytes + x / 8];
      cell &= static_cast<uint8_t>(~bit);
      if (!black(x, y)) continue;
      for (int dy = -1; dy <= 1 && !(cell & bit); dy++)
        for (int dx = -1; dx <= 1; dx++)
          if ((dx || dy) && black(x + dx, y + dy)) {
            cell |= bit;
            break;
          }
    }
  }
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      if (!black(x, y) || isInk(x, y)) continue;
      bool nearText = false;
      for (int dy = -OUTLINE_PX; dy <= OUTLINE_PX && !nearText; dy++) {
        const int ny = y + dy;
        if (ny < y0 || ny >= y1) continue;
        for (int dx = -OUTLINE_PX; dx <= OUTLINE_PX; dx++) {
          const int nx = x + dx;
          if (nx < x0 || nx >= x1 || (std::abs(dx) == OUTLINE_PX && std::abs(dy) == OUTLINE_PX)) continue;
          if (isInk(nx, ny)) {
            nearText = true;
            break;
          }
        }
      }
      if (nearText) fb[y * widthBytes + x / 8] |= static_cast<uint8_t>(0x80 >> (x & 7));
    }
  }
}

// A logical rectangle in panel pixels (x1, y1 exclusive), clamped to the panel.
void toPanelRect(const GfxRenderer::Orientation orientation, const Rect& r, const int width, const int height, int& x0,
                 int& y0, int& x1, int& y1) {
  auto map = [&](const int lx, const int ly, int& px, int& py) {
    switch (orientation) {
      case GfxRenderer::Portrait:
        px = ly;
        py = height - 1 - lx;
        break;
      case GfxRenderer::LandscapeClockwise:
        px = width - 1 - lx;
        py = height - 1 - ly;
        break;
      case GfxRenderer::PortraitInverted:
        px = width - 1 - ly;
        py = lx;
        break;
      default:
        px = lx;
        py = ly;
        break;
    }
  };
  int ax, ay, bx, by;
  map(r.x, r.y, ax, ay);
  map(r.x + r.width - 1, r.y + r.height - 1, bx, by);
  x0 = std::max(0, std::min(ax, bx));
  y0 = std::max(0, std::min(ay, by));
  x1 = std::min(width, std::max(ax, bx) + 1);
  y1 = std::min(height, std::max(ay, by) + 1);
}

void composeOutlinedBackground(uint8_t* fb, const uint8_t* background, uint8_t* scratch, const int width,
                               const int height) {
  const int wb = width / 8;
  const size_t plane = static_cast<size_t>(wb) * height;
  uint8_t* row = scratch;               // the ink, grown into a square
  uint8_t* halo = scratch + plane;      // octagon of radius OUTLINE_PX around the ink
  uint8_t* open = scratch + 2 * plane;  // open space reachable from the edge

  for (size_t i = 0; i < plane; i++) row[i] = static_cast<uint8_t>(~fb[i]);  // ink = black pixels
  // Each pass grows the square by 1 px on every side (rows, then columns).
  for (int pass = 0; pass < OUTLINE_PX - 1; pass++) {
    for (int y = 0; y < height; y++) {
      const uint8_t* r = row + static_cast<size_t>(y) * wb;
      uint8_t* t = halo + static_cast<size_t>(y) * wb;
      for (int b = 0; b < wb; b++) t[b] = r[b] | fromRight(r, b, wb) | fromLeft(r, b);
    }
    for (int y = 0; y < height; y++) {
      const uint8_t* t = halo + static_cast<size_t>(y) * wb;
      uint8_t* r = row + static_cast<size_t>(y) * wb;
      for (int b = 0; b < wb; b++) r[b] = t[b] | (y > 0 ? t[b - wb] : 0) | (y + 1 < height ? t[b + wb] : 0);
    }
  }
  for (int y = 0; y < height; y++) {  // plus around the square = octagon
    const uint8_t* s = row + static_cast<size_t>(y) * wb;
    uint8_t* h = halo + static_cast<size_t>(y) * wb;
    for (int b = 0; b < wb; b++) {
      h[b] = s[b] | fromRight(s, b, wb) | fromLeft(s, b) | (y > 0 ? s[b - wb] : 0) | (y + 1 < height ? s[b + wb] : 0);
    }
  }

  memset(open, 0, plane);
  constexpr int kMaxSweeps = 64;
  for (int sweep = 0; sweep < kMaxSweeps; sweep++) {
    bool changed = false;
    for (int y = 0; y < height; y++) {
      changed |= openRow(halo, open, y > 0 ? open + static_cast<size_t>(y - 1) * wb : nullptr, y, width, height, wb);
    }
    for (int y = height - 1; y >= 0; y--) {
      changed |=
          openRow(halo, open, y + 1 < height ? open + static_cast<size_t>(y + 1) * wb : nullptr, y, width, height, wb);
    }
    if (!changed) break;
  }

  for (size_t i = 0; i < plane; i++) fb[i] = (fb[i] & ~open[i]) | (background[i] & open[i]);
}
}  // namespace

void RaincloudTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                         const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                         bool& bufferRestored, std::function<bool()> storeCoverBuffer,
                                         const int progress) const {
  LyraTheme::drawRecentBookCover(renderer, rect, recentBooks, selectorIndex, coverRendered, coverBufferStored,
                                 bufferRestored, std::move(storeCoverBuffer), progress);
}

void RaincloudTheme::PsramFree::operator()(uint8_t* ptr) const {
  if (ptr) heap_caps_free(ptr);
}

bool RaincloudTheme::isHomeHeaderRect(const GfxRenderer& renderer, Rect rect) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return rect.y == metrics.topPadding && rect.height == metrics.homeTopPadding - metrics.topPadding;
}

void RaincloudTheme::fillSelectionBackground(const GfxRenderer& renderer, int x, int y, int width, int height) const {
  renderer.fillRectDitherBlend(x, y, width, height, Color::LightGray, 50);
  addSelectionTextRect(Rect{x, y, width, height});
}

void RaincloudTheme::addSelectionTextRect(const Rect rect) const {
  if (outlinePending && selectionTextRectCount < MAX_SELECTION_TEXT_RECTS && rect.width > 0 && rect.height > 0) {
    selectionTextRects[selectionTextRectCount++] = rect;
  }
}

void RaincloudTheme::pickIndexedBackground(std::string& selectedPath) const {
  const uint32_t epoch = SdContentEpoch::current();
  if (!backgroundIndexValid || backgroundIndexEpoch != epoch) {
    backgroundIndexEpoch = epoch;
    // One pool from both sleep folders, like the sleep screen (CrossPoint 6251054b).
    backgroundIndexPaths.clear();
    backgroundIndexCount = 0;
    backgroundIndexValid = scanBackgroundFolder("/.sleep", backgroundIndexPaths, backgroundIndexCount) &&
                           scanBackgroundFolder("/sleep", backgroundIndexPaths, backgroundIndexCount);
  }
  if (backgroundIndexCount == 0) return;

  const auto target = static_cast<uint16_t>(random(backgroundIndexCount));
  const char* path = backgroundIndexPaths.c_str();
  for (uint16_t i = 0; i < target; i++) path += strlen(path) + 1;
  selectedPath = path;
}

void RaincloudTheme::drawHomeBackground(GfxRenderer& renderer, const std::vector<RecentBook>& recentBooks) const {
  outlinePending = false;
  selectionTextRectCount = 0;
  drawBackgroundLayer(renderer, recentBooks);

  uint8_t* fb = renderer.getFrameBuffer();
  const size_t fbSize = renderer.getBufferSize();
  if (!fb || fbSize == 0 || !reservePsram(outlineBackground, outlineBackgroundSize, fbSize) ||
      !reservePsram(outlineMask, outlineMaskSize, fbSize * 3)) {
    return;  // no PSRAM: keep the background drawn directly under the UI
  }
  memcpy(outlineBackground.get(), fb, fbSize);
  renderer.clearScreen();
  outlinePending = true;
}

void RaincloudTheme::finishHomeScreen(GfxRenderer& renderer) const {
  frameComposed = false;
  frameUnchanged = false;
  if (!outlinePending) return;
  outlinePending = false;
  uint8_t* fb = renderer.getFrameBuffer();
  const int widthBytes = renderer.getDisplayWidthBytes();
  const size_t fbSize = renderer.getBufferSize();
  if (!fb || widthBytes <= 0 || fbSize > outlineBackgroundSize || fbSize * 3 > outlineMaskSize) return;
  const int panelWidth = widthBytes * 8;
  const int panelHeight = static_cast<int>(fbSize / widthBytes);
  for (int i = 0; i < selectionTextRectCount; i++) {
    int x0, y0, x1, y1;
    toPanelRect(renderer.getOrientation(), selectionTextRects[i], panelWidth, panelHeight, x0, y0, x1, y1);
    if (x0 < x1 && y0 < y1) outlineSelectionText(fb, outlineMask.get(), widthBytes, x0, y0, x1, y1);
  }
  selectionTextRectCount = 0;
  composeOutlinedBackground(fb, outlineBackground.get(), outlineMask.get(), widthBytes * 8,
                            static_cast<int>(fbSize / widthBytes));
  frameComposed = true;
  frameUnchanged = shownValid && shownFrameSize >= fbSize && renderer.panelUpdateCount() == shownPanelUpdates &&
                   memcmp(fb, shownFrame.get(), fbSize) == 0;
}

void RaincloudTheme::homeFrameShown(const GfxRenderer& renderer) const {
  const uint8_t* fb = renderer.getFrameBuffer();
  const size_t fbSize = renderer.getBufferSize();
  shownValid = frameComposed && fb && fbSize > 0 && reservePsram(shownFrame, shownFrameSize, fbSize);
  if (!shownValid) return;
  memcpy(shownFrame.get(), fb, fbSize);
  shownPanelUpdates = renderer.panelUpdateCount();
}

void RaincloudTheme::drawBackgroundLayer(GfxRenderer& renderer, const std::vector<RecentBook>& recentBooks) const {
  const RecentBook* book = recentBooks.empty() ? nullptr : &recentBooks.front();
  const bool bookChanged = book ? book->path != backgroundBookPath : !backgroundBookPath.empty();
  const bool coverChanged = book ? book->coverBmpPath != backgroundCoverPath : !backgroundCoverPath.empty();
  if (bookChanged || coverChanged) {
    backgroundBookPath = book ? book->path : "";
    backgroundCoverPath = book ? book->coverBmpPath : "";
    backgroundThumbPath =
        backgroundCoverPath.empty() ? "" : UITheme::getCoverThumbPath(backgroundCoverPath, kBackgroundCoverHeight);
    randomBackgroundPath.clear();
    randomBackgroundPicked = false;
    cachedPath.clear();
    cachedWidth = 0;
    cachedHeight = 0;
    cachedOrientation = -1;
  }

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int orientation = renderer.getOrientation();
  const size_t cacheBytes = backgroundCacheBytes(width, height);

  auto drawCachedOrLoad = [this, &width, &height, &orientation, &renderer, &cacheBytes](const std::string& path) {
    if (path.empty()) return false;

    const uint8_t coverMode = SETTINGS.sleepScreenCoverMode;
    const uint8_t coverFilter = SETTINGS.sleepScreenCoverFilter;
    if (backgroundCache) {
      if (cachedPath == path && cachedWidth == width && cachedHeight == height && cachedOrientation == orientation &&
          cachedCoverMode == coverMode && cachedCoverFilter == coverFilter) {
        return replayBackgroundCache(renderer, backgroundCache.get(), backgroundCacheCapacity);
      }
    } else if (!psramUnavailableLogged) {
      backgroundCache.reset(static_cast<uint8_t*>(heap_caps_malloc(cacheBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
      if (backgroundCache) {
        backgroundCacheCapacity = cacheBytes;
      } else {
        psramUnavailableLogged = true;
        LOG_DBG("THEME", "Raincloud PSRAM cache unavailable; drawing background from SD");
      }
    }

    const bool useCache = backgroundCache && backgroundCacheCapacity >= cacheBytes;
    if (!renderBackgroundBmp(renderer, path, useCache ? backgroundCache.get() : nullptr,
                             useCache ? backgroundCacheCapacity : 0)) {
      cachedPath.clear();
      return false;
    }
    if (useCache) {
      cachedPath = path;
      cachedWidth = width;
      cachedHeight = height;
      cachedOrientation = orientation;
      cachedCoverMode = coverMode;
      cachedCoverFilter = coverFilter;
    }
    return true;
  };

  if (!backgroundThumbPath.empty()) {
    if (drawCachedOrLoad(backgroundThumbPath)) return;
    renderer.clearScreen();
  }

  if (!randomBackgroundPicked) {
    randomBackgroundPicked = true;
    pickIndexedBackground(randomBackgroundPath);
  }
  if (!randomBackgroundPath.empty() && drawCachedOrLoad(randomBackgroundPath)) return;
  if (!randomBackgroundPath.empty()) {
    // Drawing failed part-way: start clean, do not retry this file, and rescan
    // the folder next time (the file may have been removed or replaced).
    renderer.clearScreen();
    randomBackgroundPath.clear();
    backgroundIndexValid = false;
  }

  cachedPath.clear();
  renderer.fillRectDither(0, 0, width, height, Color::DarkGray);
}
