#pragma once

#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"

// A footnote shown in a window over the current page, as an e-reader shows a note
// without leaving the text. Back (or a tap outside the window) closes it; Confirm opens
// the note in the book; the page keys page through a long note.
class FootnotePopupActivity final : public Activity {
 public:
  // The text area the window offers on this screen; notes are laid out to fit it.
  static void textArea(const GfxRenderer& renderer, int& width, int& height);

  // page: the reader page drawn under the window. On a vertical page marginLeft is the
  // screen column of the page's right edge. notePages: the note laid out for textArea().
  // href: where Confirm takes the reader.
  FootnotePopupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Page> page,
                        int marginLeft, int marginTop, bool vertical, std::vector<std::unique_ptr<Page>> notePages,
                        std::string href)
      : Activity("FootnotePopup", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop),
        vertical(vertical),
        notePages(std::move(notePages)),
        href(std::move(href)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  struct Panel {
    int x, y, width, height;
  };

  void layout();
  void close(bool open);
  void drawContent(int fontId) const;
  int pageCount() const { return static_cast<int>(notePages.size()); }

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  const bool vertical;
  std::vector<std::unique_ptr<Page>> notePages;
  const std::string href;

  Panel panel{};
  int notePage = 0;
};
