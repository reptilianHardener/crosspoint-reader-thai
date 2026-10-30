#include "UiTabListActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cassert>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int16_t TOUCH_TAB_BAR_HEIGHT = 50;
constexpr int VERTICAL_TAB_PAD = 12;  // label inset at each end and on each side
constexpr int VERTICAL_TAB_GAP = 8;
constexpr int VERTICAL_TAB_MIN_LENGTH = 56;
constexpr int VERTICAL_TAB_RULE_GAP = 10;
constexpr int VERTICAL_TAB_INDICATOR = 14;  // room for the sort arrow under the label
}

UiTabListActivity::UiTabListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const bool wantsTouchLongPress)
    : UiListActivity(name, renderer, mappedInput, wantsTouchLongPress) {}

void UiTabListActivity::onEnter() {
  // Size the per-tab state before the base resets activeNav() (which indexes
  // into it).
  tabNavs.assign(static_cast<size_t>(tabCount()), fui::ListNav{});
  UiListActivity::onEnter();
  app.on(ACTION_TAB, &UiTabListActivity::tabActionTrampoline, this);
}

fui::ListNav& UiTabListActivity::activeNav() {
  if (tabNavs.empty()) return nav;  // pre-onEnter fallback
  // Invariant: subclasses keep activeTab() inside [0, tabCount()), and
  // tabCount() does not change after onEnter() sized tabNavs.
  assert(activeTab() >= 0 && static_cast<size_t>(activeTab()) < tabNavs.size());
  return tabNavs[static_cast<size_t>(activeTab())];
}

int UiTabListActivity::ringPos() const {
  if (tabNavs.empty()) return 0;
  assert(activeTab() >= 0 && static_cast<size_t>(activeTab()) < tabNavs.size());
  return tabNavs[static_cast<size_t>(activeTab())].selected;
}

void UiTabListActivity::tabActionTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiTabListActivity*>(user);
  if (event.value < 0 || event.value >= self->tabCount()) return;
  self->onTabAction(event.value);
}

void UiTabListActivity::onRowAction(const fui::ActionEvent& event) {
  activeNav().selected = event.value + 1;  // ring position, not row index
  if (event.longPress) {
    onRowLongPress(event.value);
    return;
  }
  activateIndex(event.value);
}

void UiTabListActivity::moveRingTo(const int ringIndex) {
#if FREEINK_CAP_TOUCH
  closeSwipeDelete();
#endif
  activeNav().requestSelection(ringIndex);
  requestUpdate();
}

void UiTabListActivity::navigateButtons() {
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
      mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    navigationStartedOnTabs = ringPos() == 0;
  }
  // Buttons walk the tab band (index 0) plus the rows (1..listCount).
  const int ringSize = listCount() + 1;
  buttonNavigator.onNextPress([this, ringSize] { moveRingTo(ButtonNavigator::nextIndex(ringPos(), ringSize)); });
  buttonNavigator.onPreviousPress(
      [this, ringSize] { moveRingTo(ButtonNavigator::previousIndex(ringPos(), ringSize)); });
  buttonNavigator.onNextContinuous([this] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
    } else if (ringPos() == 0 && listCount() > 0) {
      activeNav().selected = 1;
    }
    stepTab(1);
  });
  buttonNavigator.onPreviousContinuous([this] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
    } else if (ringPos() == 0 && listCount() > 0) {
      activeNav().selected = 1;
    }
    stepTab(-1);
  });
}

void UiTabListActivity::syncTabListViewport(UiScreen& screen, fui::ListProps& props) {
  syncListViewport(screen, props, 1);
}

