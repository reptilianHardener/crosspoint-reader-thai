#pragma once

#include "components/themes/lyra/Lyra3CoversTheme.h"

class GfxRenderer;

// Lyra lists with a bookshelf home: a card for the highlighted book, a shelf of four
// covers, and a row of labelled menu buttons. The reader menu opens as a sheet of tiles.
namespace ShelfMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = Lyra3CoversMetrics::values;
  v.headerClockCentered = false;
  v.homeTopPadding = 40;
  // Portrait: everything between the status strip and the menu row (see ShelfTheme.cpp).
  v.homeCoverTileHeight = 620;
  v.homeCoverHeight = 160;
  v.homeRecentBooksCount = 4;
  v.homeContinueReadingInMenu = false;
  v.homeMenuTopOffset = 8;
  // Settings rows read as rounded boxes with a little air between them.
  v.listSelectionStyle = 0;  // invert fill
  v.listRowRadius = 8;
  v.listRowGap = 4;
  v.controlRadius = 10;
  v.sheetRadius = 14;
  v.popupCornerRadius = 10;
  return v;
}();
}  // namespace ShelfMetrics

class ShelfTheme : public Lyra3CoversTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  int homeCoverThumbHeight(const GfxRenderer& renderer) const override;
  int homeExtraCoverThumbHeights(const GfxRenderer& renderer, int* heights, int capacity) const override;
  bool homeMenuGrid(const GfxRenderer& renderer, Rect rect, int buttonCount, HomeMenuGrid& grid) const override;
  bool homeBookColumns(const GfxRenderer& renderer, Rect tile, int count, HomeBookColumns& columns) const override;
  bool homeSnapshotBand(const GfxRenderer& renderer, Rect tile, int& top, int& bottom) const override;
  bool homeRowNavigation() const override { return true; }
  void onHomeEnter() const override { cardBookIndex = 0; }
  bool readerMenuSheet(ReaderMenuSheetStyle& style) const override;

  // Cover thumbs: the height to generate for a slot, and drawing one cropped to the slot
  // (a gray placeholder when the book has no usable thumb).
  static int coverThumbHeight(Rect slot);
  static void drawCover(const GfxRenderer& renderer, const RecentBook& book, Rect slot);

 protected:
  // Corner radius of the menu buttons and cards; zero gives the square Tategaki look.
  virtual int cornerRadius() const { return 10; }
  // Top of the menu row: under the tile, pulled up when the screen is short (landscape).
  static int menuTop(const GfxRenderer& renderer, int menuHeight);
  static constexpr int MENU_HEIGHT = 68;

  mutable int cardBookIndex = 0;
};
