#include "FootnotePopupActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kPanelMargin = 10;
constexpr int kPadding = 14;
constexpr int kBorder = 2;
}  // namespace

void FootnotePopupActivity::textArea(const GfxRenderer& renderer, int& width, int& height) {
  width = renderer.getScreenWidth() - 2 * kPanelMargin - 2 * kPadding;
  // At most half the screen, so part of the page the note belongs to stays in view; the page
  // indicator's line is always kept free.
  height = renderer.getScreenHeight() / 2 - 2 * kPadding - renderer.getLineHeight(SMALL_FONT_ID);
}

void FootnotePopupActivity::onEnter() {
  Activity::onEnter();
  layout();
  requestUpdate();
}

void FootnotePopupActivity::layout() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int textWidth = 0;
  int textHeight = 0;
  textArea(renderer, textWidth, textHeight);
  // A short note gets a window only as tall as its text. Only plain text lines have a
  // known height; half a line of room covers a larger heading font at the end.
  if (pageCount() == 1) {
    const int lineH = renderer.getLineHeight(SETTINGS.getReaderFontId());
    int bottom = lineH;
    bool linesOnly = true;
    for (const auto& element : notePages[0]->elements) {
      linesOnly = linesOnly && element->getTag() == TAG_PageLine;
      bottom = std::max(bottom, element->yPos + lineH + lineH / 2);
    }
    if (linesOnly) textHeight = std::min(textHeight, bottom);
  }
  panel.x = kPanelMargin;
  panel.width = textWidth + 2 * kPadding;
  panel.height = textHeight + 2 * kPadding + (pageCount() > 1 ? renderer.getLineHeight(SMALL_FONT_ID) : 0);
  // Centred in the space above the button hints.
  const int available = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  panel.y = std::max(kPanelMargin, (available - panel.height) / 2);
  notePage = 0;
}

void FootnotePopupActivity::close(const bool open) {
  ActivityResult result;
  if (open) {
    result.data = FootnoteResult{href};
  } else {
    result.isCancelled = true;
  }
  setResult(std::move(result));
  finish();
}

void FootnotePopupActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    close(false);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    close(true);
    return;
  }

  const int pages = pageCount();
  if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown) ||
      mappedInput.wasPressed(MappedInputManager::Button::ScreenRight)) {
    if (notePage + 1 < pages) {
      notePage++;
      requestUpdate();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp) ||
             mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft)) {
    if (notePage > 0) {
      notePage--;
      requestUpdate();
    }
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const bool inside = x >= panel.x && x < panel.x + panel.width && y >= panel.y && y < panel.y + panel.height;
    if (!inside) {
      close(false);
    } else if (pages > 1) {
      notePage = (notePage + 1) % pages;
      requestUpdate();
    }
  }
}

void FootnotePopupActivity::drawContent(const int fontId) const {
  if (page) page->render(renderer, fontId, marginLeft, marginTop, vertical);
  renderer.fillRect(panel.x, panel.y, panel.width, panel.height, false);
  renderer.drawRect(panel.x, panel.y, panel.width, panel.height, kBorder, true);
  notePages[notePage]->render(renderer, fontId, panel.x + kPadding, panel.y + kPadding, false);
}

void FootnotePopupActivity::render(RenderLock&&) {
  const int fontId = SETTINGS.getReaderFontId();
  renderer.clearScreen();
  {
    // Scan once so SD-font glyphs load together, then draw.
    auto scope = renderer.getFontCacheManager()->createPrewarmScope();
    drawContent(fontId);
    scope.endScanAndPrewarm();
    drawContent(fontId);
  }

  const int pages = pageCount();
  if (pages > 1) {
    char indicator[16];
    snprintf(indicator, sizeof(indicator), "%d/%d", notePage + 1, pages);
    const int w = renderer.getTextWidth(SMALL_FONT_ID, indicator);
    renderer.drawText(SMALL_FONT_ID, panel.x + panel.width - kPadding - w,
                      panel.y + panel.height - kPadding / 2 - renderer.getLineHeight(SMALL_FONT_ID), indicator, true);
  }

  // The front Left/Right and the side buttons page the note; which ones carry the
  // labels depends on the orientation.
  const char* previous = pages > 1 ? tr(STR_DIR_UP) : "";
  const char* next = pages > 1 ? tr(STR_DIR_DOWN) : "";
  const auto labels = mappedInput.mapDirectionalLabels(tr(STR_BACK), tr(STR_OPEN), previous, next, previous, next);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
