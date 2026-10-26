#include "TategakiTheme.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// UI font: it has the SD fallback that carries Thai and Japanese.
constexpr int kTitleFont = UI_12_FONT_ID;
constexpr int kSidePadding = 16;
constexpr int kSectionGap = 10;
constexpr int kTitleColumns = 2;  // one without a cover, where the card is compact
constexpr int kColumnGap = 6;
constexpr int kBarWidth = 10;
constexpr int kRuleGap = 12;
constexpr int kSpineCount = 5;
constexpr int kSpineWidth = 44;
constexpr int kSpineGap = 6;
constexpr int kInfoGap = 10;  // between the author and progress columns
constexpr int kSpinePadding = 10;
constexpr int kMarkSize = 14;
constexpr int kSelectionInset = 4;
constexpr int kCoverGap = 12;
constexpr int kMinCoverHeight = 60;

struct TategakiLayout {
  int top = 0;
  int bottom = 0;
  int titleRight = 0;  // right edge of the first (rightmost) title column
  int titleColumnWidth = 0;
  int titleColumns = 0;
  int authorX = 0;  // left edge of the author column
  int authorWidth = 0;
  int progressX = 0;  // left edge of the progress column
  int progressWidth = 0;
  int ruleX = 0;
  int labelRight = 0;  // right edge of the vertical "recent books" label
  int spinesRight = 0;
  Rect cover;       // highlighted book's cover above the title; empty when it does not fit
  int textTop = 0;  // where the vertical title and the author / progress column start
};

TategakiLayout computeLayout(const GfxRenderer& renderer, const Rect rect, const int bottomLimit,
                             const bool showCover) {
  TategakiLayout l;
  l.top = rect.y + 8;
  l.bottom = std::min(rect.y + rect.height, bottomLimit) - 4;
  l.titleColumnWidth = renderer.getLineHeight(kTitleFont);
  l.titleRight = rect.x + rect.width - kSidePadding;
  l.titleColumns = showCover ? kTitleColumns : 1;
  const int titleLeft = l.titleRight - l.titleColumns * l.titleColumnWidth - (l.titleColumns - 1) * kColumnGap;
  l.authorWidth = renderer.getLineHeight(SMALL_FONT_ID);
  l.authorX = titleLeft - kRuleGap - l.authorWidth;
  l.progressWidth = std::max(kBarWidth, renderer.getTextWidth(SMALL_FONT_ID, "100%", EpdFontFamily::BOLD));
  l.progressX = l.authorX - kInfoGap - l.progressWidth;
  l.ruleX = l.progressX - kRuleGap;
  l.labelRight = l.ruleX - kRuleGap;
  l.spinesRight = l.labelRight - renderer.getLineHeight(SMALL_FONT_ID) - kRuleGap;
  // The cover spans the title, author and progress columns and takes at most 2/5 of the height.
  const int coverW = l.titleRight - l.progressX;
  const int coverH = std::min(coverW * 3 / 2, (l.bottom - l.top) * 2 / 5);
  l.textTop = l.top;
  if (showCover && coverH >= kMinCoverHeight) {
    const int width = std::min(coverW, coverH * 2 / 3 + 1);
    l.cover = Rect{l.titleRight - width, l.top, width, coverH};
    l.textTop = l.top + coverH + kCoverGap;
  }
  return l;
}

Rect tileRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  return Rect{content.x, metrics.homeTopPadding, content.width, metrics.homeCoverTileHeight};
}

int spineCount(const TategakiLayout& l, const Rect rect, const int books) {
  const int room = l.spinesRight - (rect.x + kSidePadding);
  const int fit = std::max(0, (room + kSpineGap) / (kSpineWidth + kSpineGap));
  return std::min({books, kSpineCount, fit});
}

Rect spineRect(const TategakiLayout& l, const int i) {
  // Lengths vary a little, as on a real shelf; the first book stands tallest.
  static constexpr int kShorten[kSpineCount] = {0, 40, 20, 60, 30};
  const int x = l.spinesRight - (i + 1) * kSpineWidth - i * kSpineGap;
  const int height = std::max(80, l.bottom - l.top - kShorten[i % kSpineCount]);
  return Rect{x, l.top, kSpineWidth, height};
}

// Thai marks and following vowels that cannot start a column.
bool isThaiTrailing(const uint32_t cp) {
  return cp == 0x0E30 || (cp >= 0x0E31 && cp <= 0x0E3A) || cp == 0x0E45 || (cp >= 0x0E47 && cp <= 0x0E4E);
}

