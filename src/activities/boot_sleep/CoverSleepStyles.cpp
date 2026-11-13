#include "CoverSleepStyles.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace {
constexpr int TOP_MARGIN = 56;
constexpr int BOTTOM_MARGIN = 32;
constexpr int SIDE_MARGIN = 32;
// Gallery: a 2 px white frame 10 px outside the cover.
constexpr int GALLERY_FRAME_GAP = 12;
constexpr int GALLERY_FRAME_LINE = 2;
constexpr int GALLERY_DOTS = 10;
constexpr int GALLERY_DOT = 6;
constexpr int GALLERY_DOT_GAP = 10;
// Backdrop: an 8 px white mat with a 1 px black edge around the cover.
constexpr int BACKDROP_MAT = 9;
constexpr int CARD_PADDING = 14;
constexpr int CARD_RADIUS = 12;
// Info Band: a 4 px black rule between the cover and the band.
constexpr int BAND_RULE = 4;
constexpr int BAND_PADDING = 24;
constexpr int BAND_SEGMENTS = 20;
constexpr int BAND_SEGMENT_GAP = 3;
constexpr int BAND_BAR_HEIGHT = 10;
constexpr int PROGRESS_BAR_HEIGHT = 6;
constexpr int COLUMN_GAP = 12;
// Vertical gaps between text lines, used both to measure a layout and to draw it.
constexpr int GAP_UNDER_COVER = 28;
constexpr int GAP_SMALL = 2;
constexpr int GAP_LINE = 4;
constexpr int GAP_BEFORE_PROGRESS = 14;
constexpr int GAP_BEFORE_FOOTER = 6;
constexpr int GALLERY_SIDE = 40;
constexpr int GALLERY_FOOTER_GAP = 16;
constexpr int BACKDROP_CARD_GAP = 32;
constexpr int VERTICAL_COVER_GAP = 20;
constexpr int VERTICAL_BAR_GAP = 24;
constexpr int PERCENT_GAP = 16;

void fitCover(const Bitmap& cover, CoverSleepRect& rect) {
  bitmapDrawnSize(cover, rect.maxWidth, rect.maxHeight, rect.cropX, rect.cropY, rect.width, rect.height);
}

std::string percentText(const int percent) { return std::to_string(percent) + "%"; }

// "chapter · 42%", either part when the other is missing.
std::string chapterAndPercent(const CoverSleepInfo& info) {
  std::string text = info.chapter;
  if (info.percent >= 0) {
    if (!text.empty()) text += " \xC2\xB7 ";
    text += percentText(info.percent);
  }
  return text;
}

// Info Band: the line under the title.
std::string authorAndChapter(const CoverSleepInfo& info) {
  std::string text = info.author;
  if (!info.chapter.empty()) {
    if (!text.empty()) text += " \xC2\xB7 ";
    text += info.chapter;
  }
  return text;
}

std::vector<std::string> galleryTitleLines(const GfxRenderer& renderer, const CoverSleepInfo& info) {
  if (info.title.empty()) return {};
  return renderer.wrappedText(UI_12_FONT_ID, info.title.c_str(), renderer.getScreenWidth() - SIDE_MARGIN * 2, 2,
                              EpdFontFamily::BOLD);
}

// Height of the Gallery caption under the frame.
int galleryCaptionHeight(const GfxRenderer& renderer, const CoverSleepInfo& info) {
  int height = GAP_UNDER_COVER +
               static_cast<int>(galleryTitleLines(renderer, info).size()) * renderer.getLineHeight(UI_12_FONT_ID);
  if (!info.author.empty()) height += GAP_SMALL + renderer.getLineHeight(UI_10_FONT_ID);
  if (!info.chapter.empty()) height += GAP_LINE + renderer.getLineHeight(SMALL_FONT_ID);
  if (info.percent >= 0) height += GAP_BEFORE_PROGRESS + GALLERY_DOT;
  return height;
}

