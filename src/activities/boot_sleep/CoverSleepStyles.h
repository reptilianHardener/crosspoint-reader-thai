#pragma once

#include <cstdint>
#include <string>

class Bitmap;
class GfxRenderer;

// What the styled cover sleep screens show next to the cover.
struct CoverSleepInfo {
  std::string title;
  std::string author;
  int percent = -1;          // -1: the book has no saved position
  bool rightToLeft = false;  // Japanese book: the Obi Band progress bar fills from the right
};

// The cover's place on screen. width/height are what GfxRenderer::drawBitmap draws when given
// maxWidth/maxHeight and the crop (it shrinks a cover but never enlarges it).
struct CoverSleepRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int maxWidth = 0;
  int maxHeight = 0;
  float cropX = 0.0f;  // Obi Band and Spine crop the cover to their area
  float cropY = 0.0f;
  // Screen columns [hiddenLeft, hiddenRight) where a band lies over the cover (Obi Band).
  int hiddenLeft = 0;
  int hiddenRight = 0;
};

// Size GfxRenderer::drawBitmap draws this bitmap at for these bounds and crop (it shrinks, never enlarges).
// Keep in step with drawBitmap's scaling.
void bitmapDrawnSize(const Bitmap& bitmap, int maxWidth, int maxHeight, float cropX, float cropY, int& width,
                     int& height);

// style is a CrossPointSettings::SLEEP_SCREEN_COVER_STYLE other than Full.
CoverSleepRect coverSleepLayout(uint8_t style, const GfxRenderer& renderer, const Bitmap& cover,
                                const CoverSleepInfo& info);

// Draws everything but the cover in black and white, and leaves the cover's visible area white.
// Backdrop reads the cover file, so the caller rewinds it before drawing the cover.
// Returns false when the cover file cannot be read.
bool drawCoverSleepDecor(uint8_t style, const GfxRenderer& renderer, const Bitmap& cover, const CoverSleepRect& rect,
                         const CoverSleepInfo& info);
