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
// Off the Edge: the cover runs off the top and right edges, right of a full-height spine rule.
constexpr int EDGE_OVERHANG = 48;
constexpr int EDGE_COVER_GAP = 12;  // spine rule to cover
constexpr int EDGE_TEXT_GAP = 20;   // text columns to spine rule
constexpr int EDGE_PERCENT_INDENT = 24;
constexpr int EDGE_PERCENT_GAP = 36;
// Obi Band: a black band over the cover, like the paper band on a Japanese book.
constexpr int OBI_FOOTER = 96;  // white strip under the cover
constexpr int OBI_RIGHT = 68;   // band to the right screen edge
constexpr int OBI_PADDING = 20;
constexpr int OBI_TOP = 48;
constexpr int OBI_OVERHANG = 32;  // band below the cover
constexpr int OBI_DIVIDER = 24;
constexpr int OBI_DIVIDER_GAP = 20;
constexpr int OBI_BAR_WIDTH = 150;
// Spine: a narrow middle slice of the cover between two lines.
constexpr int SPINE_WIDTH = 140;
constexpr int SPINE_RIGHT = 44;
constexpr int SPINE_TEXT_GAP = 24;
constexpr int SPINE_PROGRESS_INSET = 29;  // progress line to the right screen edge
// Off the Edge and Spine: a thin rule with a thick top part that shows progress.
constexpr int RULE_WIDTH = 3;
constexpr int PROGRESS_RULE_WIDTH = 11;
constexpr int PROGRESS_BAR_HEIGHT = 6;
constexpr int COLUMN_GAP = 12;
// Vertical gaps between text lines, used both to measure a layout and to draw it.
constexpr int GAP_UNDER_COVER = 28;
constexpr int GAP_SMALL = 2;
constexpr int GAP_BEFORE_PROGRESS = 14;
constexpr int GAP_BEFORE_FOOTER = 12;
constexpr int GALLERY_SIDE = 40;
constexpr int GALLERY_FOOTER_GAP = 16;
constexpr int PERCENT_GAP = 16;
// The big percent: UI_12 bold glyphs scaled up 1.75x. The UI font is not compressed, so drawing it
// needs no decompression buffer on the sleep path.
constexpr int BIG_PERCENT_SCALE = 448;

void fitCover(const Bitmap& cover, CoverSleepRect& rect) {
  bitmapDrawnSize(cover, rect.maxWidth, rect.maxHeight, rect.cropX, rect.cropY, rect.width, rect.height);
}

// Crops the cover to the shape of its area so it fills the area.
void cropToArea(const Bitmap& cover, CoverSleepRect& rect) {
  const float coverRatio = static_cast<float>(cover.getWidth()) / static_cast<float>(cover.getHeight());
  const float areaRatio = static_cast<float>(rect.maxWidth) / static_cast<float>(rect.maxHeight);
  if (coverRatio > areaRatio) {
    rect.cropX = 1.0f - areaRatio / coverRatio;
  } else {
    rect.cropY = 1.0f - coverRatio / areaRatio;
  }
}

std::string percentText(const int percent) { return std::to_string(percent) + "%"; }

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
  if (info.percent >= 0) height += GAP_BEFORE_PROGRESS + GALLERY_DOT;
  return height;
}

int edgeRuleX(const GfxRenderer& renderer) { return renderer.getScreenWidth() / 3; }

// Top of the Obi Band footer row (the progress bar, percent and "Sleeping").
int obiFooterY(const GfxRenderer& renderer) {
  return renderer.getScreenHeight() - BOTTOM_MARGIN - renderer.getLineHeight(UI_10_FONT_ID);
}

int bigPercentHeight(const GfxRenderer& renderer) {
  return renderer.getLineHeight(UI_12_FONT_ID) * BIG_PERCENT_SCALE / 256;
}

void drawBigPercent(const GfxRenderer& renderer, int x, const int top, const int percent) {
  const int baseline = top + renderer.getFontAscenderSize(UI_12_FONT_ID) * BIG_PERCENT_SCALE / 256;
  for (const char c : percentText(percent)) {
    x += renderer.drawScaledCodepoint(UI_12_FONT_ID, static_cast<unsigned char>(c), EpdFontFamily::BOLD, x, baseline,
                                      BIG_PERCENT_SCALE);
  }
}

