#include "EpubReaderMenuActivity.h"

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "components/icons/readerSheetIcons.h"
#include "fontIds.h"
#include "util/SlideSwitch.h"

namespace fui = freeink::ui;

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks,
                                               const bool hasClippings, std::string chapterTitle,
                                               const bool pageOnScreen)
    : UiListActivity("EpubReaderMenu", renderer, mappedInput),
      pageBehind(pageOnScreen),
      chapterTitle(std::move(chapterTitle)),
      title(title),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  buildMenuItems(menuItems, hasFootnotes, hasBookmarks, hasClippings);
  buildMenuRowItems();
  hasSheet = GUI.readerMenuSheet(sheetStyle);
  sheetMode = hasSheet;
  if (hasSheet) buildSheetTiles();
}

void EpubReaderMenuActivity::buildSheetTiles() {
  // Look Up needs a dictionary; without one that tile jumps to a percentage instead.
  const bool dictionary = SETTINGS.dictionaryName[0] != '\0';
  sheetTiles[0] = {MenuAction::SELECT_CHAPTER, StrId::STR_TOOL_CONTENTS, &icon_sheet_contents_24};
  sheetTiles[1] = {MenuAction::TOGGLE_BOOKMARK, StrId::STR_TILE_BOOKMARK, &icon_sheet_bookmark_24};
  sheetTiles[2] = dictionary ? SheetTile{MenuAction::DICTIONARY, StrId::STR_TILE_LOOKUP, &icon_sheet_lookup_24}
                             : SheetTile{MenuAction::GO_TO_PERCENT, StrId::STR_TILE_GO_TO, &icon_sheet_goto_24};
  sheetTiles[3] = {MenuAction::TEXT_SETTINGS, StrId::STR_TOOL_TEXT, &icon_sheet_text_24};
  sheetTiles[4] = {MenuAction::ROTATE_SCREEN, StrId::STR_TILE_ROTATE, &icon_sheet_rotate_24};
}

StrId EpubReaderMenuActivity::sheetLabel(const int index) const {
  return index == SHEET_MORE_TILE ? StrId::STR_TOOL_MORE : sheetTiles[index].labelId;
}

const freeink::Icon& EpubReaderMenuActivity::sheetIcon(const int index) const {
  return index == SHEET_MORE_TILE ? icon_sheet_more_24 : *sheetTiles[index].icon;
}

// Populates menuRowItems's labels/actionValue from menuItems. Called once
// here since menuItems (and thus which rows exist) never changes after
// construction; buildScreen() only touches the two rows with a live value.
void EpubReaderMenuActivity::buildMenuRowItems() {
  for (size_t i = 0; i < menuItems.size() && i < MAX_MENU_ITEMS; i++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[i].labelId);
    item.actionValue = static_cast<int16_t>(i);
    menuRowItems[i] = item;
  }
}

void EpubReaderMenuActivity::buildMenuItems(std::vector<MenuItem>& items, const bool hasFootnotes,
                                            const bool hasBookmarks, const bool hasClippings) {
  items.clear();
  items.reserve(MAX_MENU_ITEMS);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  items.push_back({MenuAction::SAVE_CLIPPING, StrId::STR_SAVE_CLIPPING});
  if (hasClippings) {
    items.push_back({MenuAction::VIEW_CLIPPINGS, StrId::STR_VIEW_CLIPPINGS});
  }
  items.push_back({MenuAction::NIGHT_MODE, StrId::STR_NIGHT_MODE});
  if (Frontlight.present() && !slideSwitchOwnsLight()) {
    items.push_back({MenuAction::FRONTLIGHT, StrId::STR_FRONTLIGHT});
  }
  if (bleturner::status().compiledIn) {
    items.push_back({MenuAction::BLUETOOTH, StrId::STR_BT_PAGE_TURNER});
  }
  if (SETTINGS.dictionaryName[0] != '\0') {
    items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  }
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  items.push_back({MenuAction::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS});
}

