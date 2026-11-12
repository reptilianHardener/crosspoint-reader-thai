#include "LibraryListActivity.h"

#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryText.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/icons/headerIcons.h"
#include "components/icons/listIcons.h"
#include "components/icons/search32.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookProgress.h"

namespace fui = freeink::ui;

namespace {
constexpr int SIDE_PADDING = 12;

// Side clearance for the footer text in the panel's bottom corners.
int bottomCornerClearance() {
  const auto& board = BoardConfig::ACTIVE;
  return board.bottomCornerRadius != 0 ? board.bottomCornerRadius : board.cornerRadius;
}

constexpr unsigned long LONG_PRESS_MS = 1000;

constexpr int RECENT_TAB = 0;
constexpr int TITLE_TAB = 1;
constexpr int AUTHOR_TAB = 2;
constexpr int SERIES_TAB = 3;
constexpr int TAB_SLOTS = SERIES_TAB + 1;

constexpr bool isDescending(const library::SortOrder order) {
  return order == library::SortOrder::RecentDesc || order == library::SortOrder::TitleDesc ||
         order == library::SortOrder::AuthorDesc;
}

constexpr bool isRecentSort(const library::SortOrder order) {
  return order == library::SortOrder::RecentAsc || order == library::SortOrder::RecentDesc;
}

constexpr bool isAuthorSort(const library::SortOrder order) {
  return order == library::SortOrder::AuthorAsc || order == library::SortOrder::AuthorDesc;
}

constexpr bool isSeriesSort(const library::SortOrder order) { return order == library::SortOrder::Series; }

constexpr library::SortOrder orderForTab(const int tab, const uint8_t descendingTabs) {
  const bool descending = (descendingTabs & (1u << tab)) != 0;
  if (tab == SERIES_TAB) return library::SortOrder::Series;
  if (tab == TITLE_TAB) return descending ? library::SortOrder::TitleDesc : library::SortOrder::TitleAsc;
  if (tab == AUTHOR_TAB) return descending ? library::SortOrder::AuthorDesc : library::SortOrder::AuthorAsc;
  return descending ? library::SortOrder::RecentDesc : library::SortOrder::RecentAsc;
}

const char* tabLabelFor(const int tab) {
  if (tab == TITLE_TAB) return tr(STR_LIBRARY_TAB_TITLE);
  if (tab == AUTHOR_TAB) return tr(STR_LIBRARY_TAB_AUTHOR);
  if (tab == SERIES_TAB) return tr(STR_LIBRARY_TAB_SERIES);
  return tr(STR_LIBRARY_TAB_RECENT);
}

}  // namespace

LibraryListActivity::LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiTabListActivity("Library", renderer, mappedInput, true) {
  // Four short tab labels: a full-slot pill would stretch across a third of
  // the screen, so cap it at the label plus padding (slots stay put).
  tabPillMaxPad = 16;
}

void LibraryListActivity::onEnter() {
  // One lock across the base lifecycle AND the data phase: the base onEnter
  // schedules a paint, and the render task must not read the index or the
  // filter before they are in place. The rebuild also needs the lock: the
  // render task's SD-loaded fonts read glyph data at draw time, and the walk
  // needs the card to itself.
  RenderLock lock(*this);
  UiTabListActivity::onEnter();
  app.on(ACTION_SEARCH, &LibraryListActivity::searchActionTrampoline, this);
  app.on(ACTION_REBUILD, &LibraryListActivity::rebuildActionTrampoline, this);
  app.on(ACTION_BACK, &LibraryListActivity::backActionTrampoline, this);

  // Recent is backed by the resident store. Prune before opening the index so
  // its persistence write never overlaps the long-lived index reader.
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();

  // Rebuild when the index is missing, invalid, or was built with the other
  // metadata mode. Otherwise entering the screen stays instant.
  const bool readMetadata = SETTINGS.libraryUseMetadata != 0;
  const bool rebuildNeeded = library::isLibraryIndexDirty() || !index.open(library::libraryIndexPath()) ||
                             index.header().metadataEnabled != readMetadata;
  if (rebuildNeeded) {
    index.close();
    GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
    rebuildIndex();
    if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot open library index");
  }
  degraded = index.isOpen() && index.ranksDegraded();
  seriesGroupsCached = false;
  if (index.isOpen() && index.dedupDegraded()) {
    LOG_ERR("LIB", "index was built without duplicate detection");
  }
  resolvePinned();

  // Entered while Confirm was still held (typical when launched from the home
  // menu): ignore its release, or we would open whatever sits at row 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate(true);
}

void LibraryListActivity::onExit() {
  index.close();
  readingStatus.reset();
  // Long title lists fill the UI glyph caches; don't pin them on the next screen.
  if (auto* fontCache = renderer.getFontCacheManager()) fontCache->releaseSdFontCaches();
  Activity::onExit();
}

bool LibraryListActivity::rebuildIndex() {
  library::BuildStats stats;
  const bool ok = library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0);
  if (!ok) {
    LOG_ERR("LIB", "index build failed");
    return false;
  }
  LOG_INF("LIB", "reconciled: %u unchanged, %u added, %u renamed, %u removed, %u enriched (%u dup, %u unreadable)",
          static_cast<unsigned>(stats.unchanged), static_cast<unsigned>(stats.added),
          static_cast<unsigned>(stats.renamed), static_cast<unsigned>(stats.removed),
          static_cast<unsigned>(stats.enriched), static_cast<unsigned>(stats.duplicatesDropped),
          static_cast<unsigned>(stats.unreadableSkipped));
  if (stats.dedupDegraded) LOG_ERR("LIB", "rebuild completed without duplicate detection");
  return true;
}

void LibraryListActivity::swallowHeldReleases() {
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  lockNextBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
}

int LibraryListActivity::selectedEntry() const {
  const int entry = ringPos() - 1;
  return entry < 0 ? 0 : entry;
}

// The pinned overlay applies only to the shelf that reads as "what am I up
// to": the unfiltered Recent sort, newest first. A search result is a flat
// list the reader narrowed down on purpose, and the ascending toggle asks for
// oldest-first, which pinned fresh reads would contradict.
int LibraryListActivity::pinnedCount() const {
  if (activeTabIndex != RECENT_TAB || !query.empty() || !isDescending(sortOrder)) return 0;
  return pinnedVisibleCount;
}

const RecentBook* LibraryListActivity::pinnedBook(const int entry) const {
  if (entry < 0 || entry >= pinnedCount()) return nullptr;
  const auto& books = RECENT_BOOKS.getBooks();
  const size_t book = pinnedVisible[entry];
  return book < books.size() ? &books[book] : nullptr;
}

void LibraryListActivity::resolvePinned() {
  const auto& books = RECENT_BOOKS.getBooks();
  pinnedTotal = static_cast<uint8_t>(std::min<size_t>(books.size(), RecentBooksStore::MAX_RECENT_BOOKS));
  for (int i = 0; i < pinnedTotal; i++) pinnedAscRows[i] = 0xFFFF;
  if (pinnedTotal > 0 && index.isOpen()) {
    library::BookIdentity identities[RecentBooksStore::MAX_RECENT_BOOKS];
    for (int i = 0; i < pinnedTotal; i++) {
      const std::string& path = books[static_cast<size_t>(i)].path;
      identities[i].pathHash = library::clixPathHash(path.data(), path.size());
      // Size is only a lookup prefilter; 0 (stat failed, e.g. the index handle
      // is the card's one open reader) falls back to hash-only matching.
      identities[i].fileSize = 0;
      HalFile file;
      if (Storage.openFileForRead("LIB", path.c_str(), file)) {
        identities[i].fileSize = static_cast<uint32_t>(file.fileSize());
      }
    }
    if (!index.recentRowsFor(identities, pinnedTotal, pinnedAscRows)) {
      // Without the match the overlay would duplicate every pinned book that is
      // also in the index; better to drop the pins than to show doubles.
      LOG_ERR("LIB", "recent-book lookup failed; overlay disabled");
      pinnedTotal = 0;
    }
  }
  refreshOverlap();
}