// Thai leading vowels, written before the consonant they belong to: no break after one.
bool isThaiLeading(const uint32_t cp) { return cp >= 0x0E40 && cp <= 0x0E44; }

int prefixLength(const GfxRenderer& renderer, const int fontId, const char* start, const size_t bytes,
                 const EpdFontFamily::Style style) {
  return renderer.getTextWidth(fontId, std::string(start, bytes).c_str(), style);
}

// Splits text into up to maxColumns vertical columns no longer than maxLength. Unlike
// wrappedText() this breaks between characters, since Japanese and Thai titles have no
// spaces. The fitting length is found by binary search so that each column costs a few
// measurements (each may prepare SD fallback glyphs), not one per character. The last
// column is truncated with an ellipsis when the text does not fit.
std::vector<std::string> splitColumns(const GfxRenderer& renderer, const int fontId, const char* text,
                                      const int maxLength, const int maxColumns,
                                      const EpdFontFamily::Style style) {
  std::vector<std::string> columns;
  columns.reserve(maxColumns);
  const char* p = text;
  while (*p != '\0' && static_cast<int>(columns.size()) < maxColumns - 1) {
    while (*p == ' ') p++;
    const size_t remaining = strlen(p);
    if (renderer.getTextWidth(fontId, p, style) <= maxLength) break;
    // Longest prefix that fits, on a UTF-8 boundary.
    size_t lo = 0;
    size_t hi = remaining;
    while (lo < hi) {
      size_t mid = (lo + hi + 1) / 2;
      while (mid > lo && (static_cast<unsigned char>(p[mid]) & 0xC0) == 0x80) mid--;
      if (mid == lo) break;
      if (prefixLength(renderer, fontId, p, mid, style) <= maxLength) {
        lo = mid;
      } else {
        hi = mid - 1;
      }
    }
    const char* fit = p + lo;
    // Within it, prefer to end at a space; never before a Thai mark or after a Thai leading vowel.
    const char* cursor = p;
    const char* lastBreak = nullptr;
    const char* lastSpace = nullptr;
    uint32_t previous = 0;
    while (cursor < fit) {
      const char* next = cursor;
      const uint32_t cp = utf8NextCodepoint(reinterpret_cast<const unsigned char**>(&next));
      if (cursor != p && !isThaiTrailing(cp) && !isThaiLeading(previous)) {
        lastBreak = cursor;
        if (previous == ' ') lastSpace = cursor;
      }
      previous = cp;
      cursor = next;
    }
    if (*fit != '\0') {
      const char* next = fit;
      const uint32_t cp = utf8NextCodepoint(reinterpret_cast<const unsigned char**>(&next));
      if (fit != p && !isThaiTrailing(cp) && !isThaiLeading(previous)) lastBreak = fit;
    }
    const char* end = lastSpace != nullptr ? lastSpace : lastBreak;
    if (end == nullptr || end == p) end = fit;
    if (end == p) break;
    columns.emplace_back(p, static_cast<size_t>(end - p));
    p = end;
  }
  while (*p == ' ') p++;
  if (*p != '\0') columns.push_back(renderer.truncatedText(fontId, p, maxLength, style));
  return columns;
}

// Vertical run centered across a column of the given width starting at x.
void drawVerticalCentered(const GfxRenderer& renderer, const int fontId, const int x, const int width, const int y,
                          const char* text, const bool black,
                          const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int lineH = renderer.getLineHeight(fontId);
  renderer.drawTextVertical(fontId, x + (width + lineH) / 2, y, text, black, style);
}

