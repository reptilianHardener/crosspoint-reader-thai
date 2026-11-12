#include "ReadingStatusStore.h"

#include <HalStorage.h>
#include <LibraryFormat.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

namespace {
// Header: "CRST", version, flags, record count (u16 LE). Then count x {u64 LE key, u8 state}.
constexpr char MAGIC[4] = {'C', 'R', 'S', 'T'};
constexpr uint8_t VERSION = 1;
constexpr uint8_t FLAG_SEEDED = 1u << 0;
constexpr size_t HEADER_BYTES = 8;
constexpr size_t RECORD_BYTES = 9;
constexpr size_t CHUNK_RECORDS = 16;
constexpr const char* TMP_PATH = "/.crosspoint/reading_status.bin.tmp";
constexpr const char* BAD_PATH = "/.crosspoint/reading_status.bad";

bool validState(const uint8_t state) { return state <= static_cast<uint8_t>(ReadingState::Finished); }

enum class FileStatus : uint8_t { Missing, Damaged, Valid, Unreadable };

// Opens the store file and checks its header against its size. Unreadable is an
// I/O failure on a file that exists: its contents may be fine, so nothing may
// overwrite it.
FileStatus openStoreFile(HalFile& file, uint16_t& count, bool& seeded) {
  count = 0;
  seeded = false;
  if (!Storage.exists(ReadingStatusStore::FILE_PATH)) return FileStatus::Missing;
  if (!Storage.openFileForRead("RST", ReadingStatusStore::FILE_PATH, file)) return FileStatus::Unreadable;
  const size_t size = file.fileSize();
  if (size < HEADER_BYTES) return FileStatus::Damaged;
  uint8_t header[HEADER_BYTES];
  if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) return FileStatus::Unreadable;
  if (memcmp(header, MAGIC, sizeof(MAGIC)) != 0 || header[4] != VERSION) return FileStatus::Damaged;
  const uint16_t stored = static_cast<uint16_t>(header[6] | (header[7] << 8));
  if (stored > ReadingStatusStore::MAX_ENTRIES || size != HEADER_BYTES + stored * RECORD_BYTES) {
    return FileStatus::Damaged;
  }
  count = stored;
  seeded = (header[5] & FLAG_SEEDED) != 0;
  return FileStatus::Valid;
}

bool writeHeader(HalFile& file, const uint16_t count, const bool seeded) {
  const uint8_t header[HEADER_BYTES] = {static_cast<uint8_t>(MAGIC[0]),
                                        static_cast<uint8_t>(MAGIC[1]),
                                        static_cast<uint8_t>(MAGIC[2]),
                                        static_cast<uint8_t>(MAGIC[3]),
                                        VERSION,
                                        static_cast<uint8_t>(seeded ? FLAG_SEEDED : 0),
                                        static_cast<uint8_t>(count & 0xFF),
                                        static_cast<uint8_t>(count >> 8)};
  return file.write(header, sizeof(header)) == sizeof(header);
}

void keepDamagedCopy() {
  LOG_ERR("RST", "replacing damaged reading status file, old copy kept as %s", BAD_PATH);
  Storage.replaceFile(ReadingStatusStore::FILE_PATH, BAD_PATH);
}

bool commitTmp() {
  if (!Storage.replaceFile(TMP_PATH, ReadingStatusStore::FILE_PATH)) {
    LOG_ERR("RST", "cannot replace %s", ReadingStatusStore::FILE_PATH);
    return false;
  }
  return true;
}

// Buffers records for the temp file and checks that keys keep ascending, so a
// damaged source is caught while it is copied.
class RecordSink {
 public:
  explicit RecordSink(HalFile& out) : out(out) {}

  bool push(const uint64_t key, const uint8_t state) {
    if (!validState(state) || (any && key <= last)) {
      corrupt = true;
      return false;
    }
    memcpy(buffer + pending * RECORD_BYTES, &key, sizeof(key));
    buffer[pending * RECORD_BYTES + sizeof(key)] = state;
    last = key;
    any = true;
    return ++pending < CHUNK_RECORDS || flush();
  }

  bool flush() {
    const size_t bytes = pending * RECORD_BYTES;
    pending = 0;
    return bytes == 0 || out.write(buffer, bytes) == bytes;
  }

  bool corrupt = false;

 private:
  HalFile& out;
  uint8_t buffer[CHUNK_RECORDS * RECORD_BYTES];
  size_t pending = 0;
  uint64_t last = 0;
  bool any = false;
};

