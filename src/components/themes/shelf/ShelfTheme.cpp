#include "ShelfTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
constexpr int kSidePadding = 16;
constexpr int kSectionGap = 10;
constexpr int kCardPadding = 14;
constexpr int kFrameWidth = 2;
constexpr int kCoverCount = 4;
constexpr int kCoverGap = 12;
constexpr int kShelfLine = 6;  // black board under the covers, a gray one under that
constexpr int kSelectionInset = 4;
constexpr int kMinCardCoverHeight = 90;
constexpr int kBarHeight = 10;
constexpr int kMenuGap = 10;
constexpr int kIconSize = 32;
constexpr int kMenuBottomGap = 6;

struct ShelfLayout {
  Rect card;
  Rect cardCover;  // empty when the card has no room for a cover
  int shelfLabelY = 0;
  int coversTop = 0;
  int coverWidth = 0;
  int coverHeight = 0;
  int coversLeft = 0;
  int shelfY = 0;  // top of the black board
  int shelfX = 0;
  int shelfWidth = 0;
};

ShelfLayout computeLayout(const GfxRenderer& renderer, const Rect rect, const int bottomLimit) {
  ShelfLayout l;
  const int x = rect.x + kSidePadding;
  const int width = rect.width - 2 * kSidePadding;
  const int top = rect.y;
  const int bottom = std::min(rect.y + rect.height, bottomLimit);
  const int available = std::max(0, bottom - top);
  const int labelH = renderer.getLineHeight(SMALL_FONT_ID);

  // The shelf takes at most about half the height; the card gets the rest.
  l.coverWidth = (width - (kCoverCount - 1) * kCoverGap) / kCoverCount;
  const int shelfRoom = available * 45 / 100 - labelH - 6 - 2 * kShelfLine;
  l.coverHeight = std::max(40, std::min(l.coverWidth * 3 / 2, shelfRoom));
  l.coverWidth = std::min(l.coverWidth, l.coverHeight * 2 / 3 + 1);
  l.shelfX = rect.x + kSidePadding / 2;
  l.shelfWidth = rect.width - kSidePadding;
  l.shelfY = bottom - 2 * kShelfLine;
  l.coversTop = l.shelfY - l.coverHeight;
  l.coversLeft = x;
  l.shelfLabelY = l.coversTop - 6 - labelH - kSelectionInset;

  const int cardBottom = l.shelfLabelY - kSectionGap;
  l.card = Rect{x, top, width, std::max(0, cardBottom - top)};
  const int innerTop = l.card.y + kCardPadding + labelH + 6;
  const int coverH = std::min(260, l.card.y + l.card.height - kCardPadding - innerTop);
  if (coverH >= kMinCardCoverHeight) {
    l.cardCover = Rect{l.card.x + kCardPadding, innerTop, coverH * 2 / 3, coverH};
  }
  return l;
}

Rect tileRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  return Rect{content.x, metrics.homeTopPadding, content.width, metrics.homeCoverTileHeight};
}

Rect coverSlot(const ShelfLayout& l, const int i) {
  return Rect{l.coversLeft + i * (l.coverWidth + kCoverGap), l.coversTop, l.coverWidth, l.coverHeight};
}

// Thumbs fit a (0.6h, h) box: ask for one wide enough to fill the slot and crop the height.
int thumbHeightFor(const Rect slot) { return std::max(slot.height, slot.width * 5 / 3 + 2); }

bool drawThumb(const GfxRenderer& renderer, const RecentBook& book, const Rect slot, const int thumbHeight) {
  if (book.coverBmpPath.empty()) return false;
  HalFile file;
  const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
  if (!Storage.openFileForRead("HOME", thumbPath, file)) return false;
  Bitmap bitmap(file);
  return bitmap.parseHeaders() == BmpReaderError::Ok && BaseTheme::drawCoverThumbFill(renderer, bitmap, slot);
}

void drawPlaceholder(const GfxRenderer& renderer, const Rect slot) {
  renderer.fillRectDither(slot.x, slot.y, slot.width, slot.height, Color::LightGray);
  renderer.drawIcon(CoverIcon, slot.x + (slot.width - 32) / 2, slot.y + (slot.height - 32) / 2, 32);
}

void drawBar(const GfxRenderer& renderer, const int x, const int y, const int width, const int percent,
             const int radius) {
  const int r = radius > 0 ? kBarHeight / 2 : 0;
  renderer.drawRoundedRect(x, y, width, kBarHeight, 1, r, true);
  const int fill = (width - 4) * std::clamp(percent, 0, 100) / 100;
  if (fill > 0) renderer.fillRoundedRect(x + 2, y + 2, fill, kBarHeight - 4, r > 0 ? r - 2 : 0, Color::Black);
}

void drawShelf(const GfxRenderer& renderer, const ShelfLayout& l) {
  renderer.fillRect(l.shelfX, l.shelfY, l.shelfWidth, kShelfLine, true);
  renderer.fillRectDither(l.shelfX + 4, l.shelfY + kShelfLine, l.shelfWidth - 8, kShelfLine, Color::DarkGray);
}

