#include "ThaiDictionaryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "Epub/hyphenation/ThaiWordBreaker.h"
#include "MappedInputManager.h"
#include "ThaiUserDictionary.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// Longest dictionary entry accepted from the keyboard, in bytes (about 20 Thai characters).
constexpr size_t MAX_WORD_BYTES = 63;
}  // namespace

ThaiDictionaryActivity::ThaiDictionaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ThaiDictionary", renderer, mappedInput) {}

void ThaiDictionaryActivity::onEnter() {
  UiListActivity::onEnter();
  ThaiUserDictionary::load(words);
  dirty = false;
  rebuildRows();
}

void ThaiDictionaryActivity::onExit() {
  // One write per visit, and only when something changed: the file lives on the SD card
  // but there is no reason to rewrite it for a look-only visit.
  if (dirty) {
    ThaiUserDictionary::save(words);
    // Section caches key on the dictionary revision, so open books re-lay-out next time.
    ThaiWordBreaker::setUserDictionary(words);
    dirty = false;
  }
  UiListActivity::onExit();
}

const char* ThaiDictionaryActivity::headerTitle() const { return tr(STR_THAI_DICTIONARY); }

void ThaiDictionaryActivity::rebuildRows() {
  rowItems.clear();
  rowItems.reserve(words.size() + FIRST_WORD_INDEX);

  if (words.empty()) {
    snprintf(countLabel, sizeof(countLabel), "%s", tr(STR_DICT_EMPTY));
  } else {
    snprintf(countLabel, sizeof(countLabel), "%d%s", static_cast<int>(words.size()), tr(STR_DICT_WORD_COUNT));
  }

  fui::ListItem add;
  add.label = tr(STR_ADD_WORD);
  add.value = countLabel;
  add.actionValue = ADD_WORD_INDEX;
  add.enabled = words.size() < ThaiUserDictionary::MAX_WORDS;
  rowItems.push_back(add);

  for (size_t i = 0; i < words.size(); ++i) {
    fui::ListItem item;
    item.label = words[i].c_str();
    item.value = tr(STR_DELETE);
    item.actionValue = static_cast<int16_t>(FIRST_WORD_INDEX + i);
    rowItems.push_back(item);
  }
}

void ThaiDictionaryActivity::promptForWord() {
  if (words.size() >= ThaiUserDictionary::MAX_WORDS) return;

  app.clearTapFlash();
  auto handler = [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto& entered = std::get<KeyboardResult>(result.data).text;
    if (entered.empty() || std::find(words.begin(), words.end(), entered) != words.end()) return;

    RenderLock lock(*this);
    words.push_back(entered);
    dirty = true;
    rebuildRows();
    moveSelectionTo(listCount() - 1);
  };
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_THAI_WORD), "",
                                                           MAX_WORD_BYTES, InputType::Text, /*openInThai=*/true);
  if (!keyboard) {
    LOG_ERR("THAI", "OOM: KeyboardEntryActivity");
    return;
  }
  startActivityForResult(std::move(keyboard), handler);
}

void ThaiDictionaryActivity::activateIndex(const int index) {
  if (index == ADD_WORD_INDEX) {
    promptForWord();
    return;
  }

  const int wordIndex = index - FIRST_WORD_INDEX;
  if (wordIndex < 0 || wordIndex >= static_cast<int>(words.size())) return;

  {
    RenderLock lock(*this);
    words.erase(words.begin() + wordIndex);
    dirty = true;
    rebuildRows();
    moveSelectionTo(std::min(index, listCount() - 1));
  }
  requestUpdate();
}

void ThaiDictionaryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void ThaiDictionaryActivity::drawFooter() {
  const char* confirmLabel = activeNav().selected == ADD_WORD_INDEX ? tr(STR_ADD_WORD) : tr(STR_DELETE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
