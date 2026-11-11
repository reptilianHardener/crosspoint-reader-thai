#pragma once

#include "components/themes/lyra/Lyra3CoversTheme.h"

class GfxRenderer;

// Square, right-to-left styling after vertical Japanese print (a tanzaku is the narrow strip
// a poem is written on): the home screen shows the highlighted book's cover and its title
// written vertically beside a row of spines, with a row of icon menu buttons below; tab
// bars run down the right edge, and the reader menu opens as a sheet of tiles that reads
// from the right.
namespace TategakiMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = Lyra3CoversMetrics::values;
  v.headerTitleAlign = 2;  // right
  v.headerUnderlineSize = 2;
  v.homeTopPadding = 40;
  // Portrait: everything between the status strip and the menu row (see TategakiTheme.cpp).
  v.homeCoverTileHeight = 620;
  v.homeCoverHeight = 160;
  v.homeRecentBooksCount = 5;
  v.homeMenuTopOffset = 8;
  v.listSelectionStyle = 0;  // invert fill
  v.listRowRadius = 0;
  v.controlRadius = 0;
  v.sheetRadius = 0;
  v.capsuleRadius = 0;
  // A square popup is white with a black frame (BaseTheme::drawPopup), so its text
  // and progress bar are black, unlike Lyra's rounded black popup.
  v.popupCornerRadius = 0;
  v.popupTextInverted = true;
  v.popupProgressFillInverted = true;
  v.popupProgressOutlineInverted = true;
  v.tabPillFullSlot = true;
  return v;
}();
}  // namespace TategakiMetrics

class TategakiTheme : public Lyra3CoversTheme {
 public:
  // Without a cover (the "Tanzaku (Without Cover)" theme) the card is compact: a one-column
  // title and the author start at the top, and no cover thumbnails are made.
  explicit TategakiTheme(bool showCover = true) : showCover(showCover) {}
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  // Only the highlighted book's cover is shown; -1 when there is none or no room for it.
  int homeCoverThumbHeight(const GfxRenderer& renderer) const override;
  bool homeMenuGrid(const GfxRenderer& renderer, Rect rect, int buttonCount, HomeMenuGrid& grid) const override;
  bool homeBookColumns(const GfxRenderer& renderer, Rect tile, int count, HomeBookColumns& columns) const override;
  bool homeRowNavigation() const override { return true; }
  void onHomeEnter() const override { cardBookIndex = 0; }
  bool homeShowsSeries() const override { return true; }
  bool readerMenuSheet(ReaderMenuSheetStyle& style) const override;
  bool verticalTabs() const override { return true; }

 private:
  bool showCover;
  // The book the title card shows: the highlighted one, kept while the menu row has focus.
  mutable int cardBookIndex = 0;
};
