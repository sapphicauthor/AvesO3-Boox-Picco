#include "RaincloudTheme.h"

#include <Arduino.h>
#include <Bitmap.h>
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

namespace {
constexpr int kBackgroundCoverHeight = 226;
constexpr float kBackgroundStrengthPercent = 60.0f;
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
  const float value = level * kBackgroundStrengthPercent / 100.0f + 1.2f;
  int rounded = static_cast<int>(value);
  if (value - rounded >= 0.5f) rounded++;
  return static_cast<uint8_t>(rounded);
}
constexpr uint8_t kBwThreshold[4] = {bwThreshold(0), bwThreshold(1), bwThreshold(2), bwThreshold(3)};
constexpr uint8_t kGrayShade[4] = {grayShade(0), grayShade(1), grayShade(2), grayShade(3)};

}  // namespace

// Draw one background pixel. level is the image's 2-bit gray (0 = black,
// 3 = white), lightened to kBackgroundStrengthPercent of its darkness.
void plotBackgroundPixel(const GfxRenderer& renderer, const int x, const int y, const unsigned level,
                         const int renderMode) {
  if (renderMode == GfxRenderer::BW) {
    if (kBayer4[y & 3][x & 3] < kBwThreshold[level & 3]) renderer.drawPixel(x, y, true);
    return;
  }

  const uint8_t shade = kGrayShade[level & 3];
  bool draw = false;
  if (renderMode == GfxRenderer::GRAYSCALE_MSB) {
    draw = shade == 1 || shade == 2;
  } else if (renderMode == GfxRenderer::GRAYSCALE_LSB) {
    draw = shade == 1;
  } else if (renderMode == GfxRenderer::GRAYSCALE_ABS_LSB) {
    draw = shade == 1 || shade == 3;
  } else if (renderMode == GfxRenderer::GRAYSCALE_ABS_MSB) {
    draw = shade == 2 || shade == 3;
  }
  if (draw) renderer.drawPixel(x, y, false);
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

bool pickRandomBackground(const char* dirPath, std::string& selectedPath) {
  auto dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return false;

  auto name = makeUniqueNoThrow<char[]>(kMaxBackgroundFileNameLen);
  if (!name) {
    LOG_ERR("THEME", "OOM: sleep image filename buffer");
    return false;
  }

  uint16_t fileCount = 0;
  while (fileCount < UINT16_MAX) {
    auto file = dir.openNextFile();
    if (!file) break;
    file.getName(name.get(), kMaxBackgroundFileNameLen);
    fileCount += isUsableBackground(file, name.get());
  }
  if (fileCount == 0) return false;

  const auto target = static_cast<uint16_t>(random(fileCount));
  dir.rewindDirectory();
  uint16_t index = 0;
  bool found = false;
  while (true) {
    auto file = dir.openNextFile();
    if (!file) break;
    file.getName(name.get(), kMaxBackgroundFileNameLen);
    if (!isUsableBackground(file, name.get())) continue;
    if (index == target) {
      selectedPath.reserve(strlen(dirPath) + strlen(name.get()) + 1);
      selectedPath = dirPath;
      selectedPath += "/";
      selectedPath += name.get();
      found = true;
      break;
    }
    index++;
  }
  return found;
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

void RaincloudTheme::drawHomeBackground(GfxRenderer& renderer, const std::vector<RecentBook>& recentBooks) const {
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
    if (!pickRandomBackground("/.sleep", randomBackgroundPath)) {
      pickRandomBackground("/sleep", randomBackgroundPath);
    }
  }
  if (!randomBackgroundPath.empty() && drawCachedOrLoad(randomBackgroundPath)) return;
  if (!randomBackgroundPath.empty()) {
    // Drawing failed part-way: start clean and do not retry this file.
    renderer.clearScreen();
    randomBackgroundPath.clear();
  }

  cachedPath.clear();
  renderer.fillRectDither(0, 0, width, height, Color::DarkGray);
}
