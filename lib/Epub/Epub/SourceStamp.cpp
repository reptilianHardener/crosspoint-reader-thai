#include "SourceStamp.h"

#include <HalStorage.h>

#include <cstdint>

namespace {

constexpr char STAMP_FILE[] = "/source.bin";

struct Stamp {
  uint32_t size = 0;
  uint32_t modificationTime = 0;  // FAT date in the high 16 bits, time in the low 16; 0 when unknown
};

bool statSource(const std::string& sourcePath, Stamp& out) {
  HalFile file;
  if (!Storage.openFileForRead("STP", sourcePath, file)) return false;
  out.size = static_cast<uint32_t>(file.fileSize());
  out.modificationTime = file.modificationTime();
  file.close();
  return true;
}

}  // namespace

SourceStamp::State SourceStamp::check(const std::string& cacheDir, const std::string& sourcePath) {
  Stamp stored;
  {
    HalFile file;
    if (!Storage.openFileForRead("STP", cacheDir + STAMP_FILE, file)) return State::Unknown;
    const auto read = file.read(&stored, sizeof(stored));
    file.close();
    if (read != static_cast<decltype(read)>(sizeof(stored))) return State::Unknown;
  }
  Stamp current;
  if (!statSource(sourcePath, current)) return State::Unknown;
  if (current.size != stored.size) return State::Changed;
  if (current.modificationTime != 0 && stored.modificationTime != 0 &&
      current.modificationTime != stored.modificationTime) {
    return State::Changed;
  }
  return State::Same;
}

bool SourceStamp::write(const std::string& cacheDir, const std::string& sourcePath) {
  Stamp current;
  if (!statSource(sourcePath, current)) return false;
  HalFile file;
  if (!Storage.openFileForWrite("STP", cacheDir + STAMP_FILE, file)) return false;
  const bool ok = file.write(&current, sizeof(current)) == sizeof(current);
  file.close();
  return ok;
}