void EpubReaderMenuActivity::toggleBluetooth() {
  SETTINGS.ble.enabled = SETTINGS.ble.enabled ? 0 : 1;
  if (!SETTINGS.ble.enabled) bleturner::switchOff();
  SETTINGS.saveToFile();
  // Enabling waits for the main-loop lifecycle's reader heap gates, then says in the status bar
  // whether a remote linked.
  if (SETTINGS.ble.enabled) bleturner::requestConnect();
}

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
  setResult(std::move(result));
  finish();
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  // The activated row leaves this screen (popup or finish); a lingering flash
  // would gray an unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  activateAction(menuItems[index].action);
}

void EpubReaderMenuActivity::activateAction(const MenuAction selectedAction) {
  if (selectedAction == MenuAction::ROTATE_SCREEN || selectedAction == MenuAction::AUTO_PAGE_TURN) {
    // The popup and a turned screen both cover the page the sheet was drawn over.
    pageBehind = false;
  }
  if (selectedAction == MenuAction::ROTATE_SCREEN) {
    optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                     pendingOrientation, [this](int idx) {
                       pendingOrientation = idx;
                       // Rotate the menu immediately. Only the renderer turns;
                       // SETTINGS.orientation stays unchanged so the reader's
                       // result handler still detects the change and reflows.
                       ReaderUtils::applyOrientation(renderer, pendingOrientation);
                       app.setDevice(uiTarget.deviceContext());  // hit rects follow the new frame
                       requestUpdate(true);
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
    optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                     static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](int idx) {
                       selectedPageTurnOption = idx;
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::NIGHT_MODE) {
    SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::BLUETOOTH) {
    toggleBluetooth();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::FRONTLIGHT) {
    const bool lightOn = !Frontlight.isOn();
    Frontlight.setOn(lightOn);
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
  finish();
}

bool EpubReaderMenuActivity::handleCustomInput() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  return sheetMode && handleSheetInput();
}

bool EpubReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (hasSheet) {
      // The full list was opened from the sheet's More tile: Back returns there.
      sheetMode = true;
      requestUpdate();
      return true;
    }
    closeCancelled();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void EpubReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  // Progress summary where the old sub-header band sat.
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), screen.theme().smallText);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // menuRowItems's labels/actionValue were set once in the constructor (see
  // buildMenuRowItems()); only rows with live values need refreshing here.
  for (size_t i = 0; i < menuItems.size(); i++) {
    const auto action = menuItems[i].action;
    if (action == MenuAction::ROTATE_SCREEN) {
      menuRowItems[i].value = I18N.get(orientationLabels[pendingOrientation]);
    } else if (action == MenuAction::AUTO_PAGE_TURN) {
      menuRowItems[i].value = pageTurnLabels[selectedPageTurnOption];
    } else if (action == MenuAction::NIGHT_MODE) {
      GUI.setCheckboxRow(menuRowItems[i], SETTINGS.screenInverted);
    } else if (action == MenuAction::BLUETOOTH) {
      GUI.setCheckboxRow(menuRowItems[i], SETTINGS.ble.enabled);
    } else if (action == MenuAction::FRONTLIGHT) {
      GUI.setCheckboxRow(menuRowItems[i], Frontlight.isOn());
    }
  }

  fui::ListProps props;
  props.items = menuRowItems;
  props.count = static_cast<uint16_t>(menuItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubReaderMenuActivity::drawChrome() {
  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), title.c_str());
}

int EpubReaderMenuActivity::sheetColumn(const int index) const {
  const int column = index % SHEET_COLUMNS;
  return sheetStyle.rightToLeft ? SHEET_COLUMNS - 1 - column : column;
}

