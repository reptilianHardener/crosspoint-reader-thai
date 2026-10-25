#pragma once

// Read side of the CLX1 index.
//
// Holds one open file handle and a few hundred bytes; nothing that scales with
// the library stays resident. A page of rows is a handful of seeks, which is
// what the fixed 128-byte record stride buys — record k is always at
// recordStart + 128k, so no offset table has to be loaded to find it.

#include <HalStorage.h>

#include <cstdint>
#include <string>

#include "LibraryFormat.h"

namespace library {

enum class SortOrder : uint8_t {
  // "Recent" orders by file modification time (arrival on the card), oldest
  // first in Asc; firstSeen breaks ties for filesystems without timestamps.
  RecentAsc,
  RecentDesc,
  TitleAsc,
  TitleDesc,
  AuthorAsc,
  AuthorDesc,
  // Only the books that belong to a series: grouped by series name, each series
  // in series-index order. One direction only, so volumes always run upward.
  Series,
};

// One book to locate in the index: the complete-path hash (clixPathHash) is
// the identity; fileSize, when nonzero, is a cheap in-record prefilter that
// avoids reading the hash blob for most records. Zero means size unknown and
// every record's hash is checked.
struct BookIdentity {
  uint64_t pathHash;
  uint32_t fileSize;
};

class LibraryIndexFile {
 public:
  LibraryIndexFile() = default;
  ~LibraryIndexFile();
  LibraryIndexFile(const LibraryIndexFile&) = delete;
  LibraryIndexFile& operator=(const LibraryIndexFile&) = delete;

  // Open and validate. On failure `validity()` says why, so a rebuild loop
  // caused by a format bug is visible in the log rather than looking like a slow
  // first boot.
  bool open(const char* path);
  // Accept an otherwise valid stale fold so a rebuild can preserve arrival
  // history without exposing stale sort/search keys to the browser.
  bool openForReconciliation(const char* path);
  void close();
  bool isOpen() const { return opened; }
  bool ioFailed() const { return readFailed; }

  ClixValidity validity() const { return lastValidity; }
  const ClixHeader& header() const { return head; }
  uint16_t bookCount() const { return opened ? head.bookCount : 0; }
  uint16_t seriesCount() const { return opened ? head.seriesCount : 0; }
  // Rows `order` has: every book, or only the books in a series.
  uint16_t rowCount(SortOrder order) const;
  bool ranksDegraded() const { return opened && (head.flags & CLIX_FLAG_RANKS_DEGRADED) != 0; }
  bool dedupDegraded() const { return opened && (head.flags & CLIX_FLAG_DEDUP_DEGRADED) != 0; }
  bool seriesDegraded() const { return opened && (head.flags & CLIX_FLAG_SERIES_DEGRADED) != 0; }

  // Record ordinal of the row at display position `row` in `order`. Returns
  // 0xFFFF when out of range, which callers treat as "no such row" rather than
  // indexing anyway.
  uint16_t ordinalForRow(SortOrder order, uint16_t row);

  // Display rows (RecentAsc space) of up to MAX_IDENTITY_LOOKUPS books, 0xFFFF
  // for books not in the index. One chunked pass over the record section plus
  // one over the arrival permutation, so cost is bounded by the library, not by
  // `count` — callers batch their lookups instead of calling per book.
  static constexpr size_t MAX_IDENTITY_LOOKUPS = 16;
  bool recentRowsFor(const BookIdentity* books, size_t count, uint16_t* outRows);

  // Record ordinal of the volume after `book` in its series: the first later
  // book of the same series with a higher position, so a second edition of the
  // same volume is skipped. 0xFFFF when the book is not indexed, has no series
  // or position, or is the last volume.
  uint16_t nextInSeries(const BookIdentity& book);

  bool readRecord(uint16_t ordinal, ClixRecord& out);
  // Persisted complete-path fingerprint used by rebuild reconciliation.
  bool readPathHash(const ClixRecord& record, uint64_t& out);

  // Display basename, exactly as it sits on the card. This is the only string
  // the UI draws, and it is never shortened on disk.
  bool readName(const ClixRecord& record, std::string& out);
  // The author the build settled on, stored right after the name. Reading it
  // rather than re-deriving it from the name is what makes the metadata pass and
  // the spelling harmonisation visible: neither survives a filename that no
  // longer carries "Title - Author".
  bool readAuthor(const ClixRecord& record, std::string& out);
  bool readTitle(const ClixRecord& record, std::string& out);
  // Cleaned author spelling before the library-wide spelling vote. Empty is a
  // valid value, so success is independent of `out.empty()`.
  bool readSourceAuthor(const ClixRecord& record, std::string& out);

  // Series the book belongs to, from its metadata. False when it has none.
  bool readSeries(const ClixRecord& record, std::string& out);
  // Position in the series (1, 2, 2.5...). False when the book gives none.
  bool readSeriesIndex(const ClixRecord& record, float& out);

  // Absolute path of the book, rebuilt from its folder record.
  bool readPath(const ClixRecord& record, std::string& out);

 private:
  bool openImpl(const char* path, bool acceptStaleFold);
  bool readAt(uint32_t offset, void* dst, size_t len);
  bool readBlobField(const ClixRecord& record, uint8_t field, std::string& out);

  HalFile file;
  ClixHeader head{};
  bool opened = false;
  bool readFailed = false;
  ClixValidity lastValidity = ClixValidity::BadMagic;
};

}  // namespace library
