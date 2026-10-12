#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class MappedInputManager;

// Settings screen for the Thai user dictionary (see ThaiUserDictionary.h). The first row
// adds a word through the on-screen keyboard; every other row is a word, and activating
// it removes that word. Changes are written and handed to the word breaker on exit.
class ThaiDictionaryActivity final : public UiListActivity {
 public:
  explicit ThaiDictionaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 private:
  static constexpr int ADD_WORD_INDEX = 0;
  static constexpr int FIRST_WORD_INDEX = 1;

  int listCount() const override { return FIRST_WORD_INDEX + static_cast<int>(words.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void rebuildRows();
  void promptForWord();

  std::vector<std::string> words;
  // Row labels point into `words` and `countLabel`; rebuilt under the render lock
  // whenever either changes.
  std::vector<freeink::ui::ListItem> rowItems;
  char countLabel[24] = "";
  bool dirty = false;
};