// Copies records [from, to) of the source file into the sink.
bool copyRecords(HalFile& src, const uint16_t from, const uint16_t to, RecordSink& sink) {
  if (from >= to) return true;
  if (!src.seekSet(HEADER_BYTES + from * RECORD_BYTES)) return false;
  uint8_t chunk[CHUNK_RECORDS * RECORD_BYTES];
  for (uint16_t done = from; done < to;) {
    const size_t n = std::min<size_t>(CHUNK_RECORDS, to - done);
    if (src.read(chunk, n * RECORD_BYTES) != static_cast<int>(n * RECORD_BYTES)) return false;
    for (size_t i = 0; i < n; i++) {
      uint64_t key = 0;
      memcpy(&key, chunk + i * RECORD_BYTES, sizeof(key));
      if (!sink.push(key, chunk[i * RECORD_BYTES + sizeof(key)])) return false;
    }
    done = static_cast<uint16_t>(done + n);
  }
  return true;
}

// Binary search over the fixed-size records: the first index whose key is not below `key`.
bool lowerBoundInFile(HalFile& file, const uint16_t count, const uint64_t key, uint16_t& at, bool& present,
                      uint8_t& state) {
  uint16_t lo = 0;
  uint16_t hi = count;
  while (lo < hi) {
    const uint16_t mid = static_cast<uint16_t>(lo + (hi - lo) / 2);
    uint8_t record[RECORD_BYTES];
    if (!file.seekSet(HEADER_BYTES + mid * RECORD_BYTES) ||
        file.read(record, sizeof(record)) != static_cast<int>(sizeof(record))) {
      return false;
    }
    uint64_t midKey = 0;
    memcpy(&midKey, record, sizeof(midKey));
    if (midKey < key) {
      lo = static_cast<uint16_t>(mid + 1);
    } else {
      hi = mid;
      if (midKey == key) {
        present = true;
        state = record[sizeof(midKey)];
      }
    }
  }
  at = lo;
  return true;
}

struct Change {
  enum class Op : uint8_t { None, Put, Drop } op = Op::None;
  ReadingState state = ReadingState::Unread;
};

enum class WriteResult : uint8_t { Ok, Failed, Corrupt };

// Writes the temp file: the source records with the change applied at `at`.
// `src` is null when starting from an empty store.
WriteResult writeChanged(HalFile* src, const uint16_t count, const bool seeded, const uint16_t at, const bool present,
                         const uint64_t key, const Change& change) {
  int newCount = count;
  if (change.op == Change::Op::Put && !present) newCount++;
  if (change.op == Change::Op::Drop && present) newCount--;
  if (newCount > ReadingStatusStore::MAX_ENTRIES) {
    LOG_ERR("RST", "reading status store full (%u books)", static_cast<unsigned>(count));
    return WriteResult::Failed;
  }
  Storage.mkdir("/.crosspoint");
  HalFile out;
  if (!Storage.openFileForWrite("RST", TMP_PATH, out)) {
    LOG_ERR("RST", "cannot write %s", TMP_PATH);
    return WriteResult::Failed;
  }
  RecordSink sink(out);
  const uint16_t resume = static_cast<uint16_t>(present ? at + 1 : at);
  const bool ok = writeHeader(out, static_cast<uint16_t>(newCount), seeded) &&
                  (!src || copyRecords(*src, 0, at, sink)) &&
                  (change.op != Change::Op::Put || sink.push(key, static_cast<uint8_t>(change.state))) &&
                  (!src || copyRecords(*src, resume, count, sink)) && sink.flush();
  out.flush();
  out.close();
  if (ok) return WriteResult::Ok;
  Storage.remove(TMP_PATH);
  if (sink.corrupt) return WriteResult::Corrupt;
  LOG_ERR("RST", "cannot copy reading status file");
  return WriteResult::Failed;
}

