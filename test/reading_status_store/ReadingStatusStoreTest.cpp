#include <HalStorage.h>
#include <ReadingStatusStore.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {
std::vector<uint8_t>& fileBytes() { return fake::files.at(ReadingStatusStore::FILE_PATH)->bytes; }

ReadingStatusStore loaded() {
  ReadingStatusStore store;
  EXPECT_TRUE(store.load());
  return store;
}
}  // namespace

TEST(ReadingStatusStore, MissingFileLoadsEmptyAndUnseeded) {
  fake::reset();
  ReadingStatusStore store = loaded();
  EXPECT_TRUE(store.isLoaded());
  EXPECT_EQ(store.size(), 0u);
  EXPECT_FALSE(store.seeded());
  EXPECT_EQ(store.get(42), ReadingState::Unread);
}

TEST(ReadingStatusStore, SetKeepsKeysSortedAndEraseRemoves) {
  fake::reset();
  ReadingStatusStore store = loaded();
  const uint64_t keys[] = {900, 5, 300, 1, 700, UINT64_MAX, 0};
  for (const uint64_t key : keys) ASSERT_TRUE(store.set(key, ReadingState::Reading));
  EXPECT_EQ(store.size(), 7u);
  for (const uint64_t key : keys) EXPECT_EQ(store.get(key), ReadingState::Reading);
  EXPECT_EQ(store.get(2), ReadingState::Unread);
  EXPECT_FALSE(store.has(2));

  ASSERT_TRUE(store.set(300, ReadingState::Finished));
  EXPECT_EQ(store.size(), 7u);
  EXPECT_EQ(store.get(300), ReadingState::Finished);

  // An explicit Unread is a record; erase drops it.
  ASSERT_TRUE(store.set(5, ReadingState::Unread));
  EXPECT_TRUE(store.has(5));
  EXPECT_EQ(store.get(5), ReadingState::Unread);
  store.erase(5);
  store.erase(4);  // absent: no-op
  EXPECT_FALSE(store.has(5));
  EXPECT_EQ(store.size(), 6u);
  EXPECT_EQ(store.get(1), ReadingState::Reading);
  EXPECT_EQ(store.get(700), ReadingState::Reading);
}

TEST(ReadingStatusStore, SaveAndLoadRoundTrip) {
  fake::reset();
  ReadingStatusStore store = loaded();
  for (uint64_t key = 0; key < 100; key++) {
    ASSERT_TRUE(store.set(key * 7919, key % 3 == 0 ? ReadingState::Finished : ReadingState::Reading));
  }
  store.markSeeded();
  ASSERT_TRUE(store.save());
  EXPECT_EQ(fileBytes().size(), 8u + 100u * 9u);
  EXPECT_FALSE(fake::files.count(std::string(ReadingStatusStore::FILE_PATH) + ".tmp"));

  ReadingStatusStore again = loaded();
  EXPECT_EQ(again.size(), 100u);
  EXPECT_TRUE(again.seeded());
  for (uint64_t key = 0; key < 100; key++) {
    EXPECT_EQ(again.get(key * 7919), key % 3 == 0 ? ReadingState::Finished : ReadingState::Reading);
  }
}

TEST(ReadingStatusStore, DamagedFileLoadsEmptyAndUnseeded) {
  fake::reset();
  ReadingStatusStore store = loaded();
  ASSERT_TRUE(store.set(10, ReadingState::Reading));
  ASSERT_TRUE(store.set(20, ReadingState::Finished));
  store.markSeeded();
  ASSERT_TRUE(store.save());

  const std::vector<uint8_t> good = fileBytes();
  fileBytes().pop_back();  // size no longer matches the count
  EXPECT_EQ(loaded().size(), 0u);
  EXPECT_FALSE(loaded().seeded());

  fileBytes() = good;
  fileBytes()[8 + 9 + 8] = 7;  // invalid state byte
  EXPECT_EQ(loaded().size(), 0u);

  fileBytes() = good;
  std::swap_ranges(fileBytes().begin() + 8, fileBytes().begin() + 17, fileBytes().begin() + 17);  // unsorted
  EXPECT_EQ(loaded().size(), 0u);

  fileBytes() = good;
  EXPECT_EQ(loaded().size(), 2u);
}

