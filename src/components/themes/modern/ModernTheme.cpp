#include "ModernTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/bookmark.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {
constexpr int cornerRadius = 6;
constexpr int iconSize = 32;
constexpr int mainMenuColumns = 2;

// --- Carousel geometry (mockup CSS px x2) ---
constexpr int carouselHeight = 340;  // band the covers are centred in; the info block follows
constexpr int centerCoverWidth = 180;
constexpr int sideCoverWidth = 140;
constexpr int farCoverWidth = 100;
constexpr int sideCoverInset = 64;  // side cover distance from the screen edge
constexpr int coverBorder = 2;
constexpr int centerCoverHalo = 5;  // white ring that lifts the centre cover off its neighbours

// --- Info block below the carousel: fixed offsets so nothing shifts between books ---
constexpr int titleOffset = 4;
constexpr int authorOffset = 32;
constexpr int progressOffset = 76;
constexpr int dotsOffset = 94;
constexpr int dotDiameter = 6;
constexpr int infoPillPadV = 14;

// The centre book persists while the selection is down in the menu grid.
int lastCenterIndex = 0;

const uint8_t* iconForName(const UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    case UIIcon::Bookmark:
      return BookmarkIcon;
    case UIIcon::Blocks:
      return BlocksIcon;
    default:
      return nullptr;
  }
}

// Small solid arrow centred at (cx, cy), pointing left (dir < 0) or right (dir > 0).
void drawArrow(const GfxRenderer& renderer, const int cx, const int cy, const int size, const int dir) {
  const int tipX = cx + dir * size / 2;
  const int baseX = cx - dir * size / 2;
  const int xPoints[] = {tipX, baseX, baseX};
  const int yPoints[] = {cy, cy - size / 2, cy + size / 2};
  renderer.fillPolygon(xPoints, yPoints, 3, true);
}

// Draws one cover as a rounded, framed card whose width follows the cover's own aspect
// ratio, horizontally centred on anchorX. Thumbnails are generated at exactly targetHeight
// (see ModernMetrics), so the bitmap is drawn 1:1 and only cropped if it is oversized.
void drawCoverCard(const GfxRenderer& renderer, const RecentBook& book, const int anchorX, const int topY,
                   const int targetHeight, const int fallbackWidth, const int outlineWidth, const bool withHalo) {
  int displayWidth = fallbackWidth;
  bool hasCover = false;
  HalFile file;
  // The Bitmap reads through `file`, so both stay alive until the pixels are drawn.
  const bool opened =
      !book.coverBmpPath.empty() &&
      Storage.openFileForRead("HOME", UITheme::getCoverThumbPath(book.coverBmpPath, targetHeight), file);
  Bitmap bitmap(file);
  if (opened && bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0) {
    // Never wider than tall: a landscape cover is cropped rather than allowed to cover its neighbours.
    displayWidth = std::clamp(bitmap.getWidth(), 1, targetHeight);
    hasCover = true;
  }

  const int cardX = anchorX - displayWidth / 2 - coverBorder;
  const int cardY = topY - coverBorder;
  const int cardW = displayWidth + coverBorder * 2;
  const int cardH = targetHeight + coverBorder * 2;
  const Rect content{cardX + coverBorder, cardY + coverBorder, displayWidth, targetHeight};

  if (withHalo) {
    renderer.fillRoundedRect(cardX - centerCoverHalo, cardY - centerCoverHalo, cardW + centerCoverHalo * 2,
                             cardH + centerCoverHalo * 2, cornerRadius + centerCoverHalo / 2, Color::White);
  }
  renderer.fillRoundedRect(cardX, cardY, cardW, cardH, cornerRadius, Color::Black);
  renderer.fillRect(content.x, content.y, content.width, content.height, false);

  if (hasCover) {
    BaseTheme::drawCoverThumbFill(renderer, bitmap, content);
  } else {
    renderer.fillRectDither(content.x, content.y, content.width, content.height, Color::LightGray);
    if (content.width >= iconSize && content.height >= iconSize * 2) {
      renderer.drawIcon(CoverIcon, content.x + (content.width - iconSize) / 2, content.y + content.height / 3,
                        iconSize);
    }
  }
  renderer.drawRoundedRect(cardX, cardY, cardW, cardH, outlineWidth, cornerRadius, true);
}
}  // namespace

// ============================================================
// Button hints: four equal buttons under a hairline; Left/Right shown as arrows
// ============================================================

void ModernTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                  const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation originalOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  constexpr int sidePadding = 16;
  constexpr int verticalPadding = 6;
  constexpr int buttonGap = 6;
  constexpr int stubHeight = 15;
  constexpr int arrowSize = 10;
  constexpr int areaHeight = ModernMetrics::values.buttonHintsHeight;

  const int pageHeight = renderer.getScreenHeight();
  const int screenWidth = renderer.getScreenWidth();
  const int buttonWidth = (screenWidth - sidePadding * 2 - buttonGap * 3) / 4;
  const int buttonHeight = areaHeight - verticalPadding * 2;
  const int areaTop = pageHeight - areaHeight;
  const int buttonTop = areaTop + verticalPadding;

  const char* labels[] = {btn1, btn2, btn3, btn4};
  const char* leftLabel = tr(STR_DIR_LEFT);
  const char* rightLabel = tr(STR_DIR_RIGHT);
  // Over a grayscale reader page only the button silhouettes are cleared (see LyraTheme).
  const bool grayscale = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();

  if (!grayscale) {
    renderer.drawLine(0, areaTop, screenWidth - 1, areaTop, true);
  }

  for (int i = 0; i < 4; i++) {
    const int x = sidePadding + i * (buttonWidth + buttonGap);
    const bool hasLabel = labels[i] != nullptr && labels[i][0] != '\0';
    const int top = hasLabel ? buttonTop : pageHeight - stubHeight;
    const int height = hasLabel ? buttonHeight : stubHeight;

    renderer.fillRoundedRect(x, top, buttonWidth, height, cornerRadius, grayscale ? Color::Black : Color::White);
    if (grayscale) continue;
    renderer.drawRoundedRect(x, top, buttonWidth, height, 1, cornerRadius, true, true, false, false, true);
    if (!hasLabel) continue;

    if (strcmp(labels[i], leftLabel) == 0) {
      drawArrow(renderer, x + buttonWidth / 2, top + height / 2, arrowSize, -1);
    } else if (strcmp(labels[i], rightLabel) == 0) {
      drawArrow(renderer, x + buttonWidth / 2, top + height / 2, arrowSize, +1);
    } else {
      // One line only: the button is too short for the two-line wrap drawHintLabel does.
      constexpr int textPadding = 4;
      const std::string text = renderer.truncatedText(SMALL_FONT_ID, labels[i], buttonWidth - textPadding * 2);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, text.c_str());
      renderer.drawText(SMALL_FONT_ID, x + (buttonWidth - textWidth) / 2,
                        top + (height - renderer.getLineHeight(SMALL_FONT_ID)) / 2, text.c_str());
    }
  }

  renderer.setOrientation(originalOrientation);
}

// The Modern theme shows no side button hints.
void ModernTheme::drawSideButtonHints(const GfxRenderer&, const char*, const char*) const {}

// ============================================================
// Home menu: two-column grid of icon tiles, black fill for the selection
// ============================================================

bool ModernTheme::homeMenuGrid(const GfxRenderer& renderer, const Rect rect, const int buttonCount,
                               HomeMenuGrid& grid) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int gap = metrics.menuSpacing;
  const int totalWidth = rect.width - metrics.contentSidePadding * 2;
  const int rows = std::max(1, (buttonCount + mainMenuColumns - 1) / mainMenuColumns);

  // The rect HomeActivity passes can run past the bottom of the screen; lay out only in
  // the space above the button hints.
  const int onScreenHeight = std::max(0, renderer.getScreenHeight() - rect.y - metrics.buttonHintsHeight);
  const int availableHeight = std::min(rect.height, onScreenHeight);

  // Rows grow to use spare height, up to 1.5x the nominal tile, then the grid is centred.
  const int perRow = (availableHeight - (rows - 1) * gap) / rows;
  const int tileHeight = std::clamp(perRow, metrics.menuRowHeight, metrics.menuRowHeight * 3 / 2);
  const int gridHeight = rows * tileHeight + (rows - 1) * gap;

  grid.columns = mainMenuColumns;
  grid.tileWidth = (totalWidth - gap * (mainMenuColumns - 1)) / mainMenuColumns;
  grid.tileHeight = tileHeight;
  grid.colStep = grid.tileWidth + gap;
  grid.rowStep = tileHeight + gap;
  grid.left = rect.x + metrics.contentSidePadding;
  grid.top = rect.y + std::max(0, (availableHeight - gridHeight) / 2);
  return true;
}

void ModernTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount, const int selectedIndex,
                                 const std::function<std::string(int index)>& buttonLabel,
                                 const std::function<UIIcon(int index)>& rowIcon) const {
  HomeMenuGrid grid;
  homeMenuGrid(renderer, rect, buttonCount, grid);

  constexpr int iconTextGap = 6;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  for (int i = 0; i < buttonCount; ++i) {
    const int tileX = grid.left + (i % grid.columns) * grid.colStep;
    const int tileY = grid.top + (i / grid.columns) * grid.rowStep;
    // selectedIndex is negative while the selection is up in the carousel.
    const bool selected = selectedIndex == i;

    renderer.fillRoundedRect(tileX, tileY, grid.tileWidth, grid.tileHeight, cornerRadius, Color::Black);
    if (!selected) {
      // Unselected: a 2 px outline, made by punching a white interior out of the black tile.
      renderer.fillRoundedRect(tileX + 2, tileY + 2, grid.tileWidth - 4, grid.tileHeight - 4, cornerRadius - 2,
                               Color::White);
    }

    // Icon and label are centred in the tile as one block.
    const int blockHeight = iconSize + iconTextGap + lineHeight;
    const int iconY = tileY + (grid.tileHeight - blockHeight) / 2;
    if (rowIcon != nullptr) {
      if (const uint8_t* iconBitmap = iconForName(rowIcon(i))) {
        renderer.drawIcon(iconBitmap, tileX + (grid.tileWidth - iconSize) / 2, iconY, iconSize, !selected);
      }
    }

    const std::string label = renderer.truncatedText(UI_10_FONT_ID, buttonLabel(i).c_str(), grid.tileWidth - 12);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
    renderer.drawText(UI_10_FONT_ID, tileX + (grid.tileWidth - textWidth) / 2, iconY + iconSize + iconTextGap,
                      label.c_str(), !selected);
  }
}

// ============================================================
// Home carousel: far, side and centre covers drawn back to front, then the
// centre book's title, author, progress and the position dots.
// ============================================================

// The recents list is re-ordered whenever a book is opened, so an index remembered from
// the previous visit would centre a different book. Each visit starts on the most recent.
void ModernTheme::onHomeEnter() const { lastCenterIndex = 0; }

int ModernTheme::homeExtraCoverThumbHeights(int* heights, const int capacity) const {
  constexpr int extras[] = {ModernMetrics::sideCoverHeight, ModernMetrics::farCoverHeight};
  const int count = std::min(capacity, static_cast<int>(std::size(extras)));
  std::copy_n(extras, count, heights);
  return count;
}