void LibraryListActivity::refreshOverlap() {
  overlapCount = 0;
  pinnedVisibleCount = 0;
  const auto& books = RECENT_BOOKS.getBooks();
  const int total = static_cast<int>(index.bookCount());
  for (int i = 0; i < pinnedTotal && i < static_cast<int>(books.size()); i++) {
    if (statusFilterActive() && !matchesStatusFilter(ReadingStatusStore::keyFor(books[static_cast<size_t>(i)].path))) {
      continue;
    }
    pinnedVisible[pinnedVisibleCount++] = static_cast<uint8_t>(i);
    if (pinnedAscRows[i] == 0xFFFF || pinnedAscRows[i] >= total) continue;
    uint16_t row = isDescending(sortOrder) ? static_cast<uint16_t>(total - 1 - pinnedAscRows[i]) : pinnedAscRows[i];
    if (statusFilterActive()) {
      // The list below the pins is `filtered`, so skip positions there.
      if (!filtered) continue;
      const uint16_t* begin = filtered.get();
      const uint16_t* end = begin + filteredCount;
      const uint16_t* at = std::lower_bound(begin, end, row);
      if (at == end || *at != row) continue;
      row = static_cast<uint16_t>(at - begin);
    }
    overlapRows[overlapCount++] = row;
  }
  std::sort(overlapRows, overlapRows + overlapCount);
}

void LibraryListActivity::openSelectedBook() {
  std::string path;
  if (selectedEntry() < pinnedCount()) {
    const RecentBook* book = pinnedBook(selectedEntry());
    if (!book) return;
    path = book->path;
  } else {
    if (!index.isOpen()) return;
    const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(selectedEntry())));
    if (ordinal == 0xFFFF) return;

    library::ClixRecord record{};
    if (!index.readRecord(ordinal, record) || !index.readPath(record, path)) {
      LOG_ERR("LIB", "cannot resolve path for row %d", selectedEntry());
      return;
    }
  }
  openBookByPath(path);
}

// Shared by row activation and the options menu: the reader screen this opens
// has its own surfaces; a lingering tap flash would gray an unrelated element
// there. The index handle is released first — on hardware only one reader can
// hold a file open at a time, and the reader is about to open files of its own.
void LibraryListActivity::openBookByPath(const std::string& path) {
  app.clearTapFlash();
  index.close();
  onSelectBook(path);
}

void LibraryListActivity::activateIndex(const int index) {
  if (groupsCollapsed && isSeriesSort(sortOrder)) {
    openSeries(index);
  } else if (groupsCollapsed) {
    expandGroup(index);
  } else {
    openSelectedBook();
  }
}

// Row long-press opens the hold menu. Collapsed Title/Author groups have no
// book to act on, so holding one simply expands it.
void LibraryListActivity::onRowLongPress(const int index) {
  if (groupsCollapsed && isSeriesSort(sortOrder)) {
    showSeriesOptions(index);
  } else if (groupsCollapsed) {
    activateIndex(index);
  } else {
    showBookOptions(index);
  }
}

// Book-row hold menu (button hold and touch long-press). On the Recent tab the
// first rows may come from RecentBooksStore; only those can be removed from
// recents. Title and Author lists reach their group view from here.
void LibraryListActivity::showBookOptions(const int entry) {
  if (entry < 0 || entry >= listCount()) return;

  std::string path;
  std::string title;
  const RecentBook* pinned = pinnedBook(entry);
  if (pinned) {
    path = pinned->path;
    title = pinned->title;
  } else {
    if (!index.isOpen()) return;
    const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
    library::ClixRecord record{};
    std::string author;
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record) || !index.readPath(record, path) ||
        !rowTextFor(entry, title, author)) {
      LOG_ERR("LIB", "cannot resolve row %d", entry);
      return;
    }
  }

  enum class Option : uint8_t { Open, RemoveFromRecents, MarkFinished, MarkUnread, Show, Groups, Delete, Rebuild };
  constexpr int MAX_ROW_OPTIONS = 8;
  struct Options {
    Option ids[MAX_ROW_OPTIONS];
    int count = 0;
  } options;
  const char* labels[MAX_ROW_OPTIONS];
  char showLabel[64];
  snprintf(showLabel, sizeof(showLabel), tr(STR_LIBRARY_SHOW_FILTER), statusFilterLabel(statusFilter));
  const auto add = [&options, &labels](const Option id, const char* label) {
    labels[options.count] = label;
    options.ids[options.count++] = id;
  };
  add(Option::Open, tr(STR_OPEN));
  if (pinned) add(Option::RemoveFromRecents, tr(STR_REMOVE_FROM_RECENTS));
  add(Option::MarkFinished, tr(STR_LIBRARY_MARK_FINISHED));
  add(Option::MarkUnread, tr(STR_LIBRARY_MARK_UNREAD));
  add(Option::Show, showLabel);
  if (query.empty() && groupable()) add(Option::Groups, tr(STR_LIBRARY_GROUPS));
  add(Option::Delete, tr(STR_DELETE));
  add(Option::Rebuild, tr(STR_LIBRARY_REBUILD));

  app.clearTapFlash();
  optionPopup.show(tr(STR_LIBRARY), title.c_str(), labels, options.count, 0,
                   [this, path, title, options, entry](const int choice) {
                     swallowHeldReleases();
                     if (choice < 0 || choice >= options.count) return;
                     switch (options.ids[choice]) {
                       case Option::Open:
                         openBookByPath(path);
                         break;
                       case Option::RemoveFromRecents:
                         promptRemoveRecentBook(path, title);
                         break;
                       case Option::MarkFinished:
                         markBook(path, ReadingState::Finished);
                         break;
                       case Option::MarkUnread:
                         markBook(path, ReadingState::Unread);
                         break;
                       case Option::Show:
                         filterMenuPending = true;
                         break;
                       case Option::Groups:
                         collapseGroups(entry);
                         break;
                       case Option::Delete:
                         promptDeleteBookByPath(path, title);
                         break;
                       case Option::Rebuild:
                         promptRebuildIndex();
                         break;
                     }
                   });
  requestUpdate();
}

void LibraryListActivity::showSeriesOptions(const int group) {
  if (group < 0 || group >= groupCount) return;
  std::string name;
  seriesNameForRow(rowAtPosition(groupStarts[group]), name);
  char showLabel[64];
  snprintf(showLabel, sizeof(showLabel), tr(STR_LIBRARY_SHOW_FILTER), statusFilterLabel(statusFilter));
  const char* labels[] = {tr(STR_OPEN), showLabel};
  app.clearTapFlash();
  optionPopup.show(tr(STR_LIBRARY), name.c_str(), labels, 2, 0, [this, group](const int choice) {
    swallowHeldReleases();
    if (choice == 0) {
      openSeries(group);
    } else if (choice == 1) {
      filterMenuPending = true;
    }
  });
  requestUpdate();
}