// One file change: find the key's record, ask `decide` what to do with it, and
// rewrite the file only when something changes. A damaged file is set aside and
// replaced by a fresh, unseeded one; an unreadable one is left alone.
template <typename Decide>
bool modifyFile(const uint64_t key, Decide decide) {
  bool damaged = false;
  {
    HalFile file;
    uint16_t count = 0;
    bool seeded = false;
    const FileStatus status = openStoreFile(file, count, seeded);
    if (status == FileStatus::Unreadable) {
      LOG_ERR("RST", "cannot read %s, change not saved", ReadingStatusStore::FILE_PATH);
      return false;
    }
    if (status == FileStatus::Damaged) {
      damaged = true;
    } else {
      uint16_t at = 0;
      bool present = false;
      uint8_t stored = 0;
      if (count > 0 && !lowerBoundInFile(file, count, key, at, present, stored)) {
        LOG_ERR("RST", "cannot read %s, change not saved", ReadingStatusStore::FILE_PATH);
        return false;
      }
      const Change change = decide(present, static_cast<ReadingState>(stored));
      if (change.op == Change::Op::None || (change.op == Change::Op::Drop && !present) ||
          (change.op == Change::Op::Put && present && stored == static_cast<uint8_t>(change.state))) {
        return true;
      }
      const WriteResult result =
          writeChanged(status == FileStatus::Valid ? &file : nullptr, count, seeded, at, present, key, change);
      if (result == WriteResult::Failed) return false;
      damaged = result == WriteResult::Corrupt;
    }
    // The source closes here, before the file is replaced.
  }
  if (!damaged) return commitTmp();

  const Change change = decide(false, ReadingState::Unread);
  if (change.op != Change::Op::Put) return true;
  keepDamagedCopy();
  return writeChanged(nullptr, 0, false, 0, false, key, change) == WriteResult::Ok && commitTmp();
}
}  // namespace

uint64_t ReadingStatusStore::keyFor(const std::string& path) { return library::clixPathHash(path.data(), path.size()); }

void ReadingStatusStore::reset() {
  keys.reset();
  states.reset();
  count = 0;
  capacity = 0;
  loaded = false;
  seededFlag = false;
  damagedFile = false;
}

bool ReadingStatusStore::reserve(const uint16_t wanted) {
  if (wanted <= capacity) return true;
  auto newKeys = makeUniqueNoThrow<uint64_t[]>(wanted);
  auto newStates = makeUniqueNoThrow<uint8_t[]>(wanted);
  if (!newKeys || !newStates) {
    LOG_ERR("RST", "OOM: %u-byte reading status table", static_cast<unsigned>(wanted * RECORD_BYTES));
    return false;
  }
  if (count > 0) {
    std::copy(keys.get(), keys.get() + count, newKeys.get());
    std::copy(states.get(), states.get() + count, newStates.get());
  }
  keys = std::move(newKeys);
  states = std::move(newStates);
  capacity = wanted;
  return true;
}

bool ReadingStatusStore::load() {
  reset();
  HalFile file;
  uint16_t stored = 0;
  bool seededOnCard = false;
  switch (openStoreFile(file, stored, seededOnCard)) {
    case FileStatus::Missing:
      loaded = true;
      return true;
    case FileStatus::Unreadable:
      LOG_ERR("RST", "cannot read %s", FILE_PATH);
      return false;
    case FileStatus::Damaged:
      LOG_ERR("RST", "ignoring damaged reading status file");
      loaded = true;
      damagedFile = true;
      return true;
    case FileStatus::Valid:
      break;
  }
  if (!reserve(stored)) return false;

  uint8_t chunk[CHUNK_RECORDS * RECORD_BYTES];
  for (uint16_t done = 0; done < stored;) {
    const size_t n = std::min<size_t>(CHUNK_RECORDS, stored - done);
    if (file.read(chunk, n * RECORD_BYTES) != static_cast<int>(n * RECORD_BYTES)) {
      LOG_ERR("RST", "short read in %s", FILE_PATH);
      reset();
      return false;
    }
    for (size_t i = 0; i < n; i++) {
      uint64_t key = 0;
      memcpy(&key, chunk + i * RECORD_BYTES, sizeof(key));
      const uint8_t state = chunk[i * RECORD_BYTES + sizeof(key)];
      // Binary search depends on strictly ascending keys.
      if (!validState(state) || (count > 0 && key <= keys[count - 1])) {
        LOG_ERR("RST", "ignoring damaged reading status file");
        count = 0;
        loaded = true;
        damagedFile = true;
        return true;
      }
      keys[count] = key;
      states[count] = state;
      count++;
    }
    done = static_cast<uint16_t>(done + n);
  }
  seededFlag = seededOnCard;
  loaded = true;
  return true;
}

