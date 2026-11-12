#pragma once

#include <cstdint>
#include <memory>
#include <string>

enum class ReadingState : uint8_t { Unread = 0, Reading = 1, Finished = 2 };

// Reading state per book for the Library filter, in /.crosspoint/reading_status.bin
// as {pathHash, state} records sorted by hash. A book without a record is Unread;
// an Unread record means the user marked it so, and the history import keeps it.
// The key is library::clixPathHash of the absolute path, the identity the library
// index already stores, so the filter matches index rows without opening book caches.
//
// All changes go to the file through the static helpers. The Library's loaded table
// is a read copy that applies the same change after the file write succeeds; only
// the history import saves the whole table, right after loading it.
class ReadingStatusStore {
 public:
  static constexpr const char* FILE_PATH = "/.crosspoint/reading_status.bin";
  // 9 bytes per record on disk and in memory, so 18 KB at the cap. That is far
  // more books than one reader opens; an insert past it fails and is logged.
  static constexpr uint16_t MAX_ENTRIES = 2000;

  static uint64_t keyFor(const std::string& path);

  // A missing file loads empty. A file with a bad header, size or records loads
  // empty and unseeded, and the next write replaces it (the old file is kept as
  // reading_status.bad). False when an existing file cannot be read or memory for
  // the records is not available; the store is then not loaded.
  bool load();
  bool save();
  void reset();
  bool isLoaded() const { return loaded; }
  uint16_t size() const { return count; }

  ReadingState get(uint64_t key) const;
  bool has(uint64_t key) const;
  // Stores a record, an Unread one too. False when not loaded, full or out of memory.
  bool set(uint64_t key, ReadingState state);
  void erase(uint64_t key);
  // History import for one book with saved progress: only a book with no record
  // changes, so a state the user or the reader already recorded wins.
  bool importProgress(uint64_t key, bool atEndOfBook);

  // True once books read before this store existed were imported from their
  // saved progress (see LibraryListActivity::seedReadingStatus).
  bool seeded() const { return seededFlag; }
  void markSeeded() { seededFlag = true; }

  // File changes with a fixed-size buffer: a binary search finds the record, and
  // the file is rewritten through a temp file only when the state changes. Each
  // refuses to write when the existing file cannot be read.
  static bool update(const std::string& path, ReadingState state);
  // The book file was deleted: drop its record.
  static bool removeBook(const std::string& path);
  // The book file moved; its state follows it.
  static void rename(const std::string& oldPath, const std::string& newPath);
  // Reader exit: Finished at the end of the book, otherwise Reading. A Finished
  // book stays Finished when it is reopened, so looking up a passage does not undo
  // it. openedAtEnd covers a book finished before this store existed.
  static void recordReaderExit(const std::string& path, bool atEndOfBook, bool openedAtEnd);

 private:
  int find(uint64_t key) const;
  bool reserve(uint16_t capacity);

  std::unique_ptr<uint64_t[]> keys;
  std::unique_ptr<uint8_t[]> states;
  uint16_t count = 0;
  uint16_t capacity = 0;
  bool loaded = false;
  bool seededFlag = false;
  // The file on the card is damaged; save() keeps a copy before replacing it.
  bool damagedFile = false;
};
