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
#include <cstring>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "util/SdContentEpoch.h"

namespace {
constexpr int kBackgroundCoverHeight = 226;
constexpr size_t kMaxBackgroundFileNameLen = 256;

}  // namespace

// Draw one background pixel the way the sleep screen draws a picture (drawBitmap):
// in BW every non-white level is black (the picture keeps its own dithering), and
// the gray passes write the level's planes. level is the image's 2-bit gray
// (0 = black, 3 = white); invert applies the Inverted Black & White filter.
void plotBackgroundPixel(const GfxRenderer& renderer, const int x, const int y, const unsigned level,
                         const int renderMode, const bool invert) {
  const unsigned shown = invert ? 3 - (level & 3) : level & 3;
  if (renderMode == GfxRenderer::BW) {
    if (shown < 3) renderer.drawPixel(x, y, true);
    return;
  }
  if (renderMode != GfxRenderer::GRAYSCALE_LSB && renderMode != GfxRenderer::GRAYSCALE_MSB) return;
  const auto pixel = grayPlanePixel(static_cast<uint8_t>(shown), renderMode == GfxRenderer::GRAYSCALE_MSB,
                                    renderer.grayPlanesAreAbsolute());
  if (pixel.write) renderer.drawPixel(x, y, pixel.black);
}

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

// Append every usable background in dirPath to names ('\0'-terminated each).
// Returns false only when the filename buffer cannot be allocated.
bool scanBackgroundFolder(const char* dirPath, std::string& names, uint16_t& count) {
  names.clear();
  count = 0;
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
    names.append(name.get());
    names.push_back('\0');
    count++;
  }
  return true;
}

// Draw a BMP over the screen following the sleep screen's Cover Mode: Crop fills
// the screen (centre crop), Fit shows the whole picture centred on white. When
// cache is set, the 2-bit level of every screen pixel is stored there as well
// (white outside the picture). hasGray reports whether the BMP has gray levels.
bool renderBackgroundBmp(const GfxRenderer& renderer, const std::string& path, uint8_t* cache, const size_t cacheSize,
                         bool& hasGray) {
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
  hasGray = bitmap.hasGreyscale();

  const int bmpWidth = bitmap.getWidth();
  const int bmpHeight = bitmap.getHeight();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  if (bmpWidth <= 0 || bmpHeight <= 0 || screenWidth <= 0 || screenHeight <= 0) return false;

  const size_t cacheBytes = (static_cast<size_t>(screenWidth) * screenHeight + 3) / 4;
  if (cache && cacheSize < cacheBytes) return false;
  if (cache) memset(cache, 0xFF, cacheBytes);  // level 3 (white) wherever the picture is not

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

  auto outputRow = makeUniqueNoThrow<uint8_t[]>((bmpWidth + 3) / 4);
  auto rowBytes = makeUniqueNoThrow<uint8_t[]>(bitmap.getRowBytes());
  // Source column of every screen column (-1 outside the picture): the same for
  // every row, so it is worked out once instead of with a float divide per pixel.
  auto srcColumns = makeUniqueNoThrow<int[]>(screenWidth);
  if (!outputRow || !rowBytes || !srcColumns) {
    LOG_ERR("THEME", "OOM: background BMP row buffers");
    return false;
  }
  for (int x = 0; x < screenWidth; x++) {
    const bool inside = x >= dstX0 && x < dstX0 + dstWidth;
    srcColumns[x] =
        inside ? std::min(static_cast<int>((x - dstX0 + 0.5f) * srcWidth / dstWidth + srcX0), bmpWidth - 1) : -1;
  }

  const int renderMode = renderer.getRenderMode();
  const bool invert = SETTINGS.sleepScreenCoverFilter ==
                      CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE;
  const bool topDown = bitmap.isTopDown();
  int screenY = topDown ? dstY0 : dstY0 + dstHeight - 1;
  const int step = topDown ? 1 : -1;

  for (int row = 0; row < bmpHeight; row++) {
    if (bitmap.readNextRow(outputRow.get(), rowBytes.get()) != BmpReaderError::Ok) {
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
        const uint8_t level = (outputRow[srcX / 4] >> (6 - (srcX * 2) % 8)) & 0x3;
        if (cache) {
          const size_t cacheIndex = static_cast<size_t>(screenY) * screenWidth + x;
          const int shift = 6 - static_cast<int>(cacheIndex % 4) * 2;
          cache[cacheIndex / 4] = (cache[cacheIndex / 4] & ~(0x3 << shift)) | (level << shift);
        }
        plotBackgroundPixel(renderer, x, screenY, level, renderMode, invert);
      }
      screenY += step;
    }
  }
  return true;
}