bool ReadingStatusStore::save() {
  if (!loaded) return false;
  if (damagedFile) {
    keepDamagedCopy();
    damagedFile = false;
  }
  Storage.mkdir("/.crosspoint");
  {
    HalFile file;
    if (!Storage.openFileForWrite("RST", TMP_PATH, file)) {
      LOG_ERR("RST", "cannot write %s", TMP_PATH);
      return false;
    }
    if (!writeHeader(file, count, seededFlag)) {
      LOG_ERR("RST", "short write to %s", TMP_PATH);
      return false;
    }
    RecordSink sink(file);
    for (uint16_t i = 0; i < count; i++) {
      if (!sink.push(keys[i], states[i])) {
        LOG_ERR("RST", "short write to %s", TMP_PATH);
        return false;
      }
    }
    if (!sink.flush()) {
      LOG_ERR("RST", "short write to %s", TMP_PATH);
      return false;
    }
    file.flush();
    // The temp file closes at scope exit, before it is moved into place.
  }
  return commitTmp();
}

int ReadingStatusStore::find(const uint64_t key) const {
  return static_cast<int>(std::lower_bound(keys.get(), keys.get() + count, key) - keys.get());
}

bool ReadingStatusStore::has(const uint64_t key) const {
  const int at = find(key);
  return at < count && keys[at] == key;
}

ReadingState ReadingStatusStore::get(const uint64_t key) const {
  const int at = find(key);
  if (at >= count || keys[at] != key) return ReadingState::Unread;
  return static_cast<ReadingState>(states[at]);
}

bool ReadingStatusStore::set(const uint64_t key, const ReadingState state) {
  if (!loaded) return false;
  const int at = find(key);
  if (at < count && keys[at] == key) {
    states[at] = static_cast<uint8_t>(state);
    return true;
  }
  if (count >= MAX_ENTRIES) {
    LOG_ERR("RST", "reading status store full (%u books)", static_cast<unsigned>(count));
    return false;
  }
  // Geometric growth keeps the history import from reallocating per few books.
  if (count == capacity &&
      !reserve(static_cast<uint16_t>(std::min<int>(std::max<int>(16, capacity + capacity / 2), MAX_ENTRIES)))) {
    return false;
  }
  std::copy_backward(keys.get() + at, keys.get() + count, keys.get() + count + 1);
  std::copy_backward(states.get() + at, states.get() + count, states.get() + count + 1);
  keys[at] = key;
  states[at] = static_cast<uint8_t>(state);
  count++;
  return true;
}

void ReadingStatusStore::erase(const uint64_t key) {
  const int at = find(key);
  if (at >= count || keys[at] != key) return;
  std::copy(keys.get() + at + 1, keys.get() + count, keys.get() + at);
  std::copy(states.get() + at + 1, states.get() + count, states.get() + at);
  count--;
}

bool ReadingStatusStore::importProgress(const uint64_t key, const bool atEndOfBook) {
  if (has(key)) return true;
  return set(key, atEndOfBook ? ReadingState::Finished : ReadingState::Reading);
}

bool ReadingStatusStore::update(const std::string& path, const ReadingState state) {
  return modifyFile(keyFor(path), [state](bool, ReadingState) { return Change{Change::Op::Put, state}; });
}

bool ReadingStatusStore::removeBook(const std::string& path) {
  return modifyFile(keyFor(path), [](bool, ReadingState) { return Change{Change::Op::Drop}; });
}

void ReadingStatusStore::rename(const std::string& oldPath, const std::string& newPath) {
  const uint64_t oldKey = keyFor(oldPath);
  bool found = false;
  ReadingState state = ReadingState::Unread;
  // Reads without writing: the decision only records what is stored.
  modifyFile(oldKey, [&found, &state](const bool present, const ReadingState current) {
    found = present;
    state = current;
    return Change{};
  });
  // Write the new path first, so a failure in between keeps the state somewhere.
  if (found && update(newPath, state)) removeBook(oldPath);
}

void ReadingStatusStore::recordReaderExit(const std::string& path, const bool atEndOfBook, const bool openedAtEnd) {
  modifyFile(keyFor(path), [atEndOfBook, openedAtEnd](const bool present, const ReadingState current) {
    if (present && current == ReadingState::Finished) return Change{};
    // A book with no record that opened at its end was finished before this store existed.
    const bool finished = atEndOfBook || (!present && openedAtEnd);
    return Change{Change::Op::Put, finished ? ReadingState::Finished : ReadingState::Reading};
  });
}