void UiTabListActivity::buildVerticalTabBar(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  constexpr int MAX_TABS = 8;
  const int count = tabCount() < MAX_TABS ? tabCount() : MAX_TABS;
  if (count <= 0) return;
  const int fontId = UI_12_FONT_ID;
  const int lineHeight = renderer.getLineHeight(fontId);
  const int columnWidth = lineHeight + 2 * VERTICAL_TAB_PAD;
  const fui::Rect content = screen.contentRect();
  const int columnX = content.x + content.width - metrics.contentSidePadding / 2 - columnWidth;
  const int top = content.y + 4;
  const int available = content.height - 8;

  int lengths[MAX_TABS];
  int total = (count - 1) * VERTICAL_TAB_GAP;
  for (int i = 0; i < count; i++) {
    const int indicator = tabIndicator(i) != fui::TabIndicator::None ? VERTICAL_TAB_INDICATOR : 0;
    lengths[i] = std::max(VERTICAL_TAB_MIN_LENGTH,
                          renderer.getTextWidth(fontId, tabLabel(i)) + 2 * VERTICAL_TAB_PAD + indicator);
    total += lengths[i];
  }
  // Too long for the column: equal lengths, labels truncated to fit.
  if (total > available) {
    const int each = std::max(lineHeight, (available - (count - 1) * VERTICAL_TAB_GAP) / count);
    for (int i = 0; i < count; i++) lengths[i] = each;
  }

  // Selected tab is solid while the tabs have focus, dimmed while the list does.
  const bool tabsFocused = ringPos() == 0;
  int y = top;
  for (int i = 0; i < count; i++) {
    const bool selected = activeTab() == i;
    if (selected && tabsFocused) {
      renderer.fillRect(columnX, y, columnWidth, lengths[i], true);
    } else if (selected) {
      renderer.fillRectDither(columnX, y, columnWidth, lengths[i], Color::DarkGray);
    } else {
      renderer.drawRect(columnX, y, columnWidth, lengths[i], 2, true);
    }
    const fui::TabIndicator arrow = tabIndicator(i);
    const int indicator = arrow != fui::TabIndicator::None ? VERTICAL_TAB_INDICATOR : 0;
    const std::string label =
        renderer.truncatedText(fontId, tabLabel(i), lengths[i] - VERTICAL_TAB_PAD - indicator);
    const int labelLength = renderer.getTextWidth(fontId, label.c_str());
    const int labelY = y + (lengths[i] - labelLength - indicator) / 2;
    renderer.drawTextVertical(fontId, columnX + (columnWidth + lineHeight) / 2, labelY, label.c_str(), !selected);
    if (indicator > 0) {
      const int cx = columnX + columnWidth / 2;
      const int ay = labelY + labelLength + 4;
      const bool up = arrow == fui::TabIndicator::Up;
      const int xs[3] = {cx - 5, cx + 5, cx};
      const int ys[3] = {up ? ay + 7 : ay, up ? ay + 7 : ay, up ? ay : ay + 7};
      renderer.fillPolygon(xs, ys, 3, !selected);
    }
    screen.frame().hit(fui::Rect{static_cast<int16_t>(columnX), static_cast<int16_t>(y),
                                 static_cast<int16_t>(columnWidth), static_cast<int16_t>(lengths[i])},
                       ACTION_TAB, static_cast<int16_t>(i), fui::InputTouch);
    y += lengths[i] + VERTICAL_TAB_GAP;
  }

  const int ruleX = columnX - VERTICAL_TAB_RULE_GAP;
  renderer.fillRect(ruleX, top, 2, available, true);
  const auto listRight = static_cast<int16_t>(content.x + content.width - ruleX + VERTICAL_TAB_RULE_GAP);
  screen.insetContent(fui::Insets{0, listRight, 0, 0});
}

