#pragma once

#include <GfxRenderer.h>

#include "activities/UiListActivity.h"
#include "activities/util/KeyboardLayoutSet.h"

class MappedInputManager;

class KeyboardLayoutsActivity final : public UiListActivity {
 public:
  explicit KeyboardLayoutsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("KeyboardLayouts", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  // The SDK layouts, then the app-defined Thai row.
  static constexpr int THAI_ROW = keyboard_layouts::COUNT;
  static constexpr int ROW_COUNT = keyboard_layouts::COUNT + 1;
  int listCount() const override { return ROW_COUNT; }
  static uint16_t rowBit(int row);
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  bool isLocked(int row) const;

  freeink::ui::ListItem rowItems[ROW_COUNT]{};

  uint16_t workingMask = 0;
  // Do not persist a derived default merely because the screen was visited.
  bool edited = false;
};