TEST(ReadingStatusStore, InsertFailsAtCapButUpdatesStillWork) {
  fake::reset();
  ReadingStatusStore store = loaded();
  for (uint64_t key = 0; key < ReadingStatusStore::MAX_ENTRIES; key++) {
    ASSERT_TRUE(store.set(key, ReadingState::Reading));
  }
  EXPECT_FALSE(store.set(ReadingStatusStore::MAX_ENTRIES, ReadingState::Reading));
  EXPECT_TRUE(store.set(3, ReadingState::Finished));
  store.erase(4);
  EXPECT_TRUE(store.set(ReadingStatusStore::MAX_ENTRIES, ReadingState::Reading));
}

TEST(ReadingStatusStore, AllocationFailureLeavesStoreUnchanged) {
  fake::reset();
  ReadingStatusStore store = loaded();
  ASSERT_TRUE(store.set(1, ReadingState::Reading));
  // Growth beyond the first block of slots needs a new allocation.
  for (uint64_t key = 2; key <= 16; key++) ASSERT_TRUE(store.set(key, ReadingState::Reading));
  fake::failAlloc = 0;
  EXPECT_FALSE(store.set(100, ReadingState::Reading));
  EXPECT_EQ(store.size(), 16u);
  EXPECT_EQ(store.get(16), ReadingState::Reading);
  EXPECT_TRUE(store.set(100, ReadingState::Reading));
}

TEST(ReadingStatusStore, UnloadedStoreRefusesChanges) {
  ReadingStatusStore store;
  EXPECT_FALSE(store.set(1, ReadingState::Reading));
  EXPECT_FALSE(store.save());
}

TEST(ReadingStatusStore, UpdateWritesOnlyWhenStateChanges) {
  fake::reset();
  EXPECT_TRUE(ReadingStatusStore::removeBook("/books/a.epub"));
  EXPECT_FALSE(fake::files.count(ReadingStatusStore::FILE_PATH));

  EXPECT_TRUE(ReadingStatusStore::update("/books/a.epub", ReadingState::Finished));
  ASSERT_TRUE(fake::files.count(ReadingStatusStore::FILE_PATH));
  const auto writes = fake::writesByPath;
  EXPECT_TRUE(ReadingStatusStore::update("/books/a.epub", ReadingState::Finished));
  EXPECT_EQ(fake::writesByPath, writes);

  EXPECT_EQ(loaded().get(ReadingStatusStore::keyFor("/books/a.epub")), ReadingState::Finished);
  EXPECT_TRUE(ReadingStatusStore::update("/books/a.epub", ReadingState::Unread));
  ReadingStatusStore store = loaded();
  EXPECT_EQ(store.size(), 1u);
  EXPECT_EQ(store.get(ReadingStatusStore::keyFor("/books/a.epub")), ReadingState::Unread);
  EXPECT_TRUE(ReadingStatusStore::removeBook("/books/a.epub"));
  EXPECT_EQ(loaded().size(), 0u);
  EXPECT_FALSE(fake::files.count(std::string(ReadingStatusStore::FILE_PATH) + ".tmp"));
}

