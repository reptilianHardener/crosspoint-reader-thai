#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Modern ("Halo 2") theme: a stacked cover carousel on the home screen above a
// two-column menu grid, black pill selection, thin rules.
//
// Geometry comes from a 480x800 mockup. Lists, headers and popups are drawn by the
// shared FreeInkUI components, so their look is expressed here as metrics; only the
// home screen and the button hints need drawing code of their own.
namespace ModernMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.topPadding = 5;
  v.batteryBarHeight = 28;
  v.headerHeight = 82;
  v.verticalSpacing = 14;
  v.contentSidePadding = 28;
  v.listRowHeight = 52;
  v.listWithSubtitleRowHeight = 68;
  v.listInset = 28;
  v.listSelectionStyle = 0;  // invert fill: a black pill with white text
  v.listScrollWidth = 3;
  v.headerSidePadding = 28;
  v.headerUnderlineSize = 1;
  v.menuRowHeight = 72;
  v.menuSpacing = 10;
  v.tabSpacing = 6;
  v.tabBarHeight = 40;
  v.scrollBarWidth = 3;
  v.homeTopPadding = 28;
  v.homeCoverHeight = 260;
  v.homeCoverTileHeight = 460;  // carousel + title + author + progress + dots + divider
  v.homeRecentBooksCount = 5;
  v.homeMenuTopOffset = 0;
  v.buttonHintsHeight = 30;
  v.sideButtonHintsWidth = 24;
  v.keyboardCenteredText = true;
  v.popupMarginX = 20;
  v.popupMarginY = 14;
  return v;
}();

// Cover heights of the three carousel tiers. Thumbnails are generated at exactly these
// heights so they draw 1:1; rescaling a dithered thumbnail aliases badly.
constexpr int centerCoverHeight = 260;
constexpr int sideCoverHeight = 200;
constexpr int farCoverHeight = 150;
}  // namespace ModernMetrics

class ModernTheme : public LyraTheme {
 public:
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  bool homeIsCarousel() const override { return true; }
  void onHomeEnter() const override;
  int homeExtraCoverThumbHeights(int* heights, int capacity) const override;
  bool homeMenuGrid(const GfxRenderer& renderer, Rect rect, int buttonCount, HomeMenuGrid& grid) const override;
};