void drawCurrentBook(const GfxRenderer& renderer, const TategakiLayout& l, const RecentBook& book) {
  if (l.cover.height > 0) ShelfTheme::drawCover(renderer, book, l.cover);
  const int height = l.bottom - l.textTop;
  const auto columns =
      splitColumns(renderer, kTitleFont, book.title.c_str(), height, l.titleColumns, EpdFontFamily::BOLD);
  int columnRight = l.titleRight;
  for (const auto& column : columns) {
    renderer.drawTextVertical(kTitleFont, columnRight, l.textTop, column.c_str(), true, EpdFontFamily::BOLD);
    columnRight -= l.titleColumnWidth + kColumnGap;
  }

  // Author in its own column, then the progress bar (filling downward) and percentage in the next.
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  if (!book.author.empty()) {
    const std::string author = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), height);
    drawVerticalCentered(renderer, SMALL_FONT_ID, l.authorX, l.authorWidth, l.textTop, author.c_str(), true);
  }
  const int barH = std::max(40, l.bottom - l.textTop - smallH - 6);
  const int barX = l.progressX + (l.progressWidth - kBarWidth) / 2;
  renderer.drawRect(barX, l.textTop, kBarWidth, barH, true);
  const int fill = (barH - 4) * std::clamp(static_cast<int>(book.progressPercent), 0, 100) / 100;
  if (fill > 0) renderer.fillRect(barX + 2, l.textTop + 2, kBarWidth - 4, fill, true);
  char percentText[8];
  snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(book.progressPercent));
  const int percentW = renderer.getTextWidth(SMALL_FONT_ID, percentText, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, l.progressX + (l.progressWidth - percentW) / 2, l.textTop + barH + 6, percentText,
                    true, EpdFontFamily::BOLD);
}

// Spine finishes and top marks. A book's pattern comes from its path, so it keeps its
// look wherever it sits on the shelf.
constexpr int kFinishCount = 6;
constexpr int kMarkCount = 6;

bool finishInverted(const int finish) { return finish == 1 || finish == 4; }

void drawFinish(const GfxRenderer& renderer, const Rect s, const int finish, const int capBottom,
                const int footTop) {
  switch (finish) {
    case 0:  // outlined
      renderer.drawRect(s.x, s.y, s.width, s.height, 2, true);
      break;
    case 1:  // black
      renderer.fillRect(s.x, s.y, s.width, s.height, true);
      break;
    case 2:  // light gray
      renderer.fillRectDither(s.x, s.y, s.width, s.height, Color::LightGray);
      renderer.drawRect(s.x, s.y, s.width, s.height, true);
      break;
    case 3:  // double rule
      renderer.drawRect(s.x, s.y, s.width, s.height, true);
      renderer.drawRect(s.x + 3, s.y + 3, s.width - 6, s.height - 6, true);
      break;
    case 4:  // black with white bands under the mark and above the foot
      renderer.fillRect(s.x, s.y, s.width, s.height, true);
      renderer.fillRect(s.x + 3, capBottom, s.width - 6, 3, false);
      renderer.fillRect(s.x + 3, footTop - 3, s.width - 6, 3, false);
      break;
    default:  // outlined with gray head and foot
      renderer.fillRectDither(s.x, s.y, s.width, capBottom - s.y, Color::LightGray);
      renderer.fillRectDither(s.x, footTop, s.width, s.y + s.height - footTop, Color::LightGray);
      renderer.drawRect(s.x, s.y, s.width, s.height, 2, true);
      break;
  }
}

void drawMark(const GfxRenderer& renderer, const int x, const int y, const int mark, const bool black) {
  constexpr int size = kMarkSize;
  const int cx = x + size / 2;
  const int cy = y + size / 2;
  switch (mark) {
    case 0:  // square
      renderer.fillRect(x, y, size, size, black);
      break;
    case 1:  // open square
      renderer.drawRect(x, y, size, size, 2, black);
      break;
    case 2:  // dot
      renderer.fillRoundedRect(x, y, size, size, size / 2, black ? Color::Black : Color::White);
      break;
    case 3:  // ring
      renderer.drawRoundedRect(x, y, size, size, 2, size / 2, black);
      break;
    case 4: {  // diamond
      const int xs[4] = {cx, x + size, cx, x};
      const int ys[4] = {y, cy, y + size, cy};
      renderer.fillPolygon(xs, ys, 4, black);
      break;
    }
    default:  // two bars
      renderer.fillRect(x, y + 2, size, 3, black);
      renderer.fillRect(x, y + size - 5, size, 3, black);
      break;
  }
}

unsigned spinePattern(const RecentBook& book) {
  return static_cast<unsigned>(std::hash<std::string>{}(book.path));
}