int backdropCardHeight(const GfxRenderer& renderer, const CoverSleepInfo& info) {
  int height = CARD_PADDING * 2 + renderer.getLineHeight(UI_12_FONT_ID);
  if (!chapterAndPercent(info).empty()) height += GAP_SMALL + renderer.getLineHeight(SMALL_FONT_ID);
  return height;
}

int bandHeight(const GfxRenderer& renderer, const CoverSleepInfo& info) {
  // The footer ("Sleeping") sits on the bottom padding.
  int height = BAND_PADDING * 2 + renderer.getLineHeight(UI_12_FONT_ID) + GAP_BEFORE_FOOTER +
               renderer.getLineHeight(SMALL_FONT_ID);
  if (!authorAndChapter(info).empty()) height += GAP_LINE + renderer.getLineHeight(UI_10_FONT_ID);
  if (info.percent >= 0) height += GAP_BEFORE_PROGRESS + BAND_BAR_HEIGHT;
  return height;
}

// Vertical: the text columns from the right edge, title first (up to two columns).
struct VerticalColumn {
  int fontId;
  EpdFontFamily::Style style;
  int x;  // right edge; the column occupies (x - lineHeight, x]
  int y;
  std::string text;
};

std::vector<VerticalColumn> verticalColumns(const GfxRenderer& renderer, const CoverSleepInfo& info) {
  std::vector<VerticalColumn> columns;
  const int bottom = renderer.getScreenHeight() - BOTTOM_MARGIN;
  int x = renderer.getScreenWidth() - SIDE_MARGIN;
  const auto add = [&](const int fontId, const EpdFontFamily::Style style, const int y, const std::string& text) {
    if (text.empty() || y >= bottom) return;
    columns.push_back({fontId, style, x, y, renderer.truncatedText(fontId, text.c_str(), bottom - y, style)});
    x -= renderer.getLineHeight(fontId) + COLUMN_GAP;
  };
  const int titleLine = renderer.getLineHeight(UI_12_FONT_ID);
  if (!info.title.empty()) {
    for (const auto& line :
         renderer.wrappedText(UI_12_FONT_ID, info.title.c_str(), bottom - TOP_MARGIN, 2, EpdFontFamily::BOLD)) {
      add(UI_12_FONT_ID, EpdFontFamily::BOLD, TOP_MARGIN, line);
    }
  }
  // Author and chapter start lower, like a book's spine label.
  add(UI_10_FONT_ID, EpdFontFamily::REGULAR, TOP_MARGIN + titleLine * 2, info.author);
  add(SMALL_FONT_ID, EpdFontFamily::REGULAR, TOP_MARGIN + titleLine * 4, info.chapter);
  return columns;
}

// The rule left of the Vertical text columns.
int verticalRuleX(const GfxRenderer& renderer, const std::vector<VerticalColumn>& columns) {
  if (columns.empty()) return renderer.getScreenWidth() - SIDE_MARGIN;
  const auto& last = columns.back();
  return last.x - renderer.getLineHeight(last.fontId) - COLUMN_GAP;
}

