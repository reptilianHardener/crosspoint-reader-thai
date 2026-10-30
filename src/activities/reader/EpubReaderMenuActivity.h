#pragma once
#include <Epub.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "components/themes/BaseTheme.h"

namespace freeink {
struct Icon;
}

class EpubReaderMenuActivity final : public UiListActivity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    SELECT_CHAPTER,
    FOOTNOTES,
    TEXT_SETTINGS,
    NIGHT_MODE,
    FRONTLIGHT,
    BLUETOOTH,
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    BOOKMARKS,
    SAVE_CLIPPING,
    VIEW_CLIPPINGS,
    TOGGLE_BOOKMARK,
    SCREENSHOT,
    DISPLAY_QR,
    GO_HOME,
    SYNC,
    DELETE_CACHE,
    DICTIONARY
  };

  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static void toggleBluetooth();
  static void buildMenuItems(std::vector<MenuItem>& items, bool hasFootnotes, bool hasBookmarks, bool hasClippings);

  // pageOnScreen: the framebuffer still holds the reader page, so a theme's menu sheet
  // can be drawn over it instead of on a blank screen.
  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const int currentPage, const int totalPages, const int bookProgressPercent,
                                  const uint8_t currentOrientation, const bool hasFootnotes, bool hasBookmarks,
                                  bool hasClippings, MenuAction& selectedAction, std::string chapterTitle = "",
                                  bool pageOnScreen = false);

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  bool acceptsRemoteInput() const override { return routingReady(); }

 private:
  // Row storage: menuItems is at most MAX_MENU_ITEMS, so a
  // fixed-capacity array avoids any heap allocation for the row list. Labels
  // are set once in the constructor (buildMenuRowItems()); buildScreen()
  // only refreshes rows whose values reflect live state.
  static constexpr size_t MAX_MENU_ITEMS = 19;
  freeink::ui::ListItem menuRowItems[MAX_MENU_ITEMS]{};
  void buildMenuRowItems();

  int listCount() const override { return static_cast<int>(menuItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Popup input runs before any button or touch handling.
  bool handleCustomInput() override;
  // Back closes on RELEASE and Confirm activates on RELEASE; everything else
  // (row navigation, page jumps) falls through to the base handler.
  bool handleButtons() override;
  // Header via GUI.drawHeader inside the safe area for the battery indicator.
  void drawChrome() override;

  void closeCancelled();
  void activateAction(MenuAction action);

  // --- sheet (themes with readerMenuSheet()) ----------------------------------
  // A sheet of six tiles at the bottom of the screen; its "More" tile shows the
  // full list. Both share the actions above.
  struct SheetTile {
    MenuAction action;
    StrId labelId;
    const freeink::Icon* icon;
  };
  static constexpr int SHEET_COLUMNS = 3;
  static constexpr int SHEET_TILES = 6;
  // The last tile opens the full list; the others hold an action.
  static constexpr int SHEET_MORE_TILE = SHEET_TILES - 1;
  struct SheetLayout {
    Rect sheet;
    Rect bar;
    Rect tiles[SHEET_TILES];
    int textX = 0;
    int textWidth = 0;
    int chapterY = 0;
    int bookY = 0;
    int legendY = 0;
  };
  void buildSheetTiles();
  SheetLayout sheetLayout() const;
  // Visual column of a tile: right-to-left sheets put the first tile on the right.
  int sheetColumn(int index) const;
  bool handleSheetInput();
  void renderSheet();
  void activateSheetTile(int index);
  StrId sheetLabel(int index) const;
  const freeink::Icon& sheetIcon(int index) const;

  bool sheetMode = false;
  bool hasSheet = false;
  bool pageBehind = false;
  int sheetSelected = 0;
  ReaderMenuSheetStyle sheetStyle;
  SheetTile sheetTiles[SHEET_MORE_TILE]{};
  std::string chapterTitle;

  // Fixed menu layout
  std::vector<MenuItem> menuItems;
  MenuAction& selectedAction;

  OptionPopup optionPopup;
  std::string title = "Reader Menu";
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