void LibraryListActivity::showFilterMenu() {
  const char* labels[] = {statusFilterLabel(StatusFilter::All), statusFilterLabel(StatusFilter::Unread),
                          statusFilterLabel(StatusFilter::Reading), statusFilterLabel(StatusFilter::Finished)};
  optionPopup.show(tr(STR_SHOW), labels, 4, static_cast<int>(statusFilter), [this](const int choice) {
    swallowHeldReleases();
    if (choice >= 0 && choice <= static_cast<int>(StatusFilter::Finished)) {
      setStatusFilter(static_cast<StatusFilter>(choice));
    }
  });
  requestUpdate();
}

const char* LibraryListActivity::statusFilterLabel(const StatusFilter filter) {
  switch (filter) {
    case StatusFilter::Unread:
      return tr(STR_LIBRARY_FILTER_UNREAD);
    case StatusFilter::Reading:
      return tr(STR_LIBRARY_FILTER_READING);
    case StatusFilter::Finished:
      return tr(STR_LIBRARY_FILTER_FINISHED);
    case StatusFilter::All:
      break;
  }
  return tr(STR_LIBRARY_FILTER_ALL);
}

bool LibraryListActivity::matchesStatusFilter(const uint64_t pathHash) const {
  if (!readingStatus.isLoaded()) return false;
  const ReadingState state = readingStatus.get(pathHash);
  switch (statusFilter) {
    case StatusFilter::Unread:
      return state == ReadingState::Unread;
    case StatusFilter::Reading:
      return state == ReadingState::Reading;
    case StatusFilter::Finished:
      return state == ReadingState::Finished;
    case StatusFilter::All:
      break;
  }
  return true;
}

void LibraryListActivity::setStatusFilter(const StatusFilter filter, const bool focusList) {
  {
    // Reads the store and the index, and replaces the filter the render task draws from.
    RenderLock lock(*this);
    statusFilter = filter;
    if (!statusFilterActive()) {
      readingStatus.reset();
    } else if (!readingStatus.isLoaded()) {
      loadReadingStatus();
    }
    refilter();
    auto& nav = activeNav();
    nav.selected = focusList && listCount() > 0 ? 1 : 0;
    nav.top = 0;
  }
  requestUpdate(true);
}

// applyFilter() for a changed filter or store: an open series stays open while it
// still has matching books, otherwise the series list comes back with its own nav.
// Callers hold the render lock.
void LibraryListActivity::refilter() {
  const bool inSeries = seriesGroup >= 0 && query.empty() && isSeriesSort(sortOrder);
  const std::string openName = inSeries ? library::fold(seriesTitle) : std::string();
  applyFilter();
  if (!inSeries) return;
  seriesPercents.reset();
  seriesPercentCount = 0;
  std::string name;
  for (int group = 0; group < groupCount; group++) {
    if (!seriesNameForRow(rowAtPosition(groupStarts[group]), name) || library::fold(name) != openName) continue;
    seriesGroup = group;
    groupsCollapsed = false;
    // Positions inside the series moved, so the cached percentages start over.
    const int count = seriesGroupEnd(group) - groupStarts[group];
    seriesPercents = makeUniqueNoThrow<int8_t[]>(static_cast<size_t>(count));
    seriesPercentCount = seriesPercents ? static_cast<uint16_t>(count) : 0;
    for (int i = 0; i < seriesPercentCount; i++) seriesPercents[i] = PERCENT_NOT_READ;
    updateHeaderTitle();
    return;
  }
  activeNav() = seriesListNav;
}

bool LibraryListActivity::loadReadingStatus() {
  if (!readingStatus.load()) return false;
  if (!readingStatus.seeded()) seedReadingStatus();
  return true;
}

// Books read before the store existed have saved progress but no state. One
// pass over the index imports them: an unopened book costs one exists() on its
// cache, an opened EPUB also loads its book.bin (loadBookProgress). Runs once;
// the seeded flag is saved with the result.
void LibraryListActivity::seedReadingStatus() {
  if (!index.isOpen()) return;
  GUI.drawPopup(renderer, tr(STR_LIBRARY_CHECKING_HISTORY));
  bool complete = true;
  std::string path;
  const uint16_t total = index.bookCount();
  for (uint16_t ordinal = 0; ordinal < total; ordinal++) {
    library::ClixRecord record{};
    uint64_t key = 0;
    if (!index.readRecord(ordinal, record) || !index.readPathHash(record, key)) continue;
    // A recorded state, an explicit Unread too, wins over saved progress.
    if (readingStatus.has(key) || !index.readPath(record, path)) continue;
    bool atEnd = false;
    if (loadBookProgress(path, &atEnd) < 0) continue;
    if (!readingStatus.importProgress(key, atEnd)) {
      complete = false;
      break;
    }
  }
  if (complete) readingStatus.markSeeded();
  // Same card discipline as the delete path: no writes while the index is open.
  index.close();
  readingStatus.save();
  if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
}

// The file is the only thing written; the loaded table, when the filter holds
// one, takes the same change afterwards so the two never disagree.
void LibraryListActivity::markBook(const std::string& path, const ReadingState state) {
  {
    RenderLock lock(*this);
    index.close();
    if (ReadingStatusStore::update(path, state) && readingStatus.isLoaded() &&
        !readingStatus.set(ReadingStatusStore::keyFor(path), state)) {
      readingStatus.load();
    }
    if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    if (statusFilterActive()) {
      refilter();
      clampSelection();
    }
  }
  requestUpdate(true);
}

void LibraryListActivity::clampSelection() {
  auto& nav = activeNav();
  const int count = listCount();
  if (count == 0) {
    nav.selected = 0;
  } else if (nav.selected > count) {
    nav.selected = count;
  }
  nav.followOnBuild = true;
}

#if FREEINK_CAP_TOUCH
void LibraryListActivity::swipeDelete(const int entry) {
  activeNav().selected = entry + 1;
  if (const RecentBook* book = pinnedBook(entry)) {
    promptDeleteBookByPath(book->path, book->title);
  } else {
    promptDeleteBook(entry);
  }
}
#endif

// Manual index refresh, same card discipline as the onEnter rebuild: the walk
// wants the card to itself, and the render task must not read the index (or
// the filter) around it.
void LibraryListActivity::promptRebuildIndex() {
  RenderLock lock(*this);
  GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
  index.close();
  rebuildIndex();
  if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot open library index");
  resetAfterRebuild();
  requestUpdate(true);
}

void LibraryListActivity::resetAfterRebuild() {
  // Sort positions, group starts, and pinned rows all point into the old order.
  seriesGroupsCached = false;
  applyFilter();
  resolvePinned();
  auto& nav = activeNav();
  const int count = listCount();
  if (count == 0) {
    nav.selected = 0;
  } else if (nav.selected > count) {
    nav.selected = count;
  }
  nav.followOnBuild = true;
}