uint8_t bayer4x4(const int x, const int y) {
  static constexpr uint8_t BAYER[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER[((y & 0x03) << 2) | (x & 0x03)];
}

// The cover enlarged to fill the screen and faded to a light dot pattern: at most a quarter of the
// pixels are black, so the framed cover and the text stay readable on top.
bool drawFadedBackdrop(const GfxRenderer& renderer, const Bitmap& cover) {
  const int width = cover.getWidth();
  const int height = cover.getHeight();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  if (width <= 0 || height <= 0) return false;
  int drawnWidth = screenWidth;
  int drawnHeight = static_cast<int>(static_cast<int64_t>(height) * screenWidth / width);
  if (drawnHeight < screenHeight) {
    drawnHeight = screenHeight;
    drawnWidth = static_cast<int>(static_cast<int64_t>(width) * screenHeight / height);
  }
  const int offsetX = (screenWidth - drawnWidth) / 2;
  const int offsetY = (screenHeight - drawnHeight) / 2;
  // Black-pixel share (of 256) for gray levels 0 (black) to 3 (white).
  static constexpr uint8_t INK[4] = {64, 32, 16, 0};

  // One 2-bit output row and one raw row, as in GfxRenderer::drawBitmap (a few hundred bytes for a
  // screen-sized cover), freed on return.
  const int outputRowSize = (width + 3) / 4;
  auto rows = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(outputRowSize) + cover.getRowBytes());
  if (!rows) {
    LOG_ERR("SLP", "OOM: backdrop rows");
    return false;
  }
  uint8_t* output = rows.get();
  uint8_t* raw = rows.get() + outputRowSize;
  if (cover.rewindToData() != BmpReaderError::Ok) return false;
  for (int fileRow = 0; fileRow < height; fileRow++) {
    if (cover.readNextRow(output, raw) != BmpReaderError::Ok) return false;
    const int row = cover.isTopDown() ? fileRow : height - 1 - fileRow;
    const int top = std::max(0, offsetY + static_cast<int>(static_cast<int64_t>(row) * drawnHeight / height));
    const int end =
        std::min(screenHeight, offsetY + static_cast<int>(static_cast<int64_t>(row + 1) * drawnHeight / height));
    for (int y = top; y < end; y++) {
      for (int x = 0; x < screenWidth; x++) {
        // Products stay far below 2^31 for screen-sized covers; 64-bit division is a slow library call here.
        const int sourceX = std::min(width - 1, (x - offsetX) * width / drawnWidth);
        const uint8_t level = (output[sourceX / 4] >> (6 - (sourceX % 4) * 2)) & 0x03;
        if (bayer4x4(x, y) < INK[level]) renderer.drawPixel(x, y, true);
      }
    }
  }
  return true;
}

void drawBand(const GfxRenderer& renderer, const CoverSleepRect& rect, const CoverSleepInfo& info) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  // Rounding can make the drawn cover 1 px taller than its area; the rule starts below it, since gray
  // planes redraw only the cover.
  const int bandTop = std::max(rect.maxHeight, rect.y + rect.height);
  // The cover area is black where a narrow cover leaves room at its sides.
  renderer.fillRect(0, 0, screenWidth, bandTop);
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.fillRect(0, bandTop, screenWidth, BAND_RULE);

  const int left = SIDE_MARGIN;
  const int right = screenWidth - SIDE_MARGIN;
  int y = bandTop + BAND_RULE + BAND_PADDING;
  int titleRight = right;
  if (info.percent >= 0) {
    const std::string percent = percentText(info.percent);
    const int percentWidth = renderer.getTextWidth(UI_12_FONT_ID, percent.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, right - percentWidth, y, percent.c_str(), true, EpdFontFamily::BOLD);
    titleRight = right - percentWidth - PERCENT_GAP;
  }
  const std::string title =
      renderer.truncatedText(UI_12_FONT_ID, info.title.c_str(), titleRight - left, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, left, y, title.c_str(), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID);

  const std::string subtitle = authorAndChapter(info);
  if (!subtitle.empty()) {
    y += GAP_LINE;
    renderer.drawText(UI_10_FONT_ID, left, y,
                      renderer.truncatedText(UI_10_FONT_ID, subtitle.c_str(), right - left).c_str());
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }

  if (info.percent >= 0) {
    y += GAP_BEFORE_PROGRESS;
    const int segmentWidth = (right - left - BAND_SEGMENT_GAP * (BAND_SEGMENTS - 1)) / BAND_SEGMENTS;
    const int filled = (info.percent * BAND_SEGMENTS + 50) / 100;
    for (int i = 0; i < BAND_SEGMENTS; i++) {
      const int x = left + i * (segmentWidth + BAND_SEGMENT_GAP);
      if (i < filled) {
        renderer.fillRect(x, y, segmentWidth, BAND_BAR_HEIGHT);
      } else {
        renderer.fillRectDither(x, y, segmentWidth, BAND_BAR_HEIGHT, Color::LightGray);
      }
    }
  }

  const char* sleeping = tr(STR_SLEEPING);
  const int sleepingY = screenHeight - BAND_PADDING - renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, right - renderer.getTextWidth(SMALL_FONT_ID, sleeping), sleepingY, sleeping);
}