void UiTabListActivity::buildTabBar(UiScreen& screen) {
  if (GUI.verticalTabs()) {
    buildVerticalTabBar(screen);
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Tabs. The selected pill dims to a dither when the selection is down in
  // the list (the legacy focused/unfocused tab distinction).
  // Stack array, not a heap vector: this runs on every render and the tab
  // count is small and fixed.
  constexpr int MAX_TABS = 8;
  const int count = tabCount() < MAX_TABS ? tabCount() : MAX_TABS;
  fui::TabItem tabs[MAX_TABS];
  for (int i = 0; i < count; i++) {
    tabs[i].label = tabLabel(i);
    tabs[i].value = static_cast<int16_t>(i);
    tabs[i].selected = activeTab() == i;
    tabs[i].indicator = tabIndicator(i);
  }
  fui::TabBarProps tabProps;
  tabProps.tabs = tabs;
  tabProps.count = static_cast<uint16_t>(count);
  tabProps.action = ACTION_TAB;
  tabProps.inputMask = fui::InputTouch;
  // Pill shape and label size are theme-driven. Lyra uses equal-width slots
  // with small labels so wide text (e.g. "Controls") still fits at large UI scales.
  // Full-slot (RoundedRaff): the pill fills its slot like the legacy layout
  // (slot minus a 4px frame, 8px clearance above the divider) with
  // body-size labels; zero horizontal contentInset disables the tabBar's
  // label-width shrink.
  const bool tabsFocused = ringPos() == 0;
  if (metrics.tabPillFullSlot) {
    tabProps.text = screen.theme().bodyText;
    tabProps.tabInset = fui::Insets{4, 4, 7, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  } else {
    tabProps.text = screen.theme().smallText;
    tabProps.gap = static_cast<int16_t>(metrics.tabSpacing);
    // Unfocused state: no bottom inset, so the pill (and the 2px selected
    // underline drawn along its bottom edge) reaches the band's 1px divider —
    // legacy Lyra drew the underline sitting on that rule, not floating above.
    tabProps.tabInset = tabsFocused ? fui::Insets{2, 4, 4, 4} : fui::Insets{2, 4, 0, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  }
  const int16_t tabLineHeight = screen.target().lineHeight(tabProps.text.font);
  const int16_t preferredTabHeight =
      mappedInput.hasTouch() ? TOUCH_TAB_BAR_HEIGHT : static_cast<int16_t>(metrics.tabBarHeight);
  const int16_t tabBand = preferredTabHeight > tabLineHeight + 10 ? preferredTabHeight : tabLineHeight + 10;

  if (tabPillMaxPad > 0) {
    // Cap each pill at its label plus this padding: the equal-width slots (and
    // so the tab positions) stay exactly where they were, only the pill stops
    // stretching across the whole slot. The SDK shrinks the pill to content
    // width and centers it in its slot when the horizontal contentInset is
    // nonzero.
    tabProps.contentInset.left = tabPillMaxPad;
    tabProps.contentInset.right = tabPillMaxPad;
  }

  // Legacy Lyra two-state treatment: with the selection on the tab band, the
  // band fills gray and the active tab is a solid pill; with the selection
  // down in the list, the band is plain and the active tab keeps a gray box
  // with an underline. The 1px rule under the band is always there, drawn
  // full-width below (not by tabBar, whose rect is inset for side padding).
  fui::StyleSet tabStyles;
  tabStyles.explicitlySet = true;
  tabStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  if (tabsFocused) {
    tabStyles.selected.background = fui::Paint::solid(fui::Color::Black);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = screen.theme().listRowRadius;
  } else if (metrics.tabPillFullSlot) {
    // Legacy RoundedRaff unfocused treatment: same pill, dimmed to dark gray,
    // text stays inverted; no underline.
    tabStyles.selected.background = fui::Paint::dither(fui::Color::DarkGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = screen.theme().listRowRadius;
  } else {
    tabStyles.selected.background = fui::Paint::dither(fui::Color::LightGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::Black);
    tabProps.selectedUnderline = 2;
  }
  // Focus/flash states keep the pill instead of falling back to an unset
  // (blank) style.
  tabStyles.focused = tabStyles.selected;
  tabStyles.active = tabStyles.selected;
  tabProps.tabStyles = tabStyles;
  const fui::Rect contentTabRect = screen.takeTop(tabBand);
  const fui::Rect frameRect = screen.frame().screen();
  const fui::Rect safeRect = screen.frame().safeRect();
  // The tab background reaches the display edges, but stops before the button labels.
  const fui::Rect tabRect{static_cast<int16_t>(contentTabRect.x - safeRect.x), contentTabRect.y,
                          static_cast<int16_t>(contentTabRect.width + frameRect.width - safeRect.width),
                          contentTabRect.height};
  // Focused band wash is the Lyra treatment; legacy RoundedRaff keeps the
  // band plain in both states.
  if (tabsFocused && !metrics.tabPillFullSlot) {
    screen.target().fill(tabRect, fui::Paint::dither(fui::Color::LightGray));
  }
  // Leave space on both sides of the tab labels and selection highlights.
  // Draw the line below the tabs across the full background width.
  const auto divider = static_cast<int16_t>(std::max(1, metrics.headerUnderlineSize));
  const auto side = static_cast<int16_t>(metrics.contentSidePadding);
  const fui::Rect slotsRect{static_cast<int16_t>(contentTabRect.x + side), tabRect.y,
                            static_cast<int16_t>(contentTabRect.width - 2 * side),
                            static_cast<int16_t>(tabRect.height - divider)};
  tabProps.divider = false;
  fui::tabBar(screen.frame(), slotsRect, tabProps);
  screen.target().fill(fui::Rect{tabRect.x, static_cast<int16_t>(tabRect.bottom() - divider), tabRect.width, divider},
                       fui::Paint::solid(fui::Color::Black));
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
}
