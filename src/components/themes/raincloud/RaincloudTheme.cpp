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

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "util/SdContentEpoch.h"

namespace {
constexpr int kBackgroundCoverHeight = 226;
// How much of the image's darkness the background keeps: 100 draws it at full
// grayscale, lower values lighten it toward white.
constexpr float kBackgroundStrengthPercent = 100.0f;
constexpr size_t kMaxBackgroundFileNameLen = 256;

constexpr uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// The lightening only depends on the 2-bit level, so it is worked out at compile
// time: the pixel loop runs for every screen pixel and float division is slow.
// BW: a pixel is drawn when its Bayer value is below the level's threshold.
constexpr uint8_t bwThreshold(const unsigned level) {
  const float darkness =
      static_cast<float>(3 - static_cast<int>(level)) / 3.0f * kBackgroundStrengthPercent / 100.0f * 16.0f;
  uint8_t count = 0;
  while (count < 16 && count < darkness) count++;
  return count;
}
// Gray modes: the 2-bit shade the level is lightened to (rounded half up).
constexpr uint8_t grayShade(const unsigned level) {
  const float value = 3.0f - static_cast<float>(3 - static_cast<int>(level)) * kBackgroundStrengthPercent / 100.0f;
  int rounded = static_cast<int>(value);
  if (value - rounded >= 0.5f) rounded++;
  return static_cast<uint8_t>(rounded);
}
constexpr uint8_t kBwThreshold[4] = {bwThreshold(0), bwThreshold(1), bwThreshold(2), bwThreshold(3)};
constexpr uint8_t kGrayShade[4] = {grayShade(0), grayShade(1), grayShade(2), grayShade(3)};

}  // namespace