bool replayBackgroundCache(const GfxRenderer& renderer, const uint8_t* cache, const size_t cacheSize) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  if (cacheSize < (static_cast<size_t>(screenWidth) * screenHeight + 3) / 4) return false;

  const int renderMode = renderer.getRenderMode();
  const bool invert = SETTINGS.sleepScreenCoverFilter ==
                      CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE;
  for (int y = 0; y < screenHeight; y++) {
    for (int x = 0; x < screenWidth; x++) {
      const size_t cacheIndex = static_cast<size_t>(y) * screenWidth + x;
      const uint8_t level = (cache[cacheIndex / 4] >> (6 - (cacheIndex * 2) % 8)) & 0x3;
      plotBackgroundPixel(renderer, x, y, level, renderMode, invert);
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

void RaincloudTheme::PsramFree::operator()(uint8_t* ptr) const {
  if (ptr) heap_caps_free(ptr);
}

bool RaincloudTheme::isHomeHeaderRect(const GfxRenderer& renderer, Rect rect) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return rect.y == metrics.topPadding && rect.height == metrics.homeTopPadding - metrics.topPadding;
}

void RaincloudTheme::fillSelectionBackground(const GfxRenderer& renderer, int x, int y, int width, int height) const {
  renderer.fillRectDitherBlend(x, y, width, height, Color::LightGray, 50);
}

void RaincloudTheme::pickIndexedBackground(std::string& selectedPath) const {
  const uint32_t epoch = SdContentEpoch::current();
  if (!backgroundIndexValid || backgroundIndexEpoch != epoch) {
    backgroundIndexEpoch = epoch;
    backgroundIndexDir = "/.sleep";
    backgroundIndexValid = scanBackgroundFolder("/.sleep", backgroundIndexNames, backgroundIndexCount);
    if (backgroundIndexValid && backgroundIndexCount == 0) {
      backgroundIndexDir = "/sleep";
      backgroundIndexValid = scanBackgroundFolder("/sleep", backgroundIndexNames, backgroundIndexCount);
    }
  }
  if (backgroundIndexCount == 0) return;

  const auto target = static_cast<uint16_t>(random(backgroundIndexCount));
  const char* name = backgroundIndexNames.c_str();
  for (uint16_t i = 0; i < target; i++) name += strlen(name) + 1;
  selectedPath.reserve(backgroundIndexDir.size() + strlen(name) + 1);
  selectedPath = backgroundIndexDir;
  selectedPath += "/";
  selectedPath += name;
}

void RaincloudTheme::drawHomeBackground(GfxRenderer& renderer, const std::vector<RecentBook>& recentBooks) const {
  outlinePending = false;
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
  grayPassReady = false;
  frameComposed = false;
  frameUnchanged = false;
  if (!outlinePending) return;
  outlinePending = false;
  uint8_t* fb = renderer.getFrameBuffer();
  const int widthBytes = renderer.getDisplayWidthBytes();
  const size_t fbSize = renderer.getBufferSize();
  if (!fb || widthBytes <= 0 || fbSize > outlineBackgroundSize || fbSize * 3 > outlineMaskSize) return;
  composeOutlinedBackground(fb, outlineBackground.get(), outlineMask.get(), widthBytes * 8,
                            static_cast<int>(fbSize / widthBytes));
  frameComposed = true;
  frameUnchanged = shownValid && shownFrameSize >= fbSize && renderer.panelUpdateCount() == shownPanelUpdates &&
                   memcmp(fb, shownFrame.get(), fbSize) == 0;

  // Gray pass, as on the sleep screen: only with no cover filter, for a picture
  // with gray levels, on a panel with full gray planes.
  const auto mode = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Direct).supported()
                        ? HalDisplay::GrayscaleMode::Direct
                        : HalDisplay::GrayscaleMode::Absolute;
  grayPassReady = drawnFromGrayCache && backgroundCache &&
                  SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER &&
                  renderer.grayscaleCapabilities(mode).supported();
  if (grayPassReady) memcpy(outlineBackground.get(), fb, fbSize);  // keep the BW frame for the gray pass
}