void LibraryListActivity::promptRemoveRecentBook(const std::string& path, const std::string& title) {
  const bool reopenIndex = index.isOpen();
  index.close();
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title);
  if (!confirmation) {
    LOG_ERR("LIB", "OOM: recent removal confirmation");
    if (reopenIndex && !index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    return;
  }

  startActivityForResult(std::move(confirmation), [this, path, reopenIndex](const ActivityResult& result) {
    swallowHeldReleases();
    if (reopenIndex && !index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    if (!result.isCancelled && RECENT_BOOKS.removeByPath(path)) {
      resolvePinned();
      closeRouting();
      auto& nav = activeNav();
      const int count = listCount();
      if (count == 0) {
        nav.selected = 0;
      } else if (nav.selected > count) {
        nav.selected = count;
      }
      nav.followOnBuild = true;
    }
  });
}

void LibraryListActivity::promptDeleteBook(const int entry) {
  if (!index.isOpen() || entry < 0 || entry >= bookRowCount()) return;
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  if (ordinal == 0xFFFF) return;

  std::string path;
  library::ClixRecord record{};
  if (!index.readRecord(ordinal, record) || !index.readPath(record, path)) {
    LOG_ERR("LIB", "cannot resolve path for row %d", entry);
    return;
  }
  std::string title;
  std::string author;
  rowTextFor(entry, title, author);
  promptDeleteBookByPath(path, title);
}

void LibraryListActivity::promptDeleteBookByPath(const std::string& path, const std::string& title) {
  // The dialog and the delete both want the card; reopen when we resume.
  index.close();
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ITEM_CONFIRM), title);
  if (!confirmation) {
    LOG_ERR("LIB", "OOM: delete confirmation");
    if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    return;
  }

  startActivityForResult(std::move(confirmation), [this, path](const ActivityResult& result) {
    swallowHeldReleases();
    {
      // Same lock rationale as onEnter: the walk wants the card to itself, and
      // the render task must not read the index (or the filter) around the
      // rebuild.
      RenderLock lock(*this);
      if (!result.isCancelled) {
        LOG_DBG("LIB", "deleting %s", path.c_str());
        if (!removeBookFile(path)) LOG_ERR("LIB", "cannot delete %s", path.c_str());
        // removeBookFile dropped the record from the file; keep the loaded copy in step.
        if (!Storage.exists(path.c_str())) readingStatus.erase(ReadingStatusStore::keyFor(path));
        if (RECENT_BOOKS.removeByPath(path)) RECENT_BOOKS.saveToFile();
        GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
        rebuildIndex();
      }
      if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
      if (!result.isCancelled) {
        resetAfterRebuild();
      }
    }
    if (!result.isCancelled) {
      closeRouting();
      requestUpdate(true);
    }
  });
}

void LibraryListActivity::openSearch() {
  app.clearTapFlash();
  // Refining a Thai query should not start with a trip through the language key.
  bool thaiQuery = false;
  for (const auto* p = reinterpret_cast<const unsigned char*>(query.c_str()); !thaiQuery;) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    thaiQuery = utf8IsThaiCodepoint(cp);
  }
  // No key filtering here on purpose. Greying out the letters that lead nowhere
  // was built, tested on device and removed: a letter you can see but cannot
  // reach reads as a broken keyboard, and the eye keeps returning to it.
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_LIBRARY_SEARCH), query, 48,
                                                           InputType::Text, thaiQuery);
  if (!keyboard) {
    LOG_ERR("LIB", "OOM: search keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    swallowHeldReleases();
    if (result.isCancelled) return;
    query = std::get<KeyboardResult>(result.data).text;
    applyFilter();
    auto& nav = activeNav();
    if (listCount() == 0 && !degraded) {
      // Up from the tab bar reopens Search even with no results.
      nav.selected = 0;
    } else {
      // A non-empty result belongs to the list: land on
      // its first surviving row, not on the strip.
      nav.selected = 1;
    }
    nav.top = 0;
    requestUpdate();
  });
}

void LibraryListActivity::stepTab(const int direction) {
  const int next = (activeTab() + (direction > 0 ? 1 : TAB_SLOTS - 1)) % TAB_SLOTS;
  selectTab(next, false);
}

void LibraryListActivity::onTabAction(const int index) {
  app.clearTapFlash();
  selectTab(index, true);
}

void LibraryListActivity::selectTab(const int index, const bool toggleIfActive) {
#if FREEINK_CAP_TOUCH
  closeSwipeDelete();
#endif
  if (index < 0 || index >= TAB_SLOTS) return;
  // Series has one direction: volumes always run upward.
  if (toggleIfActive && index == activeTab() && index != SERIES_TAB) {
    descendingTabs ^= static_cast<uint8_t>(1u << index);
  }
  sortOrder = orderForTab(index, descendingTabs);
  seriesGroup = -1;
  // The filter and the overlap rows hold positions in the old order, so they
  // must be rebuilt.
  applyFilter();
  activeTabIndex = index;
  refreshOverlap();
  // Tab changes happen only while the bar owns focus. A tab's remembered row
  // must not pull focus back into the list after the switch.
  auto& nav = activeNav();
  nav.selected = 0;
  nav.top = 0;
  requestUpdate();
}

void LibraryListActivity::toggleSortDirection() { selectTab(activeTab(), true); }

int LibraryListActivity::tabCount() const { return TAB_SLOTS; }

int LibraryListActivity::activeTab() const { return activeTabIndex; }

const char* LibraryListActivity::tabLabel(const int index) const { return tabLabelFor(index); }

fui::TabIndicator LibraryListActivity::tabIndicator(const int index) const {
  if (index != activeTab() || isSeriesSort(sortOrder)) return fui::TabIndicator::None;
  return isDescending(sortOrder) ? fui::TabIndicator::Down : fui::TabIndicator::Up;
}

int LibraryListActivity::bookRowCount() const {
  if (!query.empty()) return static_cast<int>(filteredCount);
  const int listed = statusFilterActive() ? static_cast<int>(filteredCount) : static_cast<int>(index.bookCount());
  if (isSeriesSort(sortOrder)) {
    if (seriesGroup >= 0) return std::max(0, seriesGroupEnd(seriesGroup) - groupStarts[seriesGroup]);
    return statusFilterActive() ? listed : static_cast<int>(index.seriesCount());
  }
  // Pinned books already in the index are skipped below the pins, not doubled;
  // pinned books the index missed still show, so the difference stays split.
  const int pinned = pinnedCount();
  return listed + (pinned > 0 ? pinned - overlapCount : 0);
}

int LibraryListActivity::listCount() const { return groupsCollapsed ? static_cast<int>(groupCount) : bookRowCount(); }

// Entry position on screen to row position in the sort order. Identity while
// unfiltered and unpinned, so the shelf costs nothing when nothing is typed.
// With pins active, entries below pinnedCount() belong to the store and must
// not reach this; the rest walk past the pinned books' own sort rows.
int LibraryListActivity::rowFor(const int entry) const {
  if (!query.empty()) {
    if (entry < 0 || entry >= static_cast<int>(filteredCount) || !filtered) return 0;
    return filtered[entry];
  }
  if (isSeriesSort(sortOrder) && seriesGroup >= 0) return rowAtPosition(groupStarts[seriesGroup] + entry);
  const int pinned = pinnedCount();
  if (pinned == 0) return rowAtPosition(entry);
  int position = entry - pinned;
  for (int i = 0; i < overlapCount; i++) {
    if (overlapRows[i] <= position) position++;
  }
  return rowAtPosition(position);
}