EpubReaderMenuActivity::SheetLayout EpubReaderMenuActivity::sheetLayout() const {
  constexpr int kPadding = 18;
  constexpr int kTileHeight = 64;
  constexpr int kTileGap = 10;
  constexpr int kBarHeight = 10;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);

  SheetLayout l;
  const int bottom = safe.y + safe.height - metrics.buttonHintsHeight;
  const int height = kPadding + titleH + smallH + 12 + kBarHeight + 6 + smallH + 16 + 2 * kTileHeight + kTileGap +
                     kPadding;
  l.sheet = Rect{safe.x, bottom - height, safe.width, height};
  l.textX = l.sheet.x + kPadding;
  l.textWidth = l.sheet.width - 2 * kPadding;
  l.chapterY = l.sheet.y + kPadding;
  l.bookY = l.chapterY + titleH;
  l.bar = Rect{l.textX, l.bookY + smallH + 12, l.textWidth, kBarHeight};
  l.legendY = l.bar.y + kBarHeight + 6;
  const int tilesTop = l.legendY + smallH + 16;
  const int tileWidth = (l.textWidth - (SHEET_COLUMNS - 1) * kTileGap) / SHEET_COLUMNS;
  for (int i = 0; i < SHEET_TILES; i++) {
    const int row = i / SHEET_COLUMNS;
    l.tiles[i] = Rect{l.textX + sheetColumn(i) * (tileWidth + kTileGap), tilesTop + row * (kTileHeight + kTileGap),
                      tileWidth, kTileHeight};
  }
  return l;
}

namespace {
// Plots a generated FreeInk icon upright (bit 0 = ink), following the reader's orientation.
void drawSheetIcon(const GfxRenderer& renderer, const freeink::Icon& icon, const int x, const int y,
                   const bool black) {
  const int rowBytes = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; row++) {
    for (int col = 0; col < icon.w; col++) {
      const uint8_t byte = icon.bits[row * rowBytes + (col >> 3)];
      if (((byte >> (7 - (col & 7))) & 1) == 0) renderer.drawPixel(x + col, y + row, black);
    }
  }
}
}  // namespace

