#include "KeyboardLayoutsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

uint16_t KeyboardLayoutsActivity::rowBit(const int row) {
  return row == THAI_ROW ? keyboard_layouts::THAI_BIT : keyboard_layouts::bitAt(static_cast<uint8_t>(row));
}

void KeyboardLayoutsActivity::onEnter() {
  UiListActivity::onEnter();
  workingMask = keyboard_layouts::enabled();
  if (keyboard_layouts::thaiEnabled()) workingMask |= keyboard_layouts::THAI_BIT;
  edited = false;

  for (int i = 0; i < ROW_COUNT; ++i) {
    const Language language = i == THAI_ROW ? Language::TH : keyboard_layouts::ALL[i].language;
    rowItems[i].label = I18N.getLanguageName(language);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void KeyboardLayoutsActivity::onExit() {
  if (edited && workingMask != SETTINGS.keyboardLayouts) {
    SETTINGS.keyboardLayouts = workingMask;
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}

const char* KeyboardLayoutsActivity::headerTitle() const { return tr(STR_KEYBOARD_LAYOUTS); }

bool KeyboardLayoutsActivity::isLocked(const int row) const {
  // A Thai UI always offers the Thai layer.
  if (row == THAI_ROW) return I18N.getLanguage() == Language::TH;
  const uint16_t bit = rowBit(row);
  if (!(workingMask & bit)) return false;
  const uint16_t without = static_cast<uint16_t>(workingMask & ~bit);
  return (without & keyboard_layouts::LATIN_BITS) == 0;
}

void KeyboardLayoutsActivity::activateIndex(const int index) {
  nav.selected = index;
  // The row stays on screen with a new checkbox state; a lingering flash would
  // gray an unrelated row on the repaint below.
  app.clearTapFlash();

  if (index < 0 || index >= ROW_COUNT || isLocked(index)) {
    requestUpdate();
    return;
  }

  const uint16_t bit = rowBit(index);
  workingMask = static_cast<uint16_t>(workingMask ^ bit);
  edited = true;
  requestUpdate();
}

void KeyboardLayoutsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (int i = 0; i < ROW_COUNT; ++i) {
    GUI.setCheckboxRow(rowItems[i], (workingMask & rowBit(i)) != 0);
    if (isLocked(i)) {
      rowItems[i].toggle = false;
      rowItems[i].value = tr(STR_DEFAULT_VALUE);
    }
  }

  fui::ListProps props;
  props.items = rowItems;
  props.count = ROW_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
