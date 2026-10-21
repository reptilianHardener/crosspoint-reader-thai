#pragma once

#include "components/themes/roundedraff/RoundedRaffTheme.h"

class GfxRenderer;

// RoundedRaff styling with a home screen of stacked rounded cards: an optional
// user image, three recent covers, a second optional user image, a card for the
// book in progress, and a single row of menu icons.
namespace BannerMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = RoundedRaffMetrics::values;
  // Battery and clock share the top strip, clock in the left corner.
  v.headerClockCentered = false;
  v.homeTopPadding = 40;
  // Portrait: everything between the status strip and the menu row (see BannerTheme.cpp).
  v.homeCoverTileHeight = 650;
  v.homeCoverHeight = 200;
  v.homeRecentBooksCount = 3;
  v.homeContinueReadingInMenu = false;
  v.homeMenuTopOffset = 8;
  return v;
}();
}  // namespace BannerMetrics

class BannerTheme : public RoundedRaffTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  int homeCoverThumbHeight(const GfxRenderer& renderer) const override;
  bool homeMenuGrid(const GfxRenderer& renderer, Rect rect, int buttonCount, HomeMenuGrid& grid) const override;
  bool homeCoverBand(const GfxRenderer& renderer, Rect rect, int& top, int& bottom) const override;
  bool homeRowNavigation() const override { return true; }
  void onHomeEnter() const override { cardBookIndex = 0; }

 private:
  mutable int cardBookIndex = 0;
};