void ModernTheme::drawRecentBookCover(GfxRenderer& renderer, const Rect rect,
                                      const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                      bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                      std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const bool bookIsSelected = selectorIndex >= 0 && selectorIndex < bookCount;
  if (bookIsSelected) {
    lastCenterIndex = selectorIndex;
  }
  const int centerIndex = std::min(lastCenterIndex, bookCount - 1);
  const RecentBook& book = recentBooks[centerIndex];

  const int centerX = rect.x + rect.width / 2;
  const int centerY = rect.y + (carouselHeight - ModernMetrics::centerCoverHeight) / 2;

  // The covers are the expensive part (SD reads). HomeActivity snapshots the tile once
  // they are drawn and restores it on later frames; a restored buffer already holds every
  // cover layer, so only the info block below is redrawn.
  if (!bufferRestored) {
    if (bookCount > 2) {
      const int farY = centerY + (ModernMetrics::centerCoverHeight - ModernMetrics::farCoverHeight) / 2;
      const int inset = ModernMetrics::values.contentSidePadding + farCoverWidth / 2;
      drawCoverCard(renderer, recentBooks[(centerIndex - 2 + bookCount) % bookCount], rect.x + inset, farY,
                    ModernMetrics::farCoverHeight, farCoverWidth, coverBorder, false);
      drawCoverCard(renderer, recentBooks[(centerIndex + 2) % bookCount], rect.x + rect.width - inset, farY,
                    ModernMetrics::farCoverHeight, farCoverWidth, coverBorder, false);
    }
    if (bookCount > 1) {
      const int sideY = centerY + (ModernMetrics::centerCoverHeight - ModernMetrics::sideCoverHeight) / 2;
      const int inset = sideCoverInset + sideCoverWidth / 2;
      drawCoverCard(renderer, recentBooks[(centerIndex - 1 + bookCount) % bookCount], rect.x + inset, sideY,
                    ModernMetrics::sideCoverHeight, sideCoverWidth, coverBorder, false);
      drawCoverCard(renderer, recentBooks[(centerIndex + 1) % bookCount], rect.x + rect.width - inset, sideY,
                    ModernMetrics::sideCoverHeight, sideCoverWidth, coverBorder, false);
    }
    // A heavier outline marks the centre cover while the selection is on the carousel.
    drawCoverCard(renderer, book, centerX, centerY, ModernMetrics::centerCoverHeight, centerCoverWidth,
                  bookIsSelected ? 3 : coverBorder, true);

    if (!coverRendered) {
      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;
    }
  }

  // --- Info pill: black with white text while a book is selected, outlined otherwise ---
  const int infoTop = rect.y + carouselHeight;
  const int sidePadding = ModernMetrics::values.contentSidePadding;
  const int pillX = rect.x + sidePadding;
  const int pillY = infoTop + titleOffset - infoPillPadV;
  const int pillW = rect.width - sidePadding * 2;
  const int pillH = dotsOffset + dotDiameter + infoPillPadV - titleOffset + infoPillPadV;
  renderer.fillRoundedRect(pillX, pillY, pillW, pillH, cornerRadius, Color::Black);
  if (!bookIsSelected) {
    renderer.fillRoundedRect(pillX + 2, pillY + 2, pillW - 4, pillH - 4, cornerRadius - 2, Color::White);
  }
  const bool textBlack = !bookIsSelected;
  const int textWidthLimit = rect.width - 40 * 2;

  const std::string title = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), textWidthLimit);
  renderer.drawText(UI_10_FONT_ID, centerX - renderer.getTextWidth(UI_10_FONT_ID, title.c_str()) / 2,
                    infoTop + titleOffset, title.c_str(), textBlack);

  if (!book.author.empty()) {
    const std::string author = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), textWidthLimit);
    renderer.drawText(SMALL_FONT_ID, centerX - renderer.getTextWidth(SMALL_FONT_ID, author.c_str()) / 2,
                      infoTop + authorOffset, author.c_str(), textBlack);
  }

  if (book.progressPercent > 0) {
    constexpr int barHeight = 5;
    constexpr int barWidth = 240;
    constexpr int percentGap = 12;
    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%d%%", book.progressPercent);
    // Reserve room for "100%" so the bar does not shift as the number grows.
    const int percentSlot = renderer.getTextWidth(SMALL_FONT_ID, "100%");
    const int barX = centerX - (barWidth + percentGap + percentSlot) / 2;
    const int barY = infoTop + progressOffset;

    renderer.fillRoundedRect(barX, barY, barWidth, barHeight, barHeight / 2,
                             bookIsSelected ? Color::DarkGray : Color::LightGray);
    renderer.fillRoundedRect(barX, barY, std::max(barHeight, barWidth * book.progressPercent / 100), barHeight,
                             barHeight / 2, bookIsSelected ? Color::White : Color::Black);
    const int percentWidth = renderer.getTextWidth(SMALL_FONT_ID, percentText);
    renderer.drawText(SMALL_FONT_ID, barX + barWidth + percentGap + (percentSlot - percentWidth),
                      barY + (barHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2, percentText, textBlack);
  }

  // --- Position dots: the active one is a short bar ---
  if (bookCount > 1) {
    constexpr int dotGap = 8;
    constexpr int activeDotWidth = 16;
    const int dotCount = std::min(bookCount, 7);
    const bool previewsRight = SETTINGS.previewDirection == CrossPointSettings::PREVIEW_RIGHT;
    const int position = centerIndex % dotCount;
    const int activeDot = previewsRight ? dotCount - 1 - position : position;
    int dotX = centerX - ((dotCount - 1) * (dotDiameter + dotGap) + activeDotWidth) / 2;
    const int dotY = infoTop + dotsOffset;

    for (int i = 0; i < dotCount; i++) {
      const bool active = i == activeDot;
      const int width = active ? activeDotWidth : dotDiameter;
      const Color color = active ? (bookIsSelected ? Color::White : Color::Black)
                                 : (bookIsSelected ? Color::DarkGray : Color::LightGray);
      renderer.fillRoundedRect(dotX, dotY, width, dotDiameter, dotDiameter / 2, color);
      dotX += width + dotGap;
    }
  }

  // --- Hairline between the info pill and the menu grid, nudged down toward the grid ---
  const int pillBottom = pillY + pillH;
  const int dividerY = (pillBottom + rect.y + rect.height) / 2 + 18;
  const int dividerInset = sidePadding + 18;
  renderer.drawLine(rect.x + dividerInset, dividerY, rect.x + rect.width - dividerInset, dividerY, true);
}