void drawGallery(const GfxRenderer& renderer, const CoverSleepRect& rect, const CoverSleepInfo& info) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  renderer.fillRect(0, 0, screenWidth, screenHeight);
  renderer.drawRect(rect.x - GALLERY_FRAME_GAP, rect.y - GALLERY_FRAME_GAP, rect.width + GALLERY_FRAME_GAP * 2,
                    rect.height + GALLERY_FRAME_GAP * 2, GALLERY_FRAME_LINE, false);
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  int y = rect.y + rect.height + GALLERY_FRAME_GAP + GAP_UNDER_COVER;
  for (const auto& line : galleryTitleLines(renderer, info)) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str(), false, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  const int textWidth = screenWidth - SIDE_MARGIN * 2;
  if (!info.author.empty()) {
    y += GAP_SMALL;
    renderer.drawCenteredText(UI_10_FONT_ID, y,
                              renderer.truncatedText(UI_10_FONT_ID, info.author.c_str(), textWidth).c_str(), false);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  if (!info.chapter.empty()) {
    y += GAP_LINE;
    renderer.drawCenteredText(SMALL_FONT_ID, y,
                              renderer.truncatedText(SMALL_FONT_ID, info.chapter.c_str(), textWidth).c_str(), false);
    y += renderer.getLineHeight(SMALL_FONT_ID);
  }
  if (info.percent >= 0) {
    y += GAP_BEFORE_PROGRESS;
    const int filled = (info.percent * GALLERY_DOTS + 50) / 100;
    const int rowWidth = GALLERY_DOTS * GALLERY_DOT + (GALLERY_DOTS - 1) * GALLERY_DOT_GAP;
    for (int i = 0; i < GALLERY_DOTS; i++) {
      const int x = (screenWidth - rowWidth) / 2 + i * (GALLERY_DOT + GALLERY_DOT_GAP);
      if (i < filled) {
        renderer.fillRoundedRect(x, y, GALLERY_DOT, GALLERY_DOT, GALLERY_DOT / 2, Color::White);
      } else {
        renderer.drawRoundedRect(x, y, GALLERY_DOT, GALLERY_DOT, 1, GALLERY_DOT / 2, false);
      }
    }
  }
  renderer.drawCenteredText(SMALL_FONT_ID, screenHeight - BOTTOM_MARGIN - renderer.getLineHeight(SMALL_FONT_ID),
                            tr(STR_SLEEPING), false);
}

