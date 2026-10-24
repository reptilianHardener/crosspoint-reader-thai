#include "ThaiUserDictionary.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "Epub/hyphenation/ThaiWordBreaker.h"

namespace ThaiUserDictionary {

namespace {
// 200 words of Thai (3 bytes per character) fit comfortably; anything past this is
// ignored rather than read in pieces.
constexpr size_t MAX_FILE_BYTES = 8192;
}  // namespace

bool load(std::vector<std::string>& words) {
  words.clear();
  if (!Storage.exists(FILE_PATH)) {
    return true;
  }

  // Heap, not stack: 8 KB is far over the 256-byte limit for locals.
  const auto buffer = makeUniqueNoThrow<char[]>(MAX_FILE_BYTES);
  if (!buffer) {
    LOG_ERR("THAI", "OOM: %d bytes for user dictionary", static_cast<int>(MAX_FILE_BYTES));
    return false;
  }
  const char* buf = buffer.get();

  const size_t bytesRead = Storage.readFileToBuffer(FILE_PATH, buffer.get(), MAX_FILE_BYTES);
  words.reserve(32);
  size_t lineStart = 0;
  for (size_t i = 0; i <= bytesRead && words.size() < MAX_WORDS; ++i) {
    if (i != bytesRead && buf[i] != '\n' && buf[i] != '\r') {
      continue;
    }
    size_t lineEnd = i;
    while (lineEnd > lineStart && (buf[lineEnd - 1] == ' ' || buf[lineEnd - 1] == '\t')) {
      --lineEnd;
    }
    if (lineEnd > lineStart && buf[lineStart] != '#') {
      words.emplace_back(buf + lineStart, lineEnd - lineStart);
    }
    lineStart = i + 1;
  }
  return true;
}

bool save(const std::vector<std::string>& words) {
  if (!Storage.exists("/crosspoint")) {
    Storage.mkdir("/crosspoint");
  }

  HalFile file;
  if (!Storage.openFileForWrite("THAI", FILE_PATH, file)) {
    LOG_ERR("THAI", "Failed to open %s for write", FILE_PATH);
    return false;
  }
  for (const auto& word : words) {
    file.write(reinterpret_cast<const uint8_t*>(word.data()), word.size());
    file.write(static_cast<uint8_t>('\n'));
  }
  LOG_INF("THAI", "Saved %d user dictionary words", static_cast<int>(words.size()));
  return true;
}

void applyFromStorage() {
  std::vector<std::string> words;
  if (load(words)) {
    ThaiWordBreaker::setUserDictionary(words);
    if (!words.empty()) {
      LOG_INF("THAI", "Loaded %d user dictionary words", static_cast<int>(words.size()));
    }
  }
}

}  // namespace ThaiUserDictionary