int LibraryListActivity::rowAtPosition(const int position) const {
  if (!statusFilterActive()) return position;
  if (!filtered || position < 0 || position >= static_cast<int>(filteredCount)) return 0;
  return filtered[position];
}

// Series groups belong to the Series tab itself, not to the hold-to-collapse gesture.
bool LibraryListActivity::groupable() const {
  return !degraded && !isRecentSort(sortOrder) && !isSeriesSort(sortOrder) && bookRowCount() > 0;
}

uint32_t LibraryListActivity::titleInitialFor(const int entry) {
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) return 0;
  return library::foldedGroupInitial(std::string_view(record.fold, record.foldLen));
}

bool LibraryListActivity::buildGroupStarts() {
  const int count = bookRowCount();
  if (count <= 0) return false;
  seriesGroupsCached = false;  // groupStarts is about to hold another tab's groups
  if (groupCapacity < count) {
    auto starts = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(count));
    if (!starts) {
      LOG_ERR("LIB", "cannot allocate %u-byte group map", static_cast<unsigned>(count * sizeof(uint16_t)));
      return false;
    }
    groupStarts = std::move(starts);
    groupCapacity = static_cast<uint16_t>(count);
  }

  groupCount = 0;
  uint32_t previousInitial = 0;
  std::string previousAuthor;
  std::string title;
  std::string author;
  previousAuthor.reserve(128);
  title.reserve(128);
  author.reserve(128);
  for (int entry = 0; entry < count; entry++) {
    bool startsGroup = entry == 0;
    if (isAuthorSort(sortOrder)) {
      rowTextFor(entry, title, author);
      startsGroup = startsGroup || author != previousAuthor;
      previousAuthor = author;
    } else {
      const uint32_t initial = titleInitialFor(entry);
      startsGroup = startsGroup || initial != previousInitial;
      previousInitial = initial;
    }
    if (startsGroup) groupStarts[groupCount++] = static_cast<uint16_t>(entry);
  }
  LOG_DBG("LIB", "group map: %u groups, %u bytes", static_cast<unsigned>(groupCount),
          static_cast<unsigned>(groupCapacity * sizeof(uint16_t)));
  return groupCount > 0;
}

int LibraryListActivity::groupForBook(const int bookEntry) const {
  int group = 0;
  while (group + 1 < groupCount && groupStarts[group + 1] <= bookEntry) group++;
  return group;
}

bool LibraryListActivity::collapseGroups(const int bookEntry) {
  if (!groupable() || !buildGroupStarts()) return false;
  expandedNav = activeNav();
  groupsCollapsed = true;
  auto& nav = activeNav();
  nav.reset(groupForBook(bookEntry) + 1);
  requestUpdate();
  return true;
}

void LibraryListActivity::expandGroup(const int groupEntry) {
  if (!groupsCollapsed || groupEntry < 0 || groupEntry >= groupCount) return;
  const int bookEntry = groupStarts[groupEntry];
  groupsCollapsed = false;
  activeNav() = expandedNav;
  auto& nav = activeNav();
  nav.selected = bookEntry + 1;
  nav.top = bookEntry;
  nav.followOnBuild = true;
  requestUpdate();
}

void LibraryListActivity::restoreExpandedList() {
  if (!groupsCollapsed) return;
  groupsCollapsed = false;
  activeNav() = expandedNav;
  requestUpdate();
}

int LibraryListActivity::seriesGroupEnd(const int group) const {
  return group + 1 < groupCount ? groupStarts[group + 1] : static_cast<int>(seriesRowTotal);
}

bool LibraryListActivity::seriesNameForRow(const int row, std::string& out) {
  out.clear();
  const uint16_t ordinal = index.ordinalForRow(library::SortOrder::Series, static_cast<uint16_t>(row));
  library::ClixRecord record{};
  return ordinal != 0xFFFF && index.readRecord(ordinal, record) && index.readSeries(record, out);
}

// One group per series. The builder sorted by the folded name, so books whose
// series differ only in case or spacing are adjacent and share a group.
// Under a status filter the groups cover only the passing books, so a series
// with none of them is hidden; that list is never cached.
bool LibraryListActivity::buildSeriesGroups() {
  groupCount = 0;
  const bool filteredSeries = statusFilterActive();
  if (seriesGroupsCached && !filteredSeries) {
    groupCount = seriesGroupTotal;
    return groupCount > 0;
  }
  seriesGroupsCached = false;
  const int count = filteredSeries ? static_cast<int>(filteredCount) : static_cast<int>(index.seriesCount());
  seriesRowTotal = static_cast<uint16_t>(count);
  if (count <= 0) return false;
  if (groupCapacity < count) {
    auto starts = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(count));
    if (!starts) {
      LOG_ERR("LIB", "cannot allocate %u-byte series map", static_cast<unsigned>(count * sizeof(uint16_t)));
      return false;
    }
    groupStarts = std::move(starts);
    groupCapacity = static_cast<uint16_t>(count);
  }
  std::string name;
  std::string folded;
  std::string previous;
  for (int position = 0; position < count; position++) {
    seriesNameForRow(rowAtPosition(position), name);
    folded = library::fold(name);
    if (position == 0 || folded != previous) groupStarts[groupCount++] = static_cast<uint16_t>(position);
    previous.swap(folded);
  }
  seriesGroupTotal = groupCount;
  seriesGroupsCached = !filteredSeries;
  return groupCount > 0;
}

void LibraryListActivity::showSeriesList() {
  seriesGroup = -1;
  groupsCollapsed = buildSeriesGroups();
}

void LibraryListActivity::openSeries(const int group) {
  if (group < 0 || group >= groupCount) return;
  // Reads the index and replaces seriesPercents, both of which the render task uses.
  RenderLock lock(*this);
  seriesListNav = activeNav();
  seriesGroup = group;
  groupsCollapsed = false;
  seriesNameForRow(rowAtPosition(groupStarts[group]), seriesTitle);
  updateHeaderTitle();
  const int count = seriesGroupEnd(group) - groupStarts[group];
  seriesPercents = makeUniqueNoThrow<int8_t[]>(static_cast<size_t>(count));
  seriesPercentCount = seriesPercents ? static_cast<uint16_t>(count) : 0;
  for (int i = 0; i < seriesPercentCount; i++) seriesPercents[i] = PERCENT_NOT_READ;
  auto& nav = activeNav();
  nav.reset(1 + mostRecentSeriesEntry(groupStarts[group], count));
  nav.followOnBuild = true;
  requestUpdate();
}

int LibraryListActivity::mostRecentSeriesEntry(const int firstPosition, const int count) {
  // resolvePinned() matched the recent books (newest first) to index rows, so
  // only 2-byte orders are read here: a lower rank is a more recent book.
  uint16_t ordinals[RecentBooksStore::MAX_RECENT_BOOKS];
  for (int r = 0; r < pinnedTotal; r++) {
    ordinals[r] =
        pinnedAscRows[r] == 0xFFFF ? 0xFFFF : index.ordinalForRow(library::SortOrder::RecentAsc, pinnedAscRows[r]);
  }
  int best = 0;
  int bestRank = pinnedTotal;
  for (int entry = 0; entry < count && bestRank > 0; entry++) {
    const uint16_t ordinal =
        index.ordinalForRow(library::SortOrder::Series, static_cast<uint16_t>(rowAtPosition(firstPosition + entry)));
    if (ordinal == 0xFFFF) continue;
    for (int r = 0; r < bestRank; r++) {
      if (ordinals[r] == ordinal) {
        best = entry;
        bestRank = r;
        break;
      }
    }
  }
  return best;
}