bool drawBackdrop(const GfxRenderer& renderer, const Bitmap& cover, const CoverSleepRect& rect,
                  const CoverSleepInfo& info) {
  if (!drawFadedBackdrop(renderer, cover)) return false;
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  renderer.fillRect(rect.x - BACKDROP_MAT, rect.y - BACKDROP_MAT, rect.width + BACKDROP_MAT * 2,
                    rect.height + BACKDROP_MAT * 2, false);
  renderer.drawRect(rect.x - BACKDROP_MAT, rect.y - BACKDROP_MAT, rect.width + BACKDROP_MAT * 2,
                    rect.height + BACKDROP_MAT * 2, true);

  const int cardWidth = screenWidth - SIDE_MARGIN * 2;
  const int cardHeight = backdropCardHeight(renderer, info);
  const int cardY = screenHeight - BOTTOM_MARGIN - cardHeight;
  renderer.fillRoundedRect(SIDE_MARGIN, cardY, cardWidth, cardHeight, CARD_RADIUS, Color::White);
  renderer.drawRoundedRect(SIDE_MARGIN, cardY, cardWidth, cardHeight, 1, CARD_RADIUS, true);
  const int textWidth = cardWidth - CARD_PADDING * 2;
  int y = cardY + CARD_PADDING;
  renderer.drawCenteredText(
      UI_12_FONT_ID, y,
      renderer.truncatedText(UI_12_FONT_ID, info.title.c_str(), textWidth, EpdFontFamily::BOLD).c_str(), true,
      EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + GAP_SMALL;
  const std::string detail = chapterAndPercent(info);
  if (!detail.empty()) {
    renderer.drawCenteredText(SMALL_FONT_ID, y,
                              renderer.truncatedText(SMALL_FONT_ID, detail.c_str(), textWidth).c_str());
  }
  return true;
}

void drawVertical(const GfxRenderer& renderer, const CoverSleepRect& rect, const CoverSleepInfo& info) {
  const int screenHeight = renderer.getScreenHeight();
  const auto columns = verticalColumns(renderer, info);
  for (const auto& column : columns) {
    renderer.drawTextVertical(column.fontId, column.x, column.y, column.text.c_str(), true, column.style);
  }
  const int ruleX = verticalRuleX(renderer, columns);
  renderer.drawLine(ruleX, TOP_MARGIN, ruleX, screenHeight - BOTTOM_MARGIN);

  renderer.drawRect(rect.x - 2, rect.y - 2, rect.width + 4, rect.height + 4, 2, true);
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  int y = rect.y + rect.height + 2 + VERTICAL_BAR_GAP;
  if (info.percent >= 0) {
    renderer.drawRect(rect.x, y, rect.width, PROGRESS_BAR_HEIGHT, true);
    const int filled = rect.width * info.percent / 100;
    renderer.fillRect(info.rightToLeft ? rect.x + rect.width - filled : rect.x, y, filled, PROGRESS_BAR_HEIGHT);
    y += PROGRESS_BAR_HEIGHT + 8;
    const std::string percent = percentText(info.percent);
    const int percentWidth = renderer.getTextWidth(SMALL_FONT_ID, percent.c_str());
    renderer.drawText(SMALL_FONT_ID, info.rightToLeft ? rect.x + rect.width - percentWidth : rect.x, y,
                      percent.c_str());
  }
  const char* sleeping = tr(STR_SLEEPING);
  renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - renderer.getTextWidth(SMALL_FONT_ID, sleeping),
                    screenHeight - BOTTOM_MARGIN - renderer.getLineHeight(SMALL_FONT_ID), sleeping);
}
}  // namespace

void bitmapDrawnSize(const Bitmap& bitmap, const int maxWidth, const int maxHeight, const float cropX,
                     const float cropY, int& width, int& height) {
  const int cropPixX = static_cast<int>(std::floor(bitmap.getWidth() * cropX / 2.0f));
  const int cropPixY = static_cast<int>(std::floor(bitmap.getHeight() * cropY / 2.0f));
  const int sourceWidth = bitmap.getWidth() - cropPixX * 2;
  const int sourceHeight = bitmap.getHeight() - cropPixY * 2;
  const float croppedWidth = (1.0f - cropX) * static_cast<float>(bitmap.getWidth());
  const float croppedHeight = (1.0f - cropY) * static_cast<float>(bitmap.getHeight());
  float scale = 1.0f;
  if (croppedWidth > 0.0f && croppedHeight > 0.0f && maxWidth > 0 && maxHeight > 0) {
    scale = std::min(static_cast<float>(maxWidth) / croppedWidth, static_cast<float>(maxHeight) / croppedHeight);
  }
  const bool scaled = scale < 1.0f;
  width = std::max(0, scaled ? static_cast<int>(std::floor((sourceWidth - 1) * scale)) + 1 : sourceWidth);
  height = std::max(0, scaled ? static_cast<int>(std::floor((sourceHeight - 1) * scale)) + 1 : sourceHeight);
}