// Top of the big percent in the Spine style, above "Sleeping".
int spinePercentY(const GfxRenderer& renderer) {
  return renderer.getScreenHeight() - BOTTOM_MARGIN - renderer.getLineHeight(SMALL_FONT_ID) - GAP_BEFORE_FOOTER -
         bigPercentHeight(renderer);
}

// Title (up to two columns) and author as vertical text, from right to left. A column occupies
// (right - lineHeight, right]; columns that do not fit right of left are dropped.
void drawTextColumns(const GfxRenderer& renderer, const CoverSleepInfo& info, int right, const int left, const int top,
                     const int bottom) {
  const auto add = [&](const int fontId, const EpdFontFamily::Style style, const int y, const std::string& text) {
    const int lineHeight = renderer.getLineHeight(fontId);
    if (text.empty() || y >= bottom || right - lineHeight < left) return;
    renderer.drawTextVertical(fontId, right, y, renderer.truncatedText(fontId, text.c_str(), bottom - y, style).c_str(),
                              true, style);
    right -= lineHeight + COLUMN_GAP;
  };
  const int titleLine = renderer.getLineHeight(UI_12_FONT_ID);
  if (!info.title.empty()) {
    for (const auto& line :
         renderer.wrappedText(UI_12_FONT_ID, info.title.c_str(), bottom - top, 2, EpdFontFamily::BOLD)) {
      add(UI_12_FONT_ID, EpdFontFamily::BOLD, top, line);
    }
  }
  // The author starts lower, like a label on a book's spine.
  add(UI_10_FONT_ID, EpdFontFamily::REGULAR, top + titleLine * 2, info.author);
}

// A full-height rule at x whose top part, as long as the share read, is thick.
void drawProgressRule(const GfxRenderer& renderer, const int x, const int lineWidth, const int percent) {
  const int screenHeight = renderer.getScreenHeight();
  renderer.fillRect(x - lineWidth / 2, 0, lineWidth, screenHeight);
  if (percent > 0) {
    renderer.fillRect(x - PROGRESS_RULE_WIDTH / 2, 0, PROGRESS_RULE_WIDTH, screenHeight * percent / 100);
  }
}

void drawSleepingRight(const GfxRenderer& renderer) {
  const char* sleeping = tr(STR_SLEEPING);
  renderer.drawText(SMALL_FONT_ID,
                    renderer.getScreenWidth() - SIDE_MARGIN - renderer.getTextWidth(SMALL_FONT_ID, sleeping),
                    renderer.getScreenHeight() - BOTTOM_MARGIN - renderer.getLineHeight(SMALL_FONT_ID), sleeping);
}

