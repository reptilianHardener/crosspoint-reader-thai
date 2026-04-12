#include "Lyra3CoversTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
constexpr int kCarouselGap = 8;
constexpr float kSideScale = 0.72f;
// Portrait book tile ratio (width / height)
constexpr float kCoverAspect = 2.0f / 3.0f;

static void drawCoverCell(GfxRenderer& renderer, const RecentBook& book, int tileX, int tileY, int tileW, int tileH,
                          int thumbHeight) {
  const int innerW = tileW - 2 * hPaddingInSelection;
  const int innerH = tileH - 2 * hPaddingInSelection;

  bool hasCover = true;
  if (book.coverBmpPath.empty()) {
    hasCover = false;
  } else {
    const std::string coverBmpPath = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
    FsFile file;
    if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const float coverHeight = static_cast<float>(bitmap.getHeight());
        const float coverWidth = static_cast<float>(bitmap.getWidth());
        const float ratio = coverWidth / coverHeight;
        const float tileRatio = static_cast<float>(innerW) / static_cast<float>(innerH);
        const float cropX = 1.0f - (tileRatio / ratio);

        renderer.drawBitmap(bitmap, tileX + hPaddingInSelection, tileY + hPaddingInSelection, innerW, innerH, cropX);
      } else {
        hasCover = false;
      }
      file.close();
    } else {
      hasCover = false;
    }
  }

  renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, innerW, innerH, true);

  if (!hasCover) {
    renderer.fillRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection + (innerH / 3), innerW,
                      2 * innerH / 3, true);
    renderer.drawIcon(CoverIcon, tileX + hPaddingInSelection + 24, tileY + hPaddingInSelection + 24, 32, 32);
  }
}
}  // namespace

void Lyra3CoversTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> /*storeCoverBuffer*/,
                                           int carouselLayoutFocusIndex) const {
  (void)bufferRestored;

  const int padding = Lyra3CoversMetrics::values.contentSidePadding;
  const int innerLeft = rect.x + padding;
  const int innerWidth = rect.width - 2 * padding;
  const int tileYBase = rect.y;
  const int largeH = Lyra3CoversMetrics::values.homeCoverHeight;
  const int largeW = std::max(1, static_cast<int>(largeH * kCoverAspect + 0.5f));
  const int smallH = std::max(1, static_cast<int>(largeH * kSideScale + 0.5f));
  const int smallW = std::max(1, static_cast<int>(smallH * kCoverAspect + 0.5f));

  const bool hasContinueReading = !recentBooks.empty();
  if (!hasContinueReading) {
    drawEmptyRecents(renderer, rect);
    coverRendered = false;
    coverBufferStored = false;
    return;
  }

  const int n = static_cast<int>(recentBooks.size());
  int layoutFocus = 0;
  if (selectorIndex >= 0 && selectorIndex < n) {
    layoutFocus = selectorIndex;
  } else if (carouselLayoutFocusIndex >= 0) {
    layoutFocus = std::clamp(carouselLayoutFocusIndex, 0, n - 1);
  }

  std::vector<int> widths(static_cast<size_t>(n));
  std::vector<int> heights(static_cast<size_t>(n));
  for (int i = 0; i < n; i++) {
    const bool isFocus = (i == layoutFocus);
    widths[static_cast<size_t>(i)] = isFocus ? largeW : smallW;
    heights[static_cast<size_t>(i)] = isFocus ? largeH : smallH;
  }

  std::vector<int> leftEdge(static_cast<size_t>(n));
  int stripW = 0;
  for (int i = 0; i < n; i++) {
    leftEdge[static_cast<size_t>(i)] = stripW;
    stripW += widths[static_cast<size_t>(i)];
    if (i + 1 < n) {
      stripW += kCarouselGap;
    }
  }

  const int focusW = widths[static_cast<size_t>(layoutFocus)];
  const int focusCenter = leftEdge[static_cast<size_t>(layoutFocus)] + focusW / 2;
  const int idealScroll = focusCenter - innerWidth / 2;
  const int scrollMax = std::max(0, stripW - innerWidth);
  const int scroll = std::clamp(idealScroll, 0, scrollMax);

  for (int i = 0; i < n; i++) {
    const int w = widths[static_cast<size_t>(i)];
    const int h = heights[static_cast<size_t>(i)];
    const int lx = innerLeft + leftEdge[static_cast<size_t>(i)] - scroll;
    const int ly = tileYBase + largeH - h;
    const int thumbH = Lyra3CoversMetrics::values.homeCoverHeight;
    drawCoverCell(renderer, recentBooks[static_cast<size_t>(i)], lx, ly, w, h, thumbH);
  }

  const int selIdx = selectorIndex;
  if (selIdx >= 0 && selIdx < n) {
    const int i = selIdx;
    const int w = widths[static_cast<size_t>(i)];
    const int h = heights[static_cast<size_t>(i)];
    const int lx = innerLeft + leftEdge[static_cast<size_t>(i)] - scroll;
    const int ly = tileYBase + largeH - h;

    const int maxLineWidth = w - 2 * hPaddingInSelection;
    auto titleLines = renderer.wrappedText(SMALL_FONT_ID, recentBooks[static_cast<size_t>(i)].title.c_str(),
                                           maxLineWidth, 3);
    const int titleLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int dynamicBlockHeight = static_cast<int>(titleLines.size()) * titleLineHeight;
    const int dynamicTitleBoxHeight = dynamicBlockHeight + hPaddingInSelection + 5;

    renderer.fillRoundedRect(lx, ly, w, hPaddingInSelection, cornerRadius, true, true, false, false, Color::LightGray);
    renderer.fillRectDither(lx, ly + hPaddingInSelection, hPaddingInSelection, h, Color::LightGray);
    renderer.fillRectDither(lx + w - hPaddingInSelection, ly + hPaddingInSelection, hPaddingInSelection, h,
                            Color::LightGray);
    renderer.fillRoundedRect(lx, ly + h + hPaddingInSelection, w, dynamicTitleBoxHeight, cornerRadius, false, false, true,
                             true, Color::LightGray);

    int currentY = ly + h + hPaddingInSelection + 5;
    for (const auto& line : titleLines) {
      renderer.drawText(SMALL_FONT_ID, lx + hPaddingInSelection, currentY, line.c_str(), true);
      currentY += titleLineHeight;
    }
  }

  coverRendered = false;
  coverBufferStored = false;
}
