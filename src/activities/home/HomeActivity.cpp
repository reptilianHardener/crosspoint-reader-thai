#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Library, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasLibrarySlot()) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(coverGridUi ? maxBooks : std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::fillCoverGridFromLibrary() {
  if (recentBooks.size() >= CoverGridHomeUi::MAX_BOOKS) return;
  // Keep the index and record together off the task stack; reuse for every row.
  struct LibraryReader {
    library::LibraryIndexFile index;
    library::ClixRecord record;
  };
  auto reader = makeUniqueNoThrow<LibraryReader>();
  if (!reader) {
    LOG_ERR("HOME", "OOM: library index");
    return;
  }
  auto& index = reader->index;
  auto& record = reader->record;
  if (!index.open(library::libraryIndexPath())) {
    index.close();
    GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
    library::BuildStats stats;
    if (!library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0) ||
        !index.open(library::libraryIndexPath())) {
      LOG_ERR("HOME", "Cannot populate cover grid from library");
      return;
    }
  }
  for (uint16_t row = 0; row < index.bookCount() && recentBooks.size() < CoverGridHomeUi::MAX_BOOKS; ++row) {
    RecentBook book;
    if (!index.readRecord(index.ordinalForRow(library::SortOrder::RecentDesc, row), record) ||
        !index.readPath(record, book.path))
      continue;
    if (std::any_of(recentBooks.begin(), recentBooks.end(),
                    [&](const RecentBook& existing) { return existing.path == book.path; }) ||
        RecentBooksStore::isMissing(book))
      continue;
    if (!index.readTitle(record, book.title) && !index.readName(record, book.title)) continue;
    index.readAuthor(record, book.author);
    if (index.ioFailed()) break;
    recentBooks.push_back(std::move(book));
  }
}

void HomeActivity::resolveGridCoverPaths() {
  for (auto& book : recentBooks) {
    if (!book.coverBmpPath.empty()) continue;
    // Constructors only derive cache paths; no metadata parsing or image generation.
    // Keep these large objects off the task stack and release each before the next book.
    if (FsHelpers::hasReflowableBookExtension(book.path)) {
      auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
      if (!epub) {
        LOG_ERR("HOME", "OOM: EPUB thumbnail path");
        continue;
      }
      book.coverBmpPath = epub->getThumbBmpPath();
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
      if (!xtc) {
        LOG_ERR("HOME", "OOM: XTC thumbnail path");
        continue;
      }
      book.coverBmpPath = xtc->getThumbBmpPath();
    }
  }
}

void HomeActivity::loadGridCover(RecentBook& book, int height, bool& showingLoading, Rect& popupRect) {
  if (!book.coverBmpPath.empty() && Storage.exists(UITheme::getCoverThumbPath(book.coverBmpPath, height).c_str()))
    return;
  // Only one parser lives at a time; EPUB/XTC objects exceed the stack budget.
  if (FsHelpers::hasReflowableBookExtension(book.path)) {
    auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("HOME", "OOM: cover EPUB");
      return;
    }
    book.coverBmpPath = epub->getThumbBmpPath();
    if (Storage.exists(epub->getThumbBmpPath(height).c_str())) return;
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
    }
    if (epub->generateThumbBmpFromSource(height)) {
      return;
    }
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("HOME", "OOM: cover XTC");
      return;
    }
    book.coverBmpPath = xtc->getThumbBmpPath();
    if (Storage.exists(xtc->getThumbBmpPath(height).c_str())) return;
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
    }
    if (xtc->load() && xtc->generateThumbBmp(height)) {
      return;
    }
  }
  book.coverBmpPath.clear();
}