// Draw one background pixel. level is the image's 2-bit gray (0 = black,
// 3 = white), keeping kBackgroundStrengthPercent of its darkness.
void plotBackgroundPixel(const GfxRenderer& renderer, const int x, const int y, const unsigned level,
                         const int renderMode) {
  if (renderMode == GfxRenderer::BW) {
    if (kBayer4[y & 3][x & 3] < kBwThreshold[level & 3]) renderer.drawPixel(x, y, true);
    return;
  }

  if (renderMode != GfxRenderer::GRAYSCALE_LSB && renderMode != GfxRenderer::GRAYSCALE_MSB) return;
  const auto pixel = grayPlanePixel(kGrayShade[level & 3], renderMode == GfxRenderer::GRAYSCALE_MSB,
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

// Draw a BMP scaled to cover the screen (centre crop). When cache is set, the
// 2-bit level of every screen pixel is stored there as well.
bool renderBackgroundBmp(const GfxRenderer& renderer, const std::string& path, uint8_t* cache, const size_t cacheSize) {
  HalFile file;
  if (!Storage.openFileForRead("THEME", path, file)) {
    LOG_DBG("THEME", "Unable to open background image: %s", path.c_str());
    return false;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_DBG("THEME", "Invalid background BMP: %s", path.c_str());
    return false;
  }

  const int bmpWidth = bitmap.getWidth();
  const int bmpHeight = bitmap.getHeight();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  if (bmpWidth <= 0 || bmpHeight <= 0 || screenWidth <= 0 || screenHeight <= 0) return false;

  const size_t cacheBytes = (static_cast<size_t>(screenWidth) * screenHeight + 3) / 4;
  if (cache && cacheSize < cacheBytes) return false;
  if (cache) memset(cache, 0, cacheBytes);

  const float screenAspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
  const float bmpAspect = static_cast<float>(bmpWidth) / static_cast<float>(bmpHeight);
  float cropWidth = static_cast<float>(bmpWidth);
  float cropHeight = static_cast<float>(bmpHeight);
  float cropX = 0.0f;
  float cropY = 0.0f;
  if (screenAspect < bmpAspect) {
    cropWidth = static_cast<float>(bmpHeight) * screenAspect;
    cropX = (static_cast<float>(bmpWidth) - cropWidth) * 0.5f;
  } else {
    cropHeight = static_cast<float>(bmpWidth) / screenAspect;
    cropY = (static_cast<float>(bmpHeight) - cropHeight) * 0.5f;
  }

  auto outputRow = makeUniqueNoThrow<uint8_t[]>((bmpWidth + 3) / 4);
  auto rowBytes = makeUniqueNoThrow<uint8_t[]>(bitmap.getRowBytes());
  // Source column of every screen column: the same for every row, so it is
  // worked out once instead of with a float divide per pixel.
  auto srcColumns = makeUniqueNoThrow<int[]>(screenWidth);
  if (!outputRow || !rowBytes || !srcColumns) {
    LOG_ERR("THEME", "OOM: background BMP row buffers");
    return false;
  }
  for (int x = 0; x < screenWidth; x++) {
    srcColumns[x] = std::min(static_cast<int>((x + 0.5f) * cropWidth / screenWidth + cropX), bmpWidth - 1);
  }

  const int renderMode = renderer.getRenderMode();
  const bool topDown = bitmap.isTopDown();
  int screenY = 0;
  int step = 1;
  if (!topDown) {
    step = -1;
    screenY = screenHeight - 1;
  }

  for (int row = 0; row < bmpHeight; row++) {
    if (bitmap.readNextRow(outputRow.get(), rowBytes.get()) != BmpReaderError::Ok) {
      LOG_ERR("THEME", "Failed to read background BMP row %d: %s", row, path.c_str());
      return false;
    }
    const int srcRow = topDown ? row : bmpHeight - 1 - row;

    while (screenY >= 0 && screenY < screenHeight) {
      const int srcY = std::min(static_cast<int>((screenY + 0.5f) * cropHeight / screenHeight + cropY), bmpHeight - 1);
      if (srcY != srcRow) break;

      for (int x = 0; x < screenWidth; x++) {
        const int srcX = srcColumns[x];
        const uint8_t level = (outputRow[srcX / 4] >> (6 - (srcX * 2) % 8)) & 0x3;
        if (cache) {
          const size_t cacheIndex = static_cast<size_t>(screenY) * screenWidth + x;
          const int shift = 6 - static_cast<int>(cacheIndex % 4) * 2;
          cache[cacheIndex / 4] = (cache[cacheIndex / 4] & ~(0x3 << shift)) | (level << shift);
        }
        plotBackgroundPixel(renderer, x, screenY, level, renderMode);
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
  for (int y = 0; y < screenHeight; y++) {
    for (int x = 0; x < screenWidth; x++) {
      const size_t cacheIndex = static_cast<size_t>(y) * screenWidth + x;
      const uint8_t level = (cache[cacheIndex / 4] >> (6 - (cacheIndex * 2) % 8)) & 0x3;
      plotBackgroundPixel(renderer, x, y, level, renderMode);
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

// Lay the background back in around a UI drawn on white: every UI pixel keeps
// a 2 px white outline (radius-2 octagon), and areas the UI encloses (covers,
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
  uint8_t* row = scratch;               // ink grown along rows, then the 3x3 square
  uint8_t* halo = scratch + plane;      // radius-2 octagon around the ink
  uint8_t* open = scratch + 2 * plane;  // open space reachable from the edge

  for (int y = 0; y < height; y++) {  // ink = black pixels; grow 1 px left/right
    const uint8_t* f = fb + static_cast<size_t>(y) * wb;
    uint8_t* r = row + static_cast<size_t>(y) * wb;
    for (int b = 0; b < wb; b++) {
      const uint8_t ink = static_cast<uint8_t>(~f[b]);
      const uint8_t inkR = static_cast<uint8_t>((ink << 1) | (b + 1 < wb ? static_cast<uint8_t>(~f[b + 1]) >> 7 : 0));
      const uint8_t inkL = static_cast<uint8_t>((ink >> 1) | (b > 0 ? static_cast<uint8_t>(~f[b - 1]) << 7 : 0));
      r[b] = ink | inkR | inkL;
    }
  }
  for (int y = 0; y < height; y++) {  // 3x3 square (into halo temporarily)
    const uint8_t* r = row + static_cast<size_t>(y) * wb;
    uint8_t* s = halo + static_cast<size_t>(y) * wb;
    for (int b = 0; b < wb; b++) {
      s[b] = r[b] | (y > 0 ? r[b - wb] : 0) | (y + 1 < height ? r[b + wb] : 0);
    }
  }
  memcpy(row, halo, plane);           // row now holds the square
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
  if (!outlinePending) return;
  outlinePending = false;
  uint8_t* fb = renderer.getFrameBuffer();
  const int widthBytes = renderer.getDisplayWidthBytes();
  const size_t fbSize = renderer.getBufferSize();
  if (!fb || widthBytes <= 0 || fbSize > outlineBackgroundSize || fbSize * 3 > outlineMaskSize) return;
  composeOutlinedBackground(fb, outlineBackground.get(), outlineMask.get(), widthBytes * 8,
                            static_cast<int>(fbSize / widthBytes));
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
  const size_t cacheBytes = (static_cast<size_t>(width) * height + 3) / 4;

  auto drawCachedOrLoad = [this, &width, &height, &orientation, &renderer, &cacheBytes](const std::string& path) {
    if (path.empty()) return false;

    if (backgroundCache) {
      if (cachedPath == path && cachedWidth == width && cachedHeight == height && cachedOrientation == orientation) {
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