uint8_t bayer4x4(const int x, const int y) {
  static constexpr uint8_t BAYER[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER[((y & 0x03) << 2) | (x & 0x03)];
}

// The cover enlarged to fill the screen and faded to a light dot pattern: at most a quarter of the
// pixels are black, so the framed cover stands out.
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

bool drawBackdrop(const GfxRenderer& renderer, const Bitmap& cover, const CoverSleepRect& rect) {
  if (!drawFadedBackdrop(renderer, cover)) return false;
  renderer.fillRect(rect.x - BACKDROP_MAT, rect.y - BACKDROP_MAT, rect.width + BACKDROP_MAT * 2,
                    rect.height + BACKDROP_MAT * 2, false);
  renderer.drawRect(rect.x - BACKDROP_MAT, rect.y - BACKDROP_MAT, rect.width + BACKDROP_MAT * 2,
                    rect.height + BACKDROP_MAT * 2, true);
  return true;
}

void drawEdge(const GfxRenderer& renderer, const CoverSleepRect& rect, const CoverSleepInfo& info) {
  const int screenHeight = renderer.getScreenHeight();
  const int ruleX = edgeRuleX(renderer);
  drawProgressRule(renderer, ruleX, RULE_WIDTH, std::max(0, info.percent));
  drawTextColumns(renderer, info, ruleX - EDGE_TEXT_GAP, SIDE_MARGIN, TOP_MARGIN, screenHeight - BOTTOM_MARGIN);
  if (info.percent >= 0) {
    const int y = rect.y + rect.height + EDGE_PERCENT_GAP;
    const int sleepingY = screenHeight - BOTTOM_MARGIN - renderer.getLineHeight(SMALL_FONT_ID);
    if (y + bigPercentHeight(renderer) <= sleepingY)
      drawBigPercent(renderer, rect.x + EDGE_PERCENT_INDENT, y, info.percent);
  }
  drawSleepingRight(renderer);
}

void drawObi(const GfxRenderer& renderer, const CoverSleepRect& rect, const CoverSleepInfo& info) {
  const int screenWidth = renderer.getScreenWidth();
  // Rounding can make the drawn cover 1 px taller than its area.
  const int coverBottom = std::max(rect.maxHeight, rect.y + rect.height);
  const int bandLeft = rect.hiddenLeft;
  const int bandWidth = rect.hiddenRight - rect.hiddenLeft;
  const int bandBottom = coverBottom + OBI_OVERHANG;
  renderer.fillRect(bandLeft, 0, bandWidth, bandBottom);

  // One white column: title, a short divider, author.
  const int titleLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int authorLine = renderer.getLineHeight(UI_10_FONT_ID);
  const int length = bandBottom - OBI_TOP - OBI_PADDING;
  int authorLength = 0;
  if (!info.author.empty()) {
    authorLength = std::min(renderer.getTextWidth(UI_10_FONT_ID, info.author.c_str()), length / 3);
  }
  const int dividerSpace = authorLength > 0 ? OBI_DIVIDER_GAP * 2 + GAP_SMALL : 0;
  int y = OBI_TOP;
  if (!info.title.empty()) {
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, info.title.c_str(),
                                                     length - authorLength - dividerSpace, EpdFontFamily::BOLD);
    renderer.drawTextVertical(UI_12_FONT_ID, bandLeft + (bandWidth + titleLine) / 2, y, title.c_str(), false,
                              EpdFontFamily::BOLD);
    y += renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
  }
  if (authorLength > 0) {
    y += OBI_DIVIDER_GAP;
    renderer.fillRect(bandLeft + (bandWidth - OBI_DIVIDER) / 2, y, OBI_DIVIDER, GAP_SMALL, false);
    y += GAP_SMALL + OBI_DIVIDER_GAP;
    renderer.drawTextVertical(UI_10_FONT_ID, bandLeft + (bandWidth + authorLine) / 2, y,
                              renderer.truncatedText(UI_10_FONT_ID, info.author.c_str(), authorLength).c_str(), false);
  }

  // Footer: "Sleeping" on the left; progress bar and percent on the right.
  const int footerY = obiFooterY(renderer);
  renderer.drawText(SMALL_FONT_ID, SIDE_MARGIN,
                    footerY + (renderer.getLineHeight(UI_10_FONT_ID) - renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                    tr(STR_SLEEPING));
  if (info.percent >= 0) {
    const std::string percent = percentText(info.percent);
    const int percentWidth = renderer.getTextWidth(UI_10_FONT_ID, percent.c_str(), EpdFontFamily::BOLD);
    const int percentX = screenWidth - SIDE_MARGIN - percentWidth;
    renderer.drawText(UI_10_FONT_ID, percentX, footerY, percent.c_str(), true, EpdFontFamily::BOLD);
    const int barX = percentX - PERCENT_GAP - OBI_BAR_WIDTH;
    const int barY = footerY + (authorLine - PROGRESS_BAR_HEIGHT) / 2;
    renderer.drawRect(barX, barY, OBI_BAR_WIDTH, PROGRESS_BAR_HEIGHT, true);
    const int filled = OBI_BAR_WIDTH * info.percent / 100;
    renderer.fillRect(info.rightToLeft ? barX + OBI_BAR_WIDTH - filled : barX, barY, filled, PROGRESS_BAR_HEIGHT);
  }
}

void drawSpine(const GfxRenderer& renderer, const CoverSleepRect& rect, const CoverSleepInfo& info) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  renderer.fillRect(rect.x - RULE_WIDTH, 0, RULE_WIDTH, screenHeight);
  renderer.fillRect(rect.x + rect.width, 0, RULE_WIDTH, screenHeight);
  drawProgressRule(renderer, screenWidth - SPINE_PROGRESS_INSET, 1, std::max(0, info.percent));

  const int percentY = spinePercentY(renderer);
  drawTextColumns(renderer, info, rect.x - RULE_WIDTH - SPINE_TEXT_GAP, SIDE_MARGIN, TOP_MARGIN,
                  percentY - PERCENT_GAP);
  if (info.percent >= 0) {
    drawBigPercent(renderer, SIDE_MARGIN, percentY, info.percent);
  }
  renderer.drawText(SMALL_FONT_ID, SIDE_MARGIN, screenHeight - BOTTOM_MARGIN - renderer.getLineHeight(SMALL_FONT_ID),
                    tr(STR_SLEEPING));
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
    case CrossPointSettings::COVER_STYLE_EDGE: {
      // Large enough to run off the top and right edges; drawBitmap skips pixels off screen.
      rect.x = edgeRuleX(renderer) + EDGE_COVER_GAP;
      rect.maxWidth = screenWidth - rect.x + EDGE_OVERHANG;
      rect.maxHeight = screenHeight * 3 / 4;
      fitCover(cover, rect);
      rect.y = -EDGE_OVERHANG;
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
      // The framed cover alone, centered on the faded copy.
      rect.maxWidth = screenWidth * 5 / 8;
      rect.maxHeight = screenHeight - TOP_MARGIN - BOTTOM_MARGIN - BACKDROP_MAT * 2;
      fitCover(cover, rect);
      rect.x = (screenWidth - rect.width) / 2;
      rect.y = (screenHeight - rect.height) / 2;
      break;
    }
    case CrossPointSettings::COVER_STYLE_OBI: {
      rect.maxWidth = screenWidth;
      rect.maxHeight = screenHeight - OBI_FOOTER;
      cropToArea(cover, rect);
      fitCover(cover, rect);
      rect.x = (screenWidth - rect.width) / 2;
      rect.y = (rect.maxHeight - rect.height) / 2;
      rect.hiddenRight = screenWidth - OBI_RIGHT;
      rect.hiddenLeft = rect.hiddenRight - renderer.getLineHeight(UI_12_FONT_ID) - OBI_PADDING * 2;
      break;
    }
    case CrossPointSettings::COVER_STYLE_SPINE:
    default: {
      // Keep the middle SPINE_WIDTH columns of the cover.
      if (cover.getWidth() > SPINE_WIDTH) {
        rect.cropX = 1.0f - static_cast<float>(SPINE_WIDTH) / static_cast<float>(cover.getWidth());
      }
      rect.maxWidth = SPINE_WIDTH;
      rect.maxHeight = screenHeight;
      fitCover(cover, rect);
      rect.x = screenWidth - SPINE_RIGHT - rect.width;
      rect.y = (screenHeight - rect.height) / 2;
      break;
    }
  }
  return rect;
}

bool drawCoverSleepDecor(const uint8_t style, const GfxRenderer& renderer, const Bitmap& cover,
                         const CoverSleepRect& rect, const CoverSleepInfo& info) {
  switch (style) {
    case CrossPointSettings::COVER_STYLE_EDGE:
      drawEdge(renderer, rect, info);
      return true;
    case CrossPointSettings::COVER_STYLE_GALLERY:
      drawGallery(renderer, rect, info);
      return true;
    case CrossPointSettings::COVER_STYLE_BACKDROP:
      return drawBackdrop(renderer, cover, rect);
    case CrossPointSettings::COVER_STYLE_OBI:
      drawObi(renderer, rect, info);
      return true;
    case CrossPointSettings::COVER_STYLE_SPINE:
    default:
      drawSpine(renderer, rect, info);
      return true;
  }
}