namespace {
// Thumbnail sizes a theme may ask for beyond the primary one (carousel side covers).
constexpr int MAX_EXTRA_THUMB_HEIGHTS = 2;
}  // namespace

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    // The cover grid shares one slot size; generating at any other height
    // would rescale the dithered thumb at draw time and alias badly.
    const int thumbHeight = coverGridUi ? coverGridUi->thumbHeightFor() : coverHeight;
    if (coverGridUi) {
      loadGridCover(book, thumbHeight, showingLoading, popupRect);
      ++progress;
      if (showingLoading) GUI.fillPopupProgress(renderer, popupRect, progress * 100 / recentBooks.size());
      continue;
    }
    if (!book.coverBmpPath.empty()) {
      // A carousel theme draws the same cover at several sizes and needs a thumbnail for
      // each: thumbnails are pre-dithered, so they cannot be rescaled at draw time.
      int thumbHeights[1 + MAX_EXTRA_THUMB_HEIGHTS] = {thumbHeight};
      const int thumbCount = 1 + GUI.homeExtraCoverThumbHeights(thumbHeights + 1, MAX_EXTRA_THUMB_HEIGHTS);
      bool missing[1 + MAX_EXTRA_THUMB_HEIGHTS] = {};
      bool anyMissing = false;
      for (int t = 0; t < thumbCount; t++) {
        missing[t] = !Storage.exists(UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeights[t]).c_str());
        anyMissing = anyMissing || missing[t];
      }
      if (anyMissing) {
        // If epub/txt/md, try to load the metadata for title/author and cover
        if (FsHelpers::hasReflowableBookExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          // The primary size decides whether the book has a usable cover at all.
          bool success = !missing[0];
          for (int t = 0; t < thumbCount; t++) {
            if (missing[t] && epub.generateThumbBmp(thumbHeights[t]) && t == 0) success = true;
          }
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = !missing[0];
            for (int t = 0; t < thumbCount; t++) {
              if (missing[t] && xtc.generateThumbBmp(thumbHeights[t]) && t == 0) success = true;
            }
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();
  // Plugin System is the global opt-in. Once enabled, expose the Plugins entry
  // even when no SD plugins are installed yet so Plugin Hub can be installed
  // from inside the Plugins screen (or users can sideload plugins manually).
  hasPlugins = SETTINGS.pluginsEnabled != 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  if (UITheme::getInstance().hasCoverGridHome()) {
    // Screen-lifetime interaction tables and component properties exceed the stack budget.
    coverGridUi = makeUniqueNoThrow<CoverGridHomeUi>(renderer);
    if (!coverGridUi) LOG_ERR("HOME", "OOM: cover grid UI; using standard home");
  }
  loadRecentBooks(coverGridUi ? CoverGridHomeUi::MAX_BOOKS : metrics.homeRecentBooksCount);
  hasContinueReading = !recentBooks.empty();
  if (coverGridUi) {
    fillCoverGridFromLibrary();
    resolveGridCoverPaths();
    coverGridUi->begin(recentBooks, hasLibrarySlot(), hasContinueReading);
  }

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasLibrarySlot());
  lastBookIndex = 0;
  lastMenuColumn = 0;
  GUI.onHomeEnter();

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  coverGridUi.reset();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::activateSelection() {
  if (selectorIndex < recentBooks.size()) {
    onRecentBookSelected(recentBooks[selectorIndex].path);
    return;
  }
  const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
  switch (indexToMenuItem(menuIndex, hasLibrarySlot())) {
    case HomeMenuItem::FILE_BROWSER:
      onFileBrowserOpen();
      break;
    case HomeMenuItem::LIBRARY:
      onLibraryOpen();
      break;
    case HomeMenuItem::OPDS_BROWSER:  // the library slot
      hasPlugins ? onPluginsOpen() : onOpdsBrowserOpen();
      break;
    case HomeMenuItem::FILE_TRANSFER:
      onFileTransferOpen();
      break;
    case HomeMenuItem::SETTINGS_MENU:
      onSettingsOpen();
      break;
    default:
      break;
  }
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Carousel themes own all of their input: four-way navigation instead of a flat cycle.
  if (!coverGridUi && GUI.homeIsCarousel()) {
    handleCarouselInput();
    return;
  }

  // Cover grid home splits navigation by button group (see below), as do row
  // themes (handleRowButtons); the flat next/previous cycle is for the list home.
  const bool rowNavigation = !coverGridUi && GUI.homeRowNavigation();
  if (!coverGridUi && !rowNavigation) {
    buttonNavigator.onNext([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onPrevious([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && hasContinueReading && !recentBooks.empty()) {
    onRecentBookSelected(recentBooks[0].path);
    return;
  }

  if (coverGridUi) {
    const int touched = coverGridUi->selectedAction(mappedInput);
    if (touched >= 0 && touched < menuCount) {
      selectorIndex = touched;
      activateSelection();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activateSelection();
      return;
    }
    // Side page buttons walk the covers, front Left/Right walk the tabs
    // (selectorIndex is flat: books first, then the tab items). A press while
    // selection sits in the other band jumps into this band first.
    const int bookCount = static_cast<int>(recentBooks.size());
    const auto cycleBand = [this](const int base, const int count, const int dir) {
      if (count <= 0) return;
      int idx = selectorIndex - base;
      if (idx < 0 || idx >= count) {
        idx = dir > 0 ? 0 : count - 1;
      } else {
        idx = (idx + count + dir) % count;
      }
      selectorIndex = base + idx;
      requestUpdate();
    };
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up},
                                         [&cycleBand, bookCount] { cycleBand(0, bookCount, -1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                         [&cycleBand, bookCount] { cycleBand(0, bookCount, +1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this, &cycleBand] {
      const int bookCount = static_cast<int>(recentBooks.size());
      cycleBand(bookCount, getMenuItemCount() - bookCount, -1);
    });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this, &cycleBand] {
      const int bookCount = static_cast<int>(recentBooks.size());
      cycleBand(bookCount, getMenuItemCount() - bookCount, +1);
    });
    return;
  }

  // Match the rendered cover grid, which lays tiles inside getContentArea()
  // (origin content.x + contentSidePadding, span content.width). Using
  // screen-relative getScreenWidth()/contentSidePadding here offset the touch
  // targets from the drawn covers by content.x on bezel-inset panels (EEGO A4).
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  const int coverColumnCount = std::max(1, metrics.homeRecentBooksCount);
  const int recentCount = std::min(static_cast<int>(recentBooks.size()), coverColumnCount);
  const int coverColumnWidth = (content.width - 2 * metrics.contentSidePadding) / coverColumnCount;
  int coverTop = metrics.homeTopPadding;
  int coverBottom = metrics.homeTopPadding + metrics.homeCoverTileHeight;
  GUI.homeCoverBand(renderer, Rect{content.x, metrics.homeTopPadding, content.width, metrics.homeCoverTileHeight},
                    coverTop, coverBottom);
  int touchedBook = -1;
  const auto coverTouch = mappedInput.colTouch(touchedBook, content.x + metrics.contentSidePadding, coverColumnWidth,
                                               recentCount, coverTop, coverBottom, coverColumnWidth);
  if (coverTouch != MappedInputManager::RowTouch::None) {
    if (coverTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedBook) {
        selectorIndex = touchedBook;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedBook;
      haptic_feedback::touchAction();
      activateSelection();
    }
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuCount =
      menuCount - (metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));
  int menuRow = -1;
  MappedInputManager::RowTouch menuTouch;
  HomeMenuGrid grid;
  if (GUI.homeMenuGrid(renderer, homeMenuRect(), renderedMenuCount, grid) && grid.columns == renderedMenuCount) {
    // A single row of menu tiles (Banner theme).
    menuTouch = mappedInput.colTouch(menuRow, grid.left, grid.colStep, renderedMenuCount, grid.top,
                                     grid.top + grid.tileHeight, grid.tileWidth);
  } else {
    // Row height from the theme, not the metrics table: RoundedRaff draws
    // font-derived rows and the touch grid must match the visuals exactly.
    const int menuRowHeight = GUI.getMenuRowHeight(renderer);
    menuTouch = mappedInput.rowTouch(menuRow, menuTop, menuRowHeight + metrics.menuSpacing, renderedMenuCount, 0,
                                     INT32_MAX, menuRowHeight);
  }
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      haptic_feedback::touchAction();
      activateSelection();
    }
    return;
  }

  if (rowNavigation && handleRowButtons()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

bool HomeActivity::handleRowButtons() {
  const int bookCount = static_cast<int>(recentBooks.size());
  const int menuCount = getMenuItemCount() - bookCount;
  if (menuCount <= 0) return false;
  const bool inBooks = selectorIndex < bookCount;
  const int menuIndex = selectorIndex - bookCount;

  const bool left = mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool right = mappedInput.wasPressed(MappedInputManager::Button::Right);
  if (left || right) {
    const int dir = right ? 1 : -1;
    if (inBooks) {
      selectorIndex = (selectorIndex + dir + bookCount) % bookCount;
    } else {
      selectorIndex = bookCount + (menuIndex + dir + menuCount) % menuCount;
    }
    requestUpdate();
    return true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (inBooks) {
      lastBookIndex = selectorIndex;
      selectorIndex = bookCount + std::min(lastMenuColumn, menuCount - 1);
    } else if (bookCount > 0) {
      lastMenuColumn = menuIndex;
      selectorIndex = std::min(lastBookIndex, bookCount - 1);
    }
    requestUpdate();
    return true;
  }
  return false;
}

Rect HomeActivity::homeMenuRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  return Rect{content.x, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset,
              content.width,
              renderer.getScreenHeight() - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                                            metrics.homeMenuTopOffset + metrics.buttonHintsHeight)};
}

void HomeActivity::selectIndex(const int newIndex) {
  if (newIndex == selectorIndex) return;
  const int bookCount = static_cast<int>(recentBooks.size());
  // The cached carousel shows one particular centre book and its selection outline, so
  // any move into, out of or within the books invalidates it. render() runs on another
  // task and reads the buffer, hence the lock.
  if (newIndex < bookCount || selectorIndex < bookCount) {
    RenderLock lock(*this);
    coverRendered = false;
    freeCoverBuffer();
  }
  if (newIndex < bookCount) {
    lastBookIndex = newIndex;
  }
  selectorIndex = newIndex;
  requestUpdate();
}

bool HomeActivity::handleCarouselTouch(const int bookCount, const int menuItemCount) {
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Carousel band in thirds: the middle opens the centre book, the outer thirds rotate.
  if (bookCount > 0) {
    int third = -1;
    const int thirdWidth = renderer.getScreenWidth() / 3;
    const auto touch = mappedInput.colTouch(third, 0, thirdWidth, 3, metrics.homeTopPadding,
                                            metrics.homeTopPadding + metrics.homeCoverTileHeight, thirdWidth);
    if (touch != MappedInputManager::RowTouch::None) {
      const int current = selectorIndex < bookCount ? selectorIndex : lastBookIndex;
      if (touch == MappedInputManager::RowTouch::Down) {
        selectIndex(current);
      } else if (third == 1) {
        selectIndex(current);
        activateSelection();
      } else {
        selectIndex((current + (third == 0 ? bookCount - 1 : 1)) % bookCount);
      }
      return true;
    }
  }

  // Menu grid: one row band per column, with the geometry the theme draws.
  HomeMenuGrid grid;
  if (!GUI.homeMenuGrid(renderer, homeMenuRect(), menuItemCount, grid)) return false;
  const int rows = (menuItemCount + grid.columns - 1) / grid.columns;
  for (int col = 0; col < grid.columns; col++) {
    int row = -1;
    const int x = grid.left + col * grid.colStep;
    const auto touch = mappedInput.rowTouch(row, grid.top, grid.rowStep, rows, x, x + grid.tileWidth, grid.tileHeight);
    if (touch == MappedInputManager::RowTouch::None) continue;
    const int menuIndex = row * grid.columns + col;
    if (menuIndex >= menuItemCount) return true;  // the empty cell of an odd-sized grid
    lastMenuColumn = col;
    selectIndex(bookCount + menuIndex);
    if (touch == MappedInputManager::RowTouch::Tap) activateSelection();
    return true;
  }
  return false;
}

void HomeActivity::handleCarouselInput() {
  const int bookCount = static_cast<int>(recentBooks.size());
  const int menuItemCount = getMenuItemCount() - bookCount;

  if (handleCarouselTouch(bookCount, menuItemCount)) return;

  HomeMenuGrid grid;
  const int cols = GUI.homeMenuGrid(renderer, homeMenuRect(), menuItemCount, grid) ? std::max(1, grid.columns) : 1;
  const bool inBooks = selectorIndex < bookCount;
  const int menuIndex = selectorIndex - bookCount;

  // Left/Right: rotate the carousel, or step across the grid row (wrapping within it).
  const bool left = mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool right = mappedInput.wasPressed(MappedInputManager::Button::Right);
  if (left || right) {
    if (inBooks) {
      // "Previews" picks which way the row of covers travels under the buttons.
      const bool previewsRight = SETTINGS.previewDirection == CrossPointSettings::PREVIEW_RIGHT;
      const int step = (left == previewsRight) ? 1 : bookCount - 1;
      selectIndex((selectorIndex + step) % bookCount);
    } else {
      const int row = menuIndex / cols;
      const int target = row * cols + (menuIndex % cols + (left ? cols - 1 : 1)) % cols;
      if (target < menuItemCount) {
        lastMenuColumn = target % cols;
        selectIndex(bookCount + target);
      }
    }
    return;
  }

  // Down: from the carousel into the grid's first row, or one grid row down.
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (inBooks) {
      selectIndex(bookCount + (lastMenuColumn < menuItemCount ? lastMenuColumn : 0));
    } else {
      int target = menuIndex + cols;
      // A short last row: fall back to its first cell.
      if (target >= menuItemCount) target = (menuIndex / cols + 1) * cols;
      if (target < menuItemCount) {
        lastMenuColumn = target % cols;
        selectIndex(bookCount + target);
      }
    }
    return;
  }

  // Up: one grid row up, or from the first row back to the book that was centred.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (!inBooks) {
      lastMenuColumn = menuIndex % cols;
      if (menuIndex >= cols) {
        selectIndex(selectorIndex - cols);
      } else if (bookCount > 0) {
        selectIndex(std::min(lastBookIndex, bookCount - 1));
      }
    }
    return;
  }

  // Back: jump from the grid straight to the carousel.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!inBooks && bookCount > 0) {
      lastMenuColumn = menuIndex % cols;
      selectIndex(std::min(lastBookIndex, bookCount - 1));
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  // Content columns clear the bezel insets on rounded panels. The header
  // self-insets in drawHeader, so only the cover/menu need it here.
  const Rect content = UITheme::getInstance().getContentArea(renderer);

  renderer.clearScreen();
  if (coverGridUi) {
    coverGridUi->setSelection(selectorIndex);
    UITheme::getInstance().drawCoverGridHome(*coverGridUi);
    // Front Left/Right walk the tabs, so their hints read Left/Right; the
    // side page buttons (unhinted) walk the covers.
    const auto labels = mappedInput.mapLabels(hasContinueReading ? tr(STR_RESUME) : "", tr(STR_SELECT),
                                              tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH
                                                                   : HalDisplay::FAST_REFRESH);
    // Slot heights are recorded during the draw above; a change (first layout
    // pass, orientation switch) means the paths must point at those sizes and
    // any missing thumbs must be generated. Refreshing the paths right away
    // lets the next pass draw already-cached thumbs before generation runs.
    const bool coverSpecChanged = coverGridUi->takeThumbHeightChanged();
    if (coverSpecChanged) {
      coverGridUi->refreshCoverPaths();
      recentsLoaded = false;
    }
    if (!firstRenderDone) {
      firstRenderDone = true;
      requestUpdate();
    } else if (!recentsLoaded && !recentsLoading) {
      loadRecentCovers(CoverGridHomeUi::THUMB_HEIGHT);
      coverGridUi->refreshCoverPaths();
      requestUpdate();
    }
    return;
  }
  bool bufferRestored = coverRendered && coverBufferStored && restoreCoverBuffer();

  // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
  // homeTopPadding, so the height must shrink by topPadding or the band (and a
  // centered title, e.g. RoundedRaff's book title) sinks into the tile.
  // Home is the stack root: no back button in its header.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr,
                 nullptr, false);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = content.x;
  coverRectY = metrics.homeTopPadding;
  coverRectW = content.width;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{content.x, metrics.homeTopPadding, content.width, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_LIBRARY), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Library, Transfer, Settings};

  if (hasLibrarySlot()) {
    menuItems.insert(menuItems.begin() + 2, hasPlugins ? tr(STR_PLUGINS) : tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Plugins);
  }

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer, homeMenuRect(), static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  // Carousel themes: Left/Right rotate the books or switch grid column, the side buttons
  // move between rows, and Back returns from the grid to the carousel.
  const bool carousel = GUI.homeIsCarousel();
  const bool horizontal = carousel || GUI.homeRowNavigation();
  const char* backLabel = recentBooks.empty() ? "" : (carousel ? tr(STR_BOOK) : tr(STR_RESUME));
  const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), horizontal ? tr(STR_DIR_LEFT) : tr(STR_DIR_UP),
                                            horizontal ? tr(STR_DIR_RIGHT) : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    const int themeThumbHeight = GUI.homeCoverThumbHeight(renderer);
    loadRecentCovers(themeThumbHeight > 0 ? themeThumbHeight : metrics.homeCoverHeight);
  }
}

void HomeActivity::onRecentBookSelected(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onPluginsOpen() { activityManager.goToPlugins(hasOpdsServers); }
