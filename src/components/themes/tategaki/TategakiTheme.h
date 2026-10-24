#pragma once

#include "components/themes/shelf/ShelfTheme.h"

class GfxRenderer;

// Square, right-to-left styling after vertical Japanese print: the home screen shows the
// highlighted book's cover and its title written vertically beside a row of spines, tab
// bars run down the right edge, and the reader menu sheet reads from the right.
namespace TategakiMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = ShelfMetrics::values;
  v.homeRecentBooksCount = 5;
  v.listSelectionStyle = 0;  // invert fill
  v.listRowRadius = 0;
  v.listRowGap = 0;
  v.headerTitleAlign = 2;  // right
  v.headerUnderlineSize = 2;
  v.controlRadius = 0;
  v.sheetRadius = 0;
  v.capsuleRadius = 0;
  v.popupCornerRadius = 0;
  v.tabPillFullSlot = true;
  return v;
}();
}  // namespace TategakiMetrics

class TategakiTheme : public ShelfTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  // Only the highlighted book's cover is shown; -1 when there is no room for it.
  int homeCoverThumbHeight(const GfxRenderer& renderer) const override;
  int homeExtraCoverThumbHeights(const GfxRenderer&, int*, int) const override { return 0; }
  bool homeBookColumns(const GfxRenderer& renderer, Rect tile, int count, HomeBookColumns& columns) const override;
  bool readerMenuSheet(ReaderMenuSheetStyle& style) const override;
  bool verticalTabs() const override { return true; }

 protected:
  int cornerRadius() const override { return 0; }
};