int LibraryListActivity::seriesPercentFor(const int entry, const library::ClixRecord& record) {
  if (seriesGroup < 0 || !query.empty() || !seriesPercents || entry < 0 || entry >= seriesPercentCount) return -1;
  if (seriesPercents[entry] == PERCENT_NOT_READ) {
    std::string path;
    seriesPercents[entry] = static_cast<int8_t>(index.readPath(record, path) ? loadBookProgress(path) : -1);
  }
  return seriesPercents[entry];
}

void LibraryListActivity::closeSeries() {
  RenderLock lock(*this);
  seriesPercents.reset();
  seriesPercentCount = 0;
  seriesGroup = -1;
  groupsCollapsed = true;
  updateHeaderTitle();
  activeNav() = seriesListNav;
  requestUpdate();
}

void LibraryListActivity::formatSeriesSubtitle(const library::ClixRecord& record, const std::string& author,
                                               std::string& out) {
  out.clear();
  float volume = 0;
  if (record.nameLen > 0 && index.readSeriesIndex(record, volume)) {
    char text[48];
    snprintf(text, sizeof(text), tr(STR_LIBRARY_SERIES_VOLUME), library::seriesPositionText(volume).c_str());
    out = text;
  }
  if (!author.empty()) {
    if (!out.empty()) out += ", ";
    out += author;
  }
}

// One pass over the sort order, keeping what matches the query and the status
// filter. No index, no cache: at the 4096-book format cap this is 4096
// comparisons of at most 96 bytes, plus one 8-byte path-hash read per row under
// a status filter. The result array is allocated once with the exact upper bound
// and fails back to an explicit message rather than letting vector growth abort
// the firmware.
void LibraryListActivity::applyFilter() {
  groupsCollapsed = false;
  groupCount = 0;
  seriesGroup = -1;
  filtered.reset();
  filteredCount = 0;
  filterFailed = false;
  if (filterActive()) filterRows();
  // A search is a flat list; the status filter alone keeps the series groups.
  if (query.empty() && isSeriesSort(sortOrder)) showSeriesList();
  refreshOverlap();
  updateHeaderTitle();
}

void LibraryListActivity::filterRows() {
  if (statusFilterActive() && !readingStatus.isLoaded()) {
    filterFailed = true;
    return;
  }
  const std::string needle = library::fold(query);
  const int total = static_cast<int>(index.rowCount(sortOrder));
  if (total <= 0) return;

  auto matches = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(total));
  if (!matches) {
    LOG_ERR("LIB", "cannot allocate %u-byte filter result buffer", static_cast<unsigned>(total * sizeof(uint16_t)));
    filterFailed = true;
    return;
  }

  uint16_t matchCount = 0;
  std::string author;
  for (int row = 0; row < total; row++) {
    const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(row));
    library::ClixRecord record{};
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) continue;
    if (statusFilterActive()) {
      uint64_t pathHash = 0;
      if (!index.readPathHash(record, pathHash) || !matchesStatusFilter(pathHash)) continue;
    }
    if (query.empty() || library::matchesQuery(std::string_view(record.fold, record.foldLen), needle)) {
      matches[matchCount++] = static_cast<uint16_t>(row);
      continue;
    }
    // The stored fold covers the title only, so the author has to be read and
    // folded here. That is the search most worth having: the reader who knows
    // the author usually also knows where the book is, while "emily" finding
    // Alice Hunter is the case the shelf exists to answer.
    author.clear();
    if (index.readAuthor(record, author) && library::matchesQuery(library::fold(author), needle)) {
      matches[matchCount++] = static_cast<uint16_t>(row);
    }
  }
  filtered = std::move(matches);
  filteredCount = matchCount;
}

// Staged back-out, shared by the Back button and the header's back arrow:
// clear the search, expand collapsed groups, return focus to the tabs, clear
// the status filter, then leave for home.
void LibraryListActivity::handleBackAction() {
  auto& nav = activeNav();
  if (!query.empty()) {
    query.clear();
    applyFilter();
    nav.selected = 0;
    nav.top = 0;
    requestUpdate();
  } else if (seriesGroup >= 0) {
    closeSeries();
  } else if (groupsCollapsed && !isSeriesSort(sortOrder)) {
    restoreExpandedList();
  } else if (!tabsFocused() && !degraded) {
    // Keep the current list and viewport while returning focus to the tabs.
    nav.selected = 0;
    requestUpdate();
  } else if (statusFilterActive()) {
    setStatusFilter(StatusFilter::All, false);
  } else {
    onGoHome();
  }
}

void LibraryListActivity::searchActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->openSearch();
}

void LibraryListActivity::backActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->handleBackAction();
}

void LibraryListActivity::rebuildActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->promptRebuildIndex();
}

// Title and author for one entry, read straight from the index. Only ever
// called for rows about to be drawn, so at most a screenful of strings exists
// at once.
bool LibraryListActivity::rowTextFor(const int entry, std::string& title, std::string& author, std::string* fileName,
                                     library::ClixRecord* recordOut) {
  title.clear();
  author.clear();
  if (fileName) fileName->clear();
  if (recordOut) *recordOut = {};
  if (entry < pinnedCount()) {
    const RecentBook* pinned = pinnedBook(entry);
    if (!pinned) return false;
    const auto& book = *pinned;
    title = book.title;
    author = book.author;
    if (fileName) *fileName = book.path;
    return true;
  }
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  if (ordinal != 0xFFFF && index.readRecord(ordinal, record)) {
    // The build already decided both fields — from the book's own metadata when
    // it has any, and with one spelling chosen per author across the library.
    // Re-parsing the name here would throw that away, and only works while the
    // name still looks like "Title - Author".
    if (!index.readAuthor(record, author)) author.clear();
    // The stored title when the book gave one, the filename otherwise.
    if (!index.readTitle(record, title) || title.empty()) index.readName(record, title);
    if (fileName) index.readName(record, *fileName);
    if (recordOut) *recordOut = record;
  }
  if (title.empty()) title = tr(STR_LIBRARY_UNKNOWN_TITLE);
  return true;
}

bool LibraryListActivity::handleCustomInput() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (filterMenuPending) {
      filterMenuPending = false;
      showFilterMenu();
    }
    return true;
  }

  if (lockNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lockNextConfirmRelease = false;
    return true;
  }
  if (lockNextBackRelease && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockNextBackRelease = false;
    return true;
  }

  return false;
}

bool LibraryListActivity::handleButtons() {
  const int count = listCount();

  // Every hold action fires at the threshold, mid-hold, including the ones
  // that open a dialog (remove-recent, delete). The release that follows is
  // armed as suppressed by wasLongPressed() and consumed globally by
  // ActivityManager::loop() before any activity runs, so it cannot land in
  // the freshly opened confirmation and select its default.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, LONG_PRESS_MS)) {
    if (tabsFocused()) {
      if (!degraded) toggleSortDirection();
    } else if (count > 0) {
      onRowLongPress(selectedEntry());
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    handleBackAction();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (tabsFocused()) {
      stepTab(1);
      return true;
    }
    if (count > 0) activateIndex(selectedEntry());
    return true;
  }

  return false;
}