void RaincloudTheme::homeFrameShown(const GfxRenderer& renderer) const {
  const uint8_t* fb = renderer.getFrameBuffer();
  const size_t fbSize = renderer.getBufferSize();
  shownValid = frameComposed && fb && fbSize > 0 && reservePsram(shownFrame, shownFrameSize, fbSize);
  if (!shownValid) return;
  memcpy(shownFrame.get(), fb, fbSize);
  shownPanelUpdates = renderer.panelUpdateCount();
}

void RaincloudTheme::displayHomeGrayPass(GfxRenderer& renderer) const {
  if (!grayPassReady) return;
  grayPassReady = false;
  uint8_t* fb = renderer.getFrameBuffer();
  const size_t fbSize = renderer.getBufferSize();
  if (!fb || !backgroundCache || fbSize > outlineBackgroundSize || fbSize * 3 > outlineMaskSize) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  const uint8_t* frame = outlineBackground.get();         // the BW home frame
  const uint8_t* open = outlineMask.get() + 2 * fbSize;   // where the picture shows
  const auto mode = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Direct).supported()
                        ? HalDisplay::GrayscaleMode::Direct
                        : HalDisplay::GrayscaleMode::Absolute;

  memcpy(fb, frame, fbSize);
  if (!renderer.displayGrayscaleBase(mode, HalDisplay::FAST_REFRESH)) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  // Each plane: the picture's levels in its open space, the UI's own black and
  // white everywhere else (full planes write every pixel).
  for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.clearScreen(0xFF);
    renderer.setRenderMode(plane);
    replayBackgroundCache(renderer, backgroundCache.get(), backgroundCacheCapacity);
    for (size_t i = 0; i < fbSize; i++) fb[i] = (fb[i] & open[i]) | (frame[i] & ~open[i]);
    if (plane == GfxRenderer::GRAYSCALE_LSB) {
      renderer.copyGrayscaleLsbBuffers();
    } else {
      renderer.copyGrayscaleMsbBuffers();
    }
  }
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  // Put the BW frame back and hand it to the panel as the reference for the next
  // fast refresh (as the reader does), so no gray ghost carries over.
  memcpy(fb, frame, fbSize);
  renderer.cleanupGrayscaleWithFrameBuffer();
}

void RaincloudTheme::drawBackgroundLayer(GfxRenderer& renderer, const std::vector<RecentBook>& recentBooks) const {
  drawnFromGrayCache = false;
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
  const size_t cacheBytes = (static_cast<size_t>(width) * height + 3) / 4;

  auto drawCachedOrLoad = [this, &width, &height, &orientation, &renderer, &cacheBytes](const std::string& path) {
    if (path.empty()) return false;

    const uint8_t coverMode = SETTINGS.sleepScreenCoverMode;
    if (backgroundCache) {
      if (cachedPath == path && cachedWidth == width && cachedHeight == height && cachedOrientation == orientation &&
          cachedCoverMode == coverMode) {
        if (!replayBackgroundCache(renderer, backgroundCache.get(), backgroundCacheCapacity)) return false;
        drawnFromGrayCache = cachedHasGray;
        return true;
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
    bool hasGray = false;
    if (!renderBackgroundBmp(renderer, path, useCache ? backgroundCache.get() : nullptr,
                             useCache ? backgroundCacheCapacity : 0, hasGray)) {
      cachedPath.clear();
      return false;
    }
    if (useCache) {
      cachedPath = path;
      cachedWidth = width;
      cachedHeight = height;
      cachedOrientation = orientation;
      cachedCoverMode = coverMode;
      cachedHasGray = hasGray;
      drawnFromGrayCache = hasGray;
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