TEST(ReadingStatusStore, FileUpdatesMatchTheLoadedTable) {
  fake::reset();
  ReadingStatusStore store = loaded();
  for (uint64_t key = 1; key <= 500; key++) ASSERT_TRUE(store.set(key * 1000003, ReadingState::Reading));
  store.markSeeded();
  ASSERT_TRUE(store.save());

  // Unchanged state: a binary search, no write.
  const std::string present = "/books/present.epub";
  ASSERT_TRUE(ReadingStatusStore::update(present, ReadingState::Finished));
  fake::resetIoCounters();
  const auto writes = fake::writesByPath;
  ASSERT_TRUE(ReadingStatusStore::update(present, ReadingState::Finished));
  EXPECT_EQ(fake::writesByPath, writes);
  EXPECT_LE(fake::reads, 12u);

  ASSERT_TRUE(ReadingStatusStore::update("/books/b.epub", ReadingState::Unread));
  ReadingStatusStore::recordReaderExit("/books/c.epub", false, false);
  ASSERT_TRUE(ReadingStatusStore::removeBook(present));

  ReadingStatusStore again = loaded();
  EXPECT_TRUE(again.seeded());
  EXPECT_EQ(again.size(), 502u);
  EXPECT_FALSE(again.has(ReadingStatusStore::keyFor(present)));
  EXPECT_EQ(again.get(ReadingStatusStore::keyFor("/books/b.epub")), ReadingState::Unread);
  EXPECT_TRUE(again.has(ReadingStatusStore::keyFor("/books/b.epub")));
  EXPECT_EQ(again.get(ReadingStatusStore::keyFor("/books/c.epub")), ReadingState::Reading);
  for (uint64_t key = 1; key <= 500; key++) EXPECT_EQ(again.get(key * 1000003), ReadingState::Reading);
}

TEST(ReadingStatusStore, UnreadableFileIsNeverOverwritten) {
  fake::reset();
  ASSERT_TRUE(ReadingStatusStore::update("/books/a.epub", ReadingState::Finished));
  ASSERT_TRUE(ReadingStatusStore::update("/books/b.epub", ReadingState::Reading));
  const std::vector<uint8_t> good = fileBytes();

  fake::failRead = 0;  // header read fails
  ReadingStatusStore store;
  EXPECT_FALSE(store.load());
  EXPECT_FALSE(store.isLoaded());
  EXPECT_FALSE(store.save());

  fake::failRead = 1;  // record read fails
  EXPECT_FALSE(store.load());

  fake::failRead = 0;
  EXPECT_FALSE(ReadingStatusStore::update("/books/c.epub", ReadingState::Reading));
  fake::failRead = 1;  // binary search read fails
  EXPECT_FALSE(ReadingStatusStore::removeBook("/books/a.epub"));
  fake::failRead = 2;  // a later read, during the search or the copy, fails
  EXPECT_FALSE(ReadingStatusStore::update("/books/c.epub", ReadingState::Reading));
  EXPECT_EQ(fileBytes(), good);
  EXPECT_FALSE(fake::files.count(std::string(ReadingStatusStore::FILE_PATH) + ".tmp"));
  EXPECT_EQ(loaded().size(), 2u);
}

TEST(ReadingStatusStore, DamagedFileIsReplacedAndKept) {
  const std::string bad = "/.crosspoint/reading_status.bad";
  fake::reset();
  fake::add(ReadingStatusStore::FILE_PATH, "not a store");
  ASSERT_TRUE(ReadingStatusStore::update("/books/a.epub", ReadingState::Finished));
  EXPECT_TRUE(fake::files.count(bad));
  ReadingStatusStore store = loaded();
  EXPECT_EQ(store.size(), 1u);
  EXPECT_FALSE(store.seeded());

  // Right size, unsorted records: caught while copying.
  fake::reset();
  ASSERT_TRUE(ReadingStatusStore::update("/books/a.epub", ReadingState::Finished));
  ASSERT_TRUE(ReadingStatusStore::update("/books/b.epub", ReadingState::Reading));
  std::swap_ranges(fileBytes().begin() + 8, fileBytes().begin() + 17, fileBytes().begin() + 17);
  ASSERT_TRUE(ReadingStatusStore::update("/books/c.epub", ReadingState::Reading));
  EXPECT_TRUE(fake::files.count(bad));
  EXPECT_EQ(loaded().size(), 1u);

  // The Library's save of a damaged file also keeps the old copy.
  fake::reset();
  fake::add(ReadingStatusStore::FILE_PATH, "not a store");
  ReadingStatusStore table = loaded();
  ASSERT_TRUE(table.set(1, ReadingState::Reading));
  ASSERT_TRUE(table.save());
  EXPECT_TRUE(fake::files.count(bad));
  EXPECT_EQ(loaded().size(), 1u);
}