void EpubReaderMenuActivity::renderSheet() {
  const SheetLayout l = sheetLayout();
  const int radius = sheetStyle.radius;
  if (!pageBehind) {
    // Opened over another screen (or after a popup): a plain page with the book's header.
    renderer.clearScreen();
    drawChrome();
  }

  const Rect& s = l.sheet;
  renderer.fillRoundedRect(s.x, s.y, s.width, s.height + radius, radius, true, true, false, false, Color::White);
  renderer.drawRoundedRect(s.x, s.y, s.width, s.height + radius, 3, radius, true, true, false, false, true);

  const std::string chapter = renderer.truncatedText(
      UI_12_FONT_ID, chapterTitle.empty() ? title.c_str() : chapterTitle.c_str(), l.textWidth, EpdFontFamily::BOLD);
  const std::string book = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), l.textWidth);
  if (sheetStyle.rightToLeft) {
    renderer.drawText(UI_12_FONT_ID, l.textX + l.textWidth - renderer.getTextWidth(UI_12_FONT_ID, chapter.c_str(),
                                                                                    EpdFontFamily::BOLD),
                      l.chapterY, chapter.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, l.textX + l.textWidth - renderer.getTextWidth(SMALL_FONT_ID, book.c_str()),
                      l.bookY, book.c_str(), true);
  } else {
    renderer.drawText(UI_12_FONT_ID, l.textX, l.chapterY, chapter.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, l.textX, l.bookY, book.c_str(), true);
  }

  // Progress through the book; right-to-left sheets fill from the right.
  const Rect& bar = l.bar;
  const int barRadius = radius > 0 ? bar.height / 2 : 0;
  renderer.drawRoundedRect(bar.x, bar.y, bar.width, bar.height, 1, barRadius, true);
  const int fill = (bar.width - 4) * std::clamp(bookProgressPercent, 0, 100) / 100;
  if (fill > 0) {
    const int fillX = sheetStyle.rightToLeft ? bar.x + bar.width - 2 - fill : bar.x + 2;
    renderer.fillRoundedRect(fillX, bar.y + 2, fill, bar.height - 4, barRadius > 0 ? barRadius - 2 : 0, Color::Black);
  }
  char progress[24];
  if (totalPages > 0) {
    snprintf(progress, sizeof(progress), "%d/%d   %d%%", currentPage, totalPages, bookProgressPercent);
  } else {
    snprintf(progress, sizeof(progress), "%d%%", bookProgressPercent);
  }
  const int progressW = renderer.getTextWidth(SMALL_FONT_ID, progress);
  if (sheetStyle.rightToLeft) {
    // Reading runs leftward: the end of the book is on the left.
    char legend[24];
    snprintf(legend, sizeof(legend), "< %s", tr(STR_PROGRESS_END));
    renderer.drawText(SMALL_FONT_ID, l.textX, l.legendY, legend, true);
    snprintf(legend, sizeof(legend), "%s >", tr(STR_PROGRESS_START));
    renderer.drawText(SMALL_FONT_ID, l.textX + l.textWidth - renderer.getTextWidth(SMALL_FONT_ID, legend), l.legendY,
                      legend, true);
    renderer.drawText(SMALL_FONT_ID, l.textX + (l.textWidth - progressW) / 2, l.legendY, progress, true);
  } else {
    renderer.drawText(SMALL_FONT_ID, l.textX + l.textWidth - progressW, l.legendY, progress, true);
  }

  const int labelH = renderer.getLineHeight(SMALL_FONT_ID);
  for (int i = 0; i < SHEET_TILES; i++) {
    const Rect& t = l.tiles[i];
    const bool selected = i == sheetSelected;
    if (selected) {
      renderer.fillRoundedRect(t.x, t.y, t.width, t.height, radius > 0 ? 10 : 0, Color::Black);
    } else {
      renderer.drawRoundedRect(t.x, t.y, t.width, t.height, 2, radius > 0 ? 10 : 0, true);
    }
    const freeink::Icon& icon = sheetIcon(i);
    const int contentH = icon.h + 4 + labelH;
    const int top = t.y + (t.height - contentH) / 2;
    drawSheetIcon(renderer, icon, t.x + (t.width - icon.w) / 2, top, !selected);
    const std::string label = renderer.truncatedText(SMALL_FONT_ID, I18N.get(sheetLabel(i)), t.width - 8);
    renderer.drawText(SMALL_FONT_ID, t.x + (t.width - renderer.getTextWidth(SMALL_FONT_ID, label.c_str())) / 2,
                      top + icon.h + 4, label.c_str(), !selected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  // Later frames redraw the sheet over whatever this one left above it.
}

void EpubReaderMenuActivity::activateSheetTile(const int index) {
  if (index < 0 || index >= SHEET_TILES) return;
  sheetSelected = index;
  if (index == SHEET_MORE_TILE) {
    sheetMode = false;
    pageBehind = false;
    nav.reset();
    requestUpdate();
    return;
  }
  activateAction(sheetTiles[index].action);
}

bool EpubReaderMenuActivity::handleSheetInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSheetTile(sheetSelected);
    return true;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.hasTouch() && mappedInput.wasScreenTapped(x, y)) {
    const SheetLayout l = sheetLayout();
    for (int i = 0; i < SHEET_TILES; i++) {
      const Rect& t = l.tiles[i];
      if (x >= t.x && x < t.x + t.width && y >= t.y && y < t.y + t.height) {
        activateSheetTile(i);
        return true;
      }
    }
    // A tap on the page above the sheet closes the menu.
    if (y < l.sheet.y) closeCancelled();
    return true;
  }

  const bool left = mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool right = mappedInput.wasPressed(MappedInputManager::Button::Right);
  if (left || right) {
    const int row = sheetSelected / SHEET_COLUMNS;
    const int column = (sheetColumn(sheetSelected) + (right ? 1 : SHEET_COLUMNS - 1)) % SHEET_COLUMNS;
    const int slot = sheetStyle.rightToLeft ? SHEET_COLUMNS - 1 - column : column;
    sheetSelected = row * SHEET_COLUMNS + slot;
    requestUpdate();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    sheetSelected = (sheetSelected + SHEET_COLUMNS) % SHEET_TILES;
    requestUpdate();
    return true;
  }
  // Swallow everything else so the hidden list does not react.
  return true;
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  if (sheetMode) {
    renderSheet();
    return;
  }

  renderer.clearScreen();
  drawChrome();

  renderUi();

  drawFooter();
  renderer.displayBuffer();
}