CoverSleepRect coverSleepLayout(const uint8_t style, const GfxRenderer& renderer, const Bitmap& cover,
                                const CoverSleepInfo& info) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  CoverSleepRect rect;
  switch (style) {
    case CrossPointSettings::COVER_STYLE_BAND: {
      // Crop the cover to the area's shape so it fills the area's width.
      rect.maxWidth = screenWidth;
      rect.maxHeight = std::max(1, screenHeight - BAND_RULE - bandHeight(renderer, info));
      const float coverRatio = static_cast<float>(cover.getWidth()) / static_cast<float>(cover.getHeight());
      const float areaRatio = static_cast<float>(rect.maxWidth) / static_cast<float>(rect.maxHeight);
      if (coverRatio > areaRatio) {
        rect.cropX = 1.0f - areaRatio / coverRatio;
      } else {
        rect.cropY = 1.0f - coverRatio / areaRatio;
      }
      fitCover(cover, rect);
      rect.x = (screenWidth - rect.width) / 2;
      rect.y = (rect.maxHeight - rect.height) / 2;
      break;
    }
    case CrossPointSettings::COVER_STYLE_GALLERY: {
      const int frame = GALLERY_FRAME_GAP;
      const int sleepingArea = BOTTOM_MARGIN + renderer.getLineHeight(SMALL_FONT_ID) + GALLERY_FOOTER_GAP;
      rect.maxWidth = screenWidth - (GALLERY_SIDE + frame) * 2;
      rect.maxHeight =
          std::max(1, screenHeight - TOP_MARGIN - frame * 2 - galleryCaptionHeight(renderer, info) - sleepingArea);
      fitCover(cover, rect);
      rect.x = (screenWidth - rect.width) / 2;
      rect.y = TOP_MARGIN + frame;
      break;
    }
    case CrossPointSettings::COVER_STYLE_BACKDROP: {
      const int cardTop = screenHeight - BOTTOM_MARGIN - backdropCardHeight(renderer, info);
      const int areaHeight = cardTop - BACKDROP_CARD_GAP - TOP_MARGIN;
      rect.maxWidth = screenWidth * 5 / 8;
      rect.maxHeight = std::max(1, areaHeight - BACKDROP_MAT * 2);
      fitCover(cover, rect);
      rect.x = (screenWidth - rect.width) / 2;
      rect.y = TOP_MARGIN + (areaHeight - rect.height) / 2;
      break;
    }
    case CrossPointSettings::COVER_STYLE_VERTICAL:
    default: {
      const int coverRight = verticalRuleX(renderer, verticalColumns(renderer, info)) - VERTICAL_COVER_GAP;
      rect.maxWidth = std::max(1, coverRight - SIDE_MARGIN - 2);
      rect.maxHeight = screenHeight * 7 / 12;
      fitCover(cover, rect);
      rect.x = coverRight - 2 - rect.width;
      rect.y = TOP_MARGIN + 2;
      break;
    }
  }
  return rect;
}

bool drawCoverSleepDecor(const uint8_t style, const GfxRenderer& renderer, const Bitmap& cover,
                         const CoverSleepRect& rect, const CoverSleepInfo& info) {
  switch (style) {
    case CrossPointSettings::COVER_STYLE_BAND:
      drawBand(renderer, rect, info);
      return true;
    case CrossPointSettings::COVER_STYLE_GALLERY:
      drawGallery(renderer, rect, info);
      return true;
    case CrossPointSettings::COVER_STYLE_BACKDROP:
      return drawBackdrop(renderer, cover, rect, info);
    case CrossPointSettings::COVER_STYLE_VERTICAL:
    default:
      drawVertical(renderer, rect, info);
      return true;
  }
}