TEST(ReadingStatusStore, FileInsertFailsAtCap) {
  fake::reset();
  ReadingStatusStore store = loaded();
  for (uint64_t key = 0; key < ReadingStatusStore::MAX_ENTRIES; key++) {
    ASSERT_TRUE(store.set(key, ReadingState::Reading));
  }
  ASSERT_TRUE(store.save());
  const std::vector<uint8_t> full = fileBytes();
  EXPECT_FALSE(ReadingStatusStore::update("/books/new.epub", ReadingState::Reading));
  EXPECT_EQ(fileBytes(), full);
}

TEST(ReadingStatusStore, ExplicitUnreadSurvivesHistoryImport) {
  fake::reset();
  const std::string path = "/books/a.epub";
  const uint64_t key = ReadingStatusStore::keyFor(path);
  ASSERT_TRUE(ReadingStatusStore::update(path, ReadingState::Unread));

  ReadingStatusStore store = loaded();
  ASSERT_FALSE(store.seeded());
  // The import found saved progress at the end of the book.
  ASSERT_TRUE(store.importProgress(key, true));
  EXPECT_EQ(store.get(key), ReadingState::Unread);
  ASSERT_TRUE(store.importProgress(ReadingStatusStore::keyFor("/books/b.epub"), true));
  ASSERT_TRUE(store.importProgress(ReadingStatusStore::keyFor("/books/c.epub"), false));
  EXPECT_EQ(store.get(ReadingStatusStore::keyFor("/books/b.epub")), ReadingState::Finished);
  EXPECT_EQ(store.get(ReadingStatusStore::keyFor("/books/c.epub")), ReadingState::Reading);
}

TEST(ReadingStatusStore, ReaderExitNeverDowngradesFinished) {
  fake::reset();
  const std::string path = "/books/a.epub";
  const uint64_t key = ReadingStatusStore::keyFor(path);
  ReadingStatusStore::recordReaderExit(path, false, false);
  EXPECT_EQ(loaded().get(key), ReadingState::Reading);
  ReadingStatusStore::recordReaderExit(path, true, false);
  EXPECT_EQ(loaded().get(key), ReadingState::Finished);
  ReadingStatusStore::recordReaderExit(path, false, false);
  EXPECT_EQ(loaded().get(key), ReadingState::Finished);
}

TEST(ReadingStatusStore, ReaderExitKeepsBookFinishedBeforeTheStore) {
  fake::reset();
  // Opened at its end with no record, then left mid-book.
  ReadingStatusStore::recordReaderExit("/books/old.epub", false, true);
  EXPECT_EQ(loaded().get(ReadingStatusStore::keyFor("/books/old.epub")), ReadingState::Finished);

  // A recorded state wins: marked unread, reopened at the end, left mid-book.
  ASSERT_TRUE(ReadingStatusStore::update("/books/b.epub", ReadingState::Unread));
  ReadingStatusStore::recordReaderExit("/books/b.epub", false, true);
  EXPECT_EQ(loaded().get(ReadingStatusStore::keyFor("/books/b.epub")), ReadingState::Reading);
}

TEST(ReadingStatusStore, RenameMovesTheState) {
  fake::reset();
  ReadingStatusStore::update("/books/a.epub", ReadingState::Finished);
  ReadingStatusStore::rename("/books/a.epub", "/Read/a.epub");
  ReadingStatusStore store = loaded();
  EXPECT_EQ(store.get(ReadingStatusStore::keyFor("/books/a.epub")), ReadingState::Unread);
  EXPECT_EQ(store.get(ReadingStatusStore::keyFor("/Read/a.epub")), ReadingState::Finished);
  EXPECT_EQ(store.size(), 1u);

  ReadingStatusStore::rename("/books/unknown.epub", "/Read/unknown.epub");
  EXPECT_EQ(loaded().size(), 1u);
}

TEST(ReadingStatusStore, KeyMatchesLibraryIndexPathHash) {
  // FNV-1a 64 of "/a.epub", as library::clixPathHash computes it for index records.
  uint64_t hash = 14695981039346656037ULL;
  for (const char c : std::string("/a.epub")) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 1099511628211ULL;
  }
  EXPECT_EQ(ReadingStatusStore::keyFor("/a.epub"), hash);
}