// Drawn on every frame (outside the cached snapshot): it follows the highlighted cover.
void drawCard(const GfxRenderer& renderer, const ShelfLayout& l, const RecentBook* book, const int radius,
              const int thumbHeight) {
  const Rect& card = l.card;
  if (card.height <= 0) return;
  renderer.fillRect(card.x, card.y, card.width, card.height, false);
  renderer.drawRoundedRect(card.x, card.y, card.width, card.height, kFrameWidth, radius, true);
  const int labelH = renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, card.x + kCardPadding, card.y + kCardPadding, tr(STR_CONTINUE_READING), true,
                    EpdFontFamily::BOLD);

  if (book == nullptr) {
    const std::string text = renderer.truncatedText(UI_12_FONT_ID, tr(STR_NO_OPEN_BOOK), card.width - 2 * kCardPadding);
    const int textX = card.x + (card.width - renderer.getTextWidth(UI_12_FONT_ID, text.c_str())) / 2;
    renderer.drawText(UI_12_FONT_ID, textX, card.y + card.height / 2, text.c_str(), true);
    return;
  }

  int textX = card.x + kCardPadding;
  if (l.cardCover.height > 0) {
    if (!drawThumb(renderer, *book, l.cardCover, thumbHeight)) drawPlaceholder(renderer, l.cardCover);
    renderer.drawRect(l.cardCover.x, l.cardCover.y, l.cardCover.width, l.cardCover.height, true);
    textX = l.cardCover.x + l.cardCover.width + kCardPadding;
  }
  const int textW = card.x + card.width - kCardPadding - textX;
  const int top = card.y + kCardPadding + labelH + 6;
  const int bottom = card.y + card.height - kCardPadding;
  if (textW <= 20) return;

  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  // Progress sits at the bottom; the title takes the lines that fit above the author.
  const int barY = bottom - kBarHeight;
  const int percentY = barY - 6 - smallH;
  const int titleRoom = percentY - top - (book->author.empty() ? 0 : smallH + 4) - 8;
  const int maxLines = std::clamp(titleRoom / titleH, 1, 4);
  const auto lines = renderer.wrappedText(UI_12_FONT_ID, book->title.c_str(), textW, maxLines, EpdFontFamily::BOLD);
  int y = top;
  for (const auto& line : lines) {
    renderer.drawText(UI_12_FONT_ID, textX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleH;
  }
  if (!book->author.empty()) {
    const std::string author = renderer.truncatedText(SMALL_FONT_ID, book->author.c_str(), textW);
    renderer.drawText(SMALL_FONT_ID, textX, y + 4, author.c_str(), true);
  }

  char percentText[8];
  snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(book->progressPercent));
  renderer.drawText(SMALL_FONT_ID, textX + textW - renderer.getTextWidth(SMALL_FONT_ID, percentText), percentY,
                    percentText, true, EpdFontFamily::BOLD);
  drawBar(renderer, textX, barY, textW, book->progressPercent, radius);
}

const uint8_t* iconFor(const UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Blocks:
    case UIIcon::Plugins:
      return BlocksIcon;
    default:
      return nullptr;
  }
}
}  // namespace

int ShelfTheme::coverThumbHeight(const Rect slot) { return thumbHeightFor(slot); }

void ShelfTheme::drawCover(const GfxRenderer& renderer, const RecentBook& book, const Rect slot) {
  if (!drawThumb(renderer, book, slot, thumbHeightFor(slot))) drawPlaceholder(renderer, slot);
  renderer.drawRect(slot.x, slot.y, slot.width, slot.height, true);
}

int ShelfTheme::menuTop(const GfxRenderer& renderer, const int menuHeight) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int belowTile = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int aboveHints = renderer.getScreenHeight() - metrics.buttonHintsHeight - kMenuBottomGap - menuHeight;
  return std::min(belowTile, aboveHints);
}

int ShelfTheme::homeCoverThumbHeight(const GfxRenderer& renderer) const {
  const ShelfLayout l = computeLayout(renderer, tileRect(renderer), menuTop(renderer, MENU_HEIGHT) - kSectionGap);
  return thumbHeightFor(coverSlot(l, 0));
}

int ShelfTheme::homeExtraCoverThumbHeights(const GfxRenderer& renderer, int* heights, const int capacity) const {
  const ShelfLayout l = computeLayout(renderer, tileRect(renderer), menuTop(renderer, MENU_HEIGHT) - kSectionGap);
  if (capacity < 1 || l.cardCover.height <= 0) return 0;
  heights[0] = thumbHeightFor(l.cardCover);
  return 1;
}

bool ShelfTheme::homeSnapshotBand(const GfxRenderer& renderer, const Rect tile, int& top, int& bottom) const {
  // Only the shelf is snapshotted: the card above it is redrawn on every frame anyway.
  const ShelfLayout l = computeLayout(renderer, tile, menuTop(renderer, MENU_HEIGHT) - kSectionGap);
  top = l.shelfLabelY;
  bottom = l.shelfY + 2 * kShelfLine;
  return true;
}