void LibraryListActivity::navigateButtons() {
  const int count = listCount();
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
      mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    navigationStartedOnTabs = tabsFocused();
  }
  buttonNavigator.onNextPress([this, count] {
    if (count > 0) moveRingTo(ringPos() == count ? 1 : ringPos() + 1);
  });
  buttonNavigator.onPreviousPress([this, count] {
    if ((!navigationStartedOnTabs || degraded) && count > 0) {
      moveRingTo(ringPos() <= 1 ? count : ringPos() - 1);
    }
  });
  // Search is an activation: defer it so holding Previous can still step tabs.
  buttonNavigator.onPreviousRelease([this] {
    if (navigationStartedOnTabs && tabsFocused() && !degraded) openSearch();
  });
  // A held button steps tabs while the strip has focus (the base behaviour
  // Settings keeps) and page-jumps once the selection is down in the rows,
  // where fast travel through a long shelf is what a hold means.
  buttonNavigator.onNextContinuous([this, count] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
      stepTab(1);
    } else if (count > 0) {
      moveRingTo(ButtonNavigator::nextPageIndex(selectedEntry(), count, activeNav().pageRows()) + 1);
    }
  });
  buttonNavigator.onPreviousContinuous([this, count] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
      stepTab(-1);
    } else if (count > 0) {
      moveRingTo(ButtonNavigator::previousPageIndex(selectedEntry(), count, activeNav().pageRows()) + 1);
    }
  });
}

void LibraryListActivity::buildRows(UiScreen& screen) {
  auto& nav = activeNav();
  const int count = listCount();
  const bool authorGrouped = isAuthorSort(sortOrder);
  const bool seriesRows = isSeriesSort(sortOrder);
  const bool grouped = !isRecentSort(sortOrder) && !seriesRows;

  fui::ListProps props;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;
  // Breathing room between rows; the dense theme default packs the two-line
  // rows edge-to-edge.
  props.rowGap = std::max<int16_t>(screen.theme().listRowGap, 6);
  props.headerUnderline = false;
  syncTabListViewport(screen, props);

  // Keep one extra entry in the reusable window for a clipped trailing row.
  const size_t cap = static_cast<size_t>(nav.visibleRows > 0 ? nav.visibleRows : 1) + 1;
  if (winTitles.size() < cap) winTitles.resize(cap);
  if (winAuthors.size() < cap) winAuthors.resize(cap);
  if (!groupsCollapsed && winHeaders.size() < cap) winHeaders.resize(cap);
  winItems.clear();
  if (winItems.capacity() < cap) winItems.reserve(cap);

  int rows = 0;
  int headers = 0;
  uint32_t previousInitial = 0;
  std::string rowFile;
  rowFile.reserve(128);
  // Capture this after syncTabListViewport(), which may clamp nav.top.
  const int windowStart = static_cast<int>(props.topIndex);
  for (int entry = windowStart; entry < count && rows < static_cast<int>(cap); entry++) {
    std::string& title = winTitles[static_cast<size_t>(rows)];
    std::string& author = winAuthors[static_cast<size_t>(rows)];
    fui::ListItem item;
    if (groupsCollapsed && seriesRows) {
      seriesNameForRow(rowAtPosition(groupStarts[entry]), title);
      char count[24];
      snprintf(count, sizeof(count), tr(STR_LIBRARY_SERIES_BOOKS), seriesGroupEnd(entry) - groupStarts[entry]);
      author = count;
      item.subtitle = author.c_str();
    } else if (groupsCollapsed) {
      const int bookEntry = groupStarts[entry];
      if (authorGrouped) {
        rowTextFor(bookEntry, title, author);
        formatAuthorHeading(author, title);
      } else {
        formatInitialHeading(titleInitialFor(bookEntry), title);
      }
    } else {
      library::ClixRecord record{};
      if (!rowTextFor(entry, title, author, &rowFile, seriesRows ? &record : nullptr)) continue;
      uint32_t initial = 0;
      bool startsGroup = false;
      if (authorGrouped) {
        startsGroup = rows == 0 || author != winAuthors[static_cast<size_t>(rows - 1)];
      } else if (grouped) {
        initial = titleInitialFor(entry);
        startsGroup = rows == 0 || initial != previousInitial;
        previousInitial = initial;
      }
      if (startsGroup) {
        std::string& heading = winHeaders[static_cast<size_t>(headers++)];
        if (authorGrouped)
          formatAuthorHeading(author, heading);
        else
          formatInitialHeading(initial, heading);
        item.sectionHeading = heading.c_str();
      }
      if (seriesRows) {
        std::string& subtitle = winHeaders[static_cast<size_t>(headers++)];
        formatSeriesSubtitle(record, author, subtitle);
        const int percent = seriesPercentFor(entry, record);
        if (percent > 0) {
          char text[8];
          snprintf(text, sizeof(text), "%d%%", percent);
          if (!subtitle.empty()) subtitle += ", ";
          subtitle += text;
        }
        if (!subtitle.empty()) item.subtitle = subtitle.c_str();
      } else if (!authorGrouped && !author.empty()) {
        item.subtitle = author.c_str();
      }
    }

    item.label = title.c_str();
    // Group headings stay bare; every book row gets its file-type icon.
    if (!groupsCollapsed && !rowFile.empty()) item.icon = listIconFor(UITheme::getFileIcon(rowFile), 32);
    item.actionValue = static_cast<int16_t>(entry);
    winItems.push_back(item);
    rows++;
  }

  props.items = winItems.data();
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart);
  props.itemsWindowCount = static_cast<uint16_t>(winItems.size());
#if FREEINK_CAP_TOUCH
  configureSwipeDelete(screen, props);
#endif
  screen.list(props);
#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
  const int next = nav.drawnRows;
  const auto body = screen.body();
  LOG_DBG("LIB", "page tab=%d top=%d full=%d loaded=%d body=%d..%d next=%d title=%s", activeTabIndex, windowStart,
          nav.drawnRows, rows, body.y, body.bottom(),
          next < rows ? winItems[static_cast<size_t>(next)].actionValue : -1,
          next < rows ? winItems[static_cast<size_t>(next)].label : "<none>");
  LOG_DBG("LIB", "page first=%d title=%s", rows > 0 ? winItems[0].actionValue : -1,
          rows > 0 ? winItems[0].label : "<none>");
#endif
}

void LibraryListActivity::formatInitialHeading(uint32_t initial, std::string& out) {
  out.clear();
  if (initial == 0) {
    out.push_back('#');
    return;
  }
  if (initial >= 'a' && initial <= 'z') initial -= 'a' - 'A';
  utf8AppendCodepoint(initial, out);
}

void LibraryListActivity::formatAuthorHeading(const std::string& author, std::string& out) const {
  out = author.empty() ? std::string(tr(STR_LIBRARY_UNKNOWN_AUTHOR)) : author;
  if (author.empty()) return;
  const size_t lastSpace = out.find_last_of(' ');
  if (lastSpace != std::string::npos && lastSpace + 1 < out.size()) {
    out = out.substr(lastSpace + 1) + ", " + out.substr(0, lastSpace);
  }
}