void drawSpine(const GfxRenderer& renderer, const Rect s, const RecentBook& book, const int finish, const int mark) {
  const bool inverted = finishInverted(finish);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  const int markY = s.y + kSpinePadding;
  const int capBottom = markY + kMarkSize + 6;
  const int percentY = s.y + s.height - kSpinePadding - smallH;
  const int footTop = percentY - 6;
  drawFinish(renderer, s, finish, capBottom, footTop);
  drawMark(renderer, s.x + (s.width - kMarkSize) / 2, markY, mark, !inverted);

  char percentText[8];
  snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(book.progressPercent));
  const int percentW = renderer.getTextWidth(SMALL_FONT_ID, percentText);
  renderer.drawText(SMALL_FONT_ID, s.x + (s.width - percentW) / 2, percentY, percentText, !inverted);

  const int titleTop = capBottom + 6;
  const int titleLength = footTop - 6 - titleTop;
  if (titleLength <= 0) return;
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), titleLength);
  drawVerticalCentered(renderer, UI_12_FONT_ID, s.x, s.width, titleTop, title.c_str(), !inverted);
}
}  // namespace

int TategakiTheme::homeCoverThumbHeight(const GfxRenderer& renderer) const {
  const TategakiLayout l =
      computeLayout(renderer, tileRect(renderer), menuTop(renderer, MENU_HEIGHT) - kSectionGap, showCover);
  return l.cover.height > 0 ? coverThumbHeight(l.cover) : -1;
}

bool TategakiTheme::homeBookColumns(const GfxRenderer& renderer, const Rect tile, const int count,
                                    HomeBookColumns& columns) const {
  const TategakiLayout l = computeLayout(renderer, tile, menuTop(renderer, MENU_HEIGHT) - kSectionGap, showCover);
  columns.count = spineCount(l, tile, count);
  const Rect leftmost = spineRect(l, std::max(1, columns.count) - 1);
  columns.left = leftmost.x;
  columns.step = kSpineWidth + kSpineGap;
  columns.width = kSpineWidth;
  columns.top = l.top;
  columns.bottom = l.bottom;
  columns.rightToLeft = true;
  return true;
}

void TategakiTheme::drawRecentBookCover(GfxRenderer& renderer, const Rect rect,
                                        const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                        bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                        std::function<bool()> storeCoverBuffer) const {
  // Spines are text and rectangles, quicker to draw than to snapshot, and the one cover
  // changes with the selection: no cover buffer is kept.
  (void)coverRendered;
  (void)coverBufferStored;
  (void)bufferRestored;
  (void)storeCoverBuffer;
  const TategakiLayout l = computeLayout(renderer, rect, menuTop(renderer, MENU_HEIGHT) - kSectionGap, showCover);
  const int count = spineCount(l, rect, static_cast<int>(recentBooks.size()));

  renderer.fillRect(l.ruleX, l.top, 2, l.bottom - l.top, true);
  drawVerticalCentered(renderer, SMALL_FONT_ID, l.labelRight - renderer.getLineHeight(SMALL_FONT_ID),
                       renderer.getLineHeight(SMALL_FONT_ID), l.top, tr(STR_RECENT_BOOKS), true);

  if (count == 0) {
    drawVerticalCentered(renderer, UI_12_FONT_ID, l.titleRight - l.titleColumnWidth, l.titleColumnWidth, l.top,
                         tr(STR_NO_OPEN_BOOK), true);
    return;
  }

  int previousFinish = -1;
  for (int i = 0; i < count; i++) {
    const unsigned pattern = spinePattern(recentBooks[i]);
    int finish = static_cast<int>(pattern % kFinishCount);
    // Neighbours never share a finish.
    if (finish == previousFinish) finish = (finish + 1) % kFinishCount;
    const int mark = static_cast<int>(pattern / kFinishCount % kMarkCount);
    drawSpine(renderer, spineRect(l, i), recentBooks[i], finish, mark);
    previousFinish = finish;
  }

  // The title shows the highlighted book, or the last one highlighted while the menu row has focus.
  if (selectorIndex >= 0 && selectorIndex < count) cardBookIndex = selectorIndex;
  drawCurrentBook(renderer, l, recentBooks[std::min(cardBookIndex, count - 1)]);

  if (selectorIndex >= 0 && selectorIndex < count) {
    const Rect s = spineRect(l, selectorIndex);
    renderer.drawRect(s.x - kSelectionInset, s.y - kSelectionInset, s.width + 2 * kSelectionInset,
                      s.height + 2 * kSelectionInset, 3, true);
  }
}

bool TategakiTheme::readerMenuSheet(ReaderMenuSheetStyle& style) const {
  style.radius = 0;
  style.rightToLeft = true;
  return true;
}