bool ShelfTheme::homeBookColumns(const GfxRenderer& renderer, const Rect tile, const int count,
                                 HomeBookColumns& columns) const {
  const ShelfLayout l = computeLayout(renderer, tile, menuTop(renderer, MENU_HEIGHT) - kSectionGap);
  columns.left = l.coversLeft;
  columns.step = l.coverWidth + kCoverGap;
  columns.width = l.coverWidth;
  columns.count = std::min(count, kCoverCount);
  columns.top = l.coversTop;
  columns.bottom = l.shelfY + 2 * kShelfLine;
  columns.rightToLeft = false;
  return true;
}

void ShelfTheme::drawRecentBookCover(GfxRenderer& renderer, const Rect rect, const std::vector<RecentBook>& recentBooks,
                                     const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                     bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  (void)bufferRestored;
  const ShelfLayout l = computeLayout(renderer, rect, menuTop(renderer, MENU_HEIGHT) - kSectionGap);
  const int count = std::min(static_cast<int>(recentBooks.size()), kCoverCount);
  const int shelfThumb = thumbHeightFor(coverSlot(l, 0));

  // Shelf, label and covers are drawn once; later frames restore the snapshot.
  if (!coverRendered) {
    renderer.drawText(SMALL_FONT_ID, l.coversLeft, l.shelfLabelY, tr(STR_BOOKSHELF), true, EpdFontFamily::BOLD);
    for (int i = 0; i < count; i++) {
      const Rect slot = coverSlot(l, i);
      if (!drawThumb(renderer, recentBooks[i], slot, shelfThumb)) drawPlaceholder(renderer, slot);
      renderer.drawRect(slot.x, slot.y, slot.width, slot.height, true);
    }
    drawShelf(renderer, l);
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // The card shows the highlighted book, or the last one highlighted while the menu row has focus.
  if (selectorIndex >= 0 && selectorIndex < count) cardBookIndex = selectorIndex;
  const RecentBook* book = count > 0 ? &recentBooks[std::min(cardBookIndex, count - 1)] : nullptr;
  drawCard(renderer, l, book, cornerRadius(), thumbHeightFor(l.cardCover));

  if (selectorIndex >= 0 && selectorIndex < count) {
    const Rect slot = coverSlot(l, selectorIndex);
    renderer.drawRoundedRect(slot.x - kSelectionInset, slot.y - kSelectionInset, slot.width + 2 * kSelectionInset,
                             slot.height + 2 * kSelectionInset, 3, cornerRadius() > 0 ? 6 : 0, true);
  }
}

bool ShelfTheme::homeMenuGrid(const GfxRenderer& renderer, const Rect rect, const int buttonCount,
                              HomeMenuGrid& grid) const {
  const int width = rect.width - 2 * kSidePadding;
  const int columns = std::max(1, buttonCount);
  grid.columns = columns;
  grid.left = rect.x + kSidePadding;
  grid.top = std::min(rect.y, menuTop(renderer, MENU_HEIGHT));
  grid.colStep = (width + kMenuGap) / columns;
  grid.tileWidth = grid.colStep - kMenuGap;
  grid.tileHeight = MENU_HEIGHT;
  grid.rowStep = MENU_HEIGHT;
  return true;
}

void ShelfTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount, const int selectedIndex,
                                const std::function<std::string(int index)>& buttonLabel,
                                const std::function<UIIcon(int index)>& rowIcon) const {
  HomeMenuGrid grid;
  homeMenuGrid(renderer, rect, buttonCount, grid);
  const int radius = cornerRadius();
  const int labelH = renderer.getLineHeight(SMALL_FONT_ID);
  const int contentH = kIconSize + 4 + labelH;

  for (int i = 0; i < buttonCount; i++) {
    const int x = grid.left + i * grid.colStep;
    const bool selected = selectedIndex == i;
    if (selected) {
      renderer.fillRoundedRect(x, grid.top, grid.tileWidth, grid.tileHeight, radius, Color::Black);
    } else {
      renderer.drawRoundedRect(x, grid.top, grid.tileWidth, grid.tileHeight, kFrameWidth, radius, true);
    }
    const int top = grid.top + (grid.tileHeight - contentH) / 2;
    if (rowIcon != nullptr) {
      if (const uint8_t* icon = iconFor(rowIcon(i))) {
        renderer.drawIcon(icon, x + (grid.tileWidth - kIconSize) / 2, top, kIconSize, !selected);
      }
    }
    if (buttonLabel != nullptr) {
      const std::string label = renderer.truncatedText(SMALL_FONT_ID, buttonLabel(i).c_str(), grid.tileWidth - 6);
      const int labelX = x + (grid.tileWidth - renderer.getTextWidth(SMALL_FONT_ID, label.c_str())) / 2;
      renderer.drawText(SMALL_FONT_ID, labelX, top + kIconSize + 4, label.c_str(), !selected);
    }
  }
}

bool ShelfTheme::readerMenuSheet(ReaderMenuSheetStyle& style) const {
  style.radius = 14;
  style.rightToLeft = false;
  return true;
}