void LibraryListActivity::buildHeader(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  fui::HeaderProps header;
  header.title = headerTitle();
  header.titleText = theme.titleText;
  header.titleText.align = theme.headerTitleAlign;
  header.sidePadding = theme.headerSidePadding;
  header.minTouchSize = theme.minTouchSize;
  header.styles = theme.popup;
  if (header.styles.normal.border.kind == fui::PaintKind::None && theme.headerUnderline > 0) {
    header.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    header.styles.normal.borderWidth = theme.headerUnderline;
  }
  header.trailingStyles = fui::plainStyles(fui::Paint::solid(fui::Color::Black));
  header.borderEdges = fui::EdgeBottom;
  // Same battery/clock band as every GUI.drawHeader screen; the header
  // heights are unified across themes, so the buttons derive from the band.
  GUI.applyHeaderStatus(renderer, header);
  if (mappedInput.hasTouch()) {
    header.leadingIcon = fui::bitmapFromIcon(icon_header_back_32);
    header.leadingAction = ACTION_BACK;
  }
  if (!degraded) {
    // Keep both touch actions together on the right; button boards reach
    // rebuild through the row options menu.
    header.trailingIcon = fui::bitmapFromIcon(icon_search_32);
    header.trailingAction = ACTION_SEARCH;
    if (mappedInput.hasTouch()) {
      header.trailingAdjacentIcon = fui::bitmapFromIcon(icon_refresh_cw_32);
      header.trailingAdjacentAction = ACTION_REBUILD;
    }
    // Vertical placement comes from applyHeaderStatus: buttons center on the
    // unified band.
  }
  const auto frameRect = screen.frame().screen();
  // Header and tabs share a screen-relative boundary, independent of bezel insets.
  const fui::Rect band = BaseTheme::headerBand(renderer,
                                               fui::Rect{frameRect.x, static_cast<int16_t>(metrics.topPadding),
                                                         frameRect.width, static_cast<int16_t>(metrics.headerHeight)},
                                               header);
  fui::header(screen.frame(), band, header);
}

void LibraryListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // The position readout owns the line above the hints; rows must not overlap
  // it.
  constexpr int READOUT_GAP = 4;  // the list's fold sits just above the readout's line box
  const int16_t readoutReserved = static_cast<int16_t>(renderer.getLineHeight(SMALL_FONT_ID) + READOUT_GAP);
  const Rect area = UITheme::getContentArea(renderer);
  const auto bottomInset = static_cast<int16_t>(renderer.getScreenHeight() - area.y - area.height);
  buildHeader(screen);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                  static_cast<int16_t>(bottomInset + metrics.buttonHintsHeight + readoutReserved), 0});

  if (!degraded) buildTabBar(screen);
  if (bookRowCount() == 0) {
    const char* message = tr(STR_LIBRARY_NO_RESULTS);
    if (filterFailed) {
      message = query.empty() ? tr(STR_LIBRARY_FILTER_UNAVAILABLE) : tr(STR_LIBRARY_SEARCH_UNAVAILABLE);
    } else if (query.empty()) {
      if (statusFilterActive() && index.bookCount() > 0) {
        message = tr(STR_LIBRARY_FILTER_EMPTY);
      } else if (isSeriesSort(sortOrder) && index.bookCount() > 0) {
        message = index.seriesDegraded() ? tr(STR_LIBRARY_SERIES_UNAVAILABLE) : tr(STR_LIBRARY_NO_SERIES);
      } else {
        message = tr(STR_LIBRARY_EMPTY);
      }
    }
    screen.centeredText(message);
    return;
  }
  buildRows(screen);
}

// "12/69 books" at the bottom right: which book is selected, out of how many.
//
// NOT a page count. How many rows fit varies with the view (author headings
// consume band height), so a page total grows and shrinks as you scroll. The
// book position is stable by construction, and it answers the question the
// reader actually has: how far in am I, and how much is left.
void LibraryListActivity::drawPositionReadout() const {
  const int count = listCount();
  if (count <= 0) return;

  char buf[32];
  const char* positionFormat = groupsCollapsed ? tr(STR_LIBRARY_GROUP_POSITION) : tr(STR_LIBRARY_POSITION);
  snprintf(buf, sizeof(buf), positionFormat, selectedEntry() + 1, count);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getTextWidth(SMALL_FONT_ID, buf);
  // Bottom-right of the viewable area, clear of the bezel and rounded corner.
  const Rect area = UITheme::getContentArea(renderer);
  const int right = std::max(renderer.getScreenWidth() - area.x - area.width, bottomCornerClearance());
  const int x = renderer.getScreenWidth() - right - width - SIDE_PADDING;
  const int y = area.y + area.height - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, x, y, buf, true);
}

const char* LibraryListActivity::headerTitle() const {
  if (!headerTitleText.empty()) return headerTitleText.c_str();
  if (seriesGroup >= 0 && !seriesTitle.empty()) return seriesTitle.c_str();
  return degraded ? tr(STR_LIBRARY_TITLE_UNSORTED) : tr(STR_LIBRARY);
}

// The header shows the active query in place of the screen title, so the
// reader can see what narrowed the list without reopening the keyboard, and
// names the status filter after it: "Library (Reading)".
void LibraryListActivity::updateHeaderTitle() {
  headerTitleText.clear();
  if (!query.empty()) headerTitleText = "“" + query + "”";
  if (!statusFilterActive()) return;
  const std::string base = headerTitleText.empty() ? std::string(headerTitle()) : headerTitleText;
  char text[160];
  snprintf(text, sizeof(text), tr(STR_LIBRARY_TITLE_FILTERED), base.c_str(), statusFilterLabel(statusFilter));
  headerTitleText = text;
}

void LibraryListActivity::drawHoldHelp() const {
  const bool seriesList = groupsCollapsed && isSeriesSort(sortOrder);
  if (mappedInput.hasTouch() || (groupsCollapsed && !seriesList)) return;
  const char* help = nullptr;
  if (tabsFocused()) {
    if (!degraded && !groupsCollapsed) help = tr(STR_LIBRARY_HOLD_SORT);
  } else if (listCount() > 0) {
    help = tr(STR_LIBRARY_HOLD_OPTIONS);  // book rows and series: hold opens the menu
  }
  if (!help) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const Rect area = UITheme::getContentArea(renderer);
  const int left = std::max(area.x, bottomCornerClearance());
  const int y = area.y + area.height - metrics.buttonHintsHeight - lineHeight;
  GUI.drawHelpText(renderer,
                   Rect{left + SIDE_PADDING, y, renderer.getScreenWidth() / 2 - left - SIDE_PADDING, lineHeight}, help);
}

// OptionPopup is a self-contained modal: it owns the whole frame (hints
// included) whenever it is up, mirroring the FileBrowser pattern.
void LibraryListActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiTabListActivity::render(std::move(lock));
}

void LibraryListActivity::drawFooter() {
  drawPositionReadout();
  drawHoldHelp();

  // The Series list is collapsed groups, but Back from its tabs still goes home.
  const bool backGoesHome = tabsFocused() && (!groupsCollapsed || (isSeriesSort(sortOrder) && seriesGroup < 0)) &&
                            query.empty() && !statusFilterActive();
  const char* backLabel = backGoesHome ? tr(STR_HOME) : tr(STR_BACK);
  const char* confirmLabel = groupsCollapsed ? tr(STR_SELECT) : tr(STR_OPEN);
  const bool canSearch = tabsFocused() && !degraded;
  const auto labels = mappedInput.mapLabels(backLabel, tabsFocused() ? tr(STR_TOGGLE) : confirmLabel,
                                            canSearch ? tr(STR_SEARCH) : tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
