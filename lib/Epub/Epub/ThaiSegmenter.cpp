#include "ThaiSegmenter.h"

#include <Logging.h>
#include <Utf8.h>

#include <cstring>

#include "ThaiDictCore.h"

ThaiSegmenter ThaiSegmenter::instance_;

ThaiSegmenter& ThaiSegmenter::getInstance() { return instance_; }

bool ThaiSegmenter::startsWithThai(const char* text, int len) {
  if (len < 3) return false;
  const auto b0 = static_cast<uint8_t>(text[0]);
  const auto b1 = static_cast<uint8_t>(text[1]);
  return b0 == 0xE0 && (b1 == 0xB8 || b1 == 0xB9);
}

// dictWord is null-terminated; query has exactly wordLen bytes.
// Returns <0 if dictWord < query, 0 if exact match, >0 if dictWord > query.
int ThaiSegmenter::cmpDictQuery(const char* dictWord, const char* query, int wordLen) {
  const int cmp = strncmp(dictWord, query, static_cast<size_t>(wordLen));
  if (cmp != 0) return cmp;
  // If strncmp says equal for first wordLen bytes, dictWord is longer if its
  // next byte is non-null (returns positive), or an exact match if null.
  return static_cast<int>(static_cast<uint8_t>(dictWord[wordLen]));
}

bool ThaiSegmenter::lookupFlash(const char* text, int wordLen) {
  int lo = 0;
  int hi = static_cast<int>(THAI_DICT_CORE_COUNT) - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const char* dictWord = THAI_DICT_CORE_DATA + THAI_DICT_CORE_OFFSETS[mid];
    const int cmp = cmpDictQuery(dictWord, text, wordLen);
    if (cmp < 0) {
      lo = mid + 1;
    } else if (cmp > 0) {
      hi = mid - 1;
    } else {
      return true;
    }
  }
  return false;
}

bool ThaiSegmenter::lookupSd(const char* text, int wordLen) {
  // Binary search the coarse index (in RAM) for the block whose first key <= query.
  int lo = 0;
  int hi = sdNumBlocks_ - 1;
  int block = -1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const int cmp = cmpDictQuery(reinterpret_cast<const char*>(coarseIndex_[mid].key), text, wordLen);
    if (cmp <= 0) {
      block = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  if (block < 0) return false;  // Query sorts before all blocks.

  if (!sdFile_.seekSet(coarseIndex_[block].offset)) return false;

  // Determine how many words are in this block.
  const uint32_t blockStart = static_cast<uint32_t>(block) * sdBlockSize_;
  uint32_t wordsLeft = sdBlockSize_;
  if (blockStart + wordsLeft > sdWordCount_) {
    wordsLeft = sdWordCount_ - blockStart;
  }

  // Sequential scan: read null-terminated words and compare until past target.
  char buf[128];
  for (uint32_t i = 0; i < wordsLeft; i++) {
    int wlen = 0;
    bool overflow = false;
    char c;
    while (sdFile_.read(&c, 1) == 1) {
      if (c == '\0') break;
      if (wlen < static_cast<int>(sizeof(buf)) - 1) {
        buf[wlen++] = c;
      } else {
        overflow = true;
      }
    }
    buf[wlen] = '\0';
    if (overflow) continue;

    const int cmp = cmpDictQuery(buf, text, wordLen);
    if (cmp == 0) return true;
    if (cmp > 0) return false;  // Past target — not in this block.
  }
  return false;
}

bool ThaiSegmenter::lookup(const char* text, int wordLen) {
  if (lookupFlash(text, wordLen)) return true;
  if (sdAvailable_) return lookupSd(text, wordLen);
  return false;
}

int ThaiSegmenter::nextCluster(const char* text, int remaining) {
  if (remaining <= 0) return 0;

  const auto b0 = static_cast<uint8_t>(text[0]);

  // Determine the byte length of the current UTF-8 codepoint.
  int cpLen;
  if (b0 < 0x80) {
    cpLen = 1;
  } else if ((b0 & 0xE0) == 0xC0) {
    cpLen = 2;
  } else if ((b0 & 0xF0) == 0xE0) {
    cpLen = 3;
  } else {
    cpLen = 4;
  }
  if (cpLen > remaining) cpLen = remaining;

  // For non-Thai codepoints, just return the codepoint length.
  if (cpLen != 3) return cpLen;
  const auto b1 = static_cast<uint8_t>(text[1]);
  if (b0 != 0xE0 || (b1 != 0xB8 && b1 != 0xB9)) return 3;

  // Thai cluster: consume the base character plus any following combining marks
  // (tone marks, above/below vowels) which share the same glyph cell.
  int count = 3;
  while (count + 3 <= remaining) {
    const auto c0 = static_cast<uint8_t>(text[count]);
    const auto c1 = static_cast<uint8_t>(text[count + 1]);
    const auto c2 = static_cast<uint8_t>(text[count + 2]);
    if (c0 != 0xE0 || (c1 != 0xB8 && c1 != 0xB9) || (c2 & 0xC0) != 0x80) break;
    const uint32_t cp = (static_cast<uint32_t>(c1 & 0x3F) << 6) | (c2 & 0x3F);
    if (!utf8IsCombiningMark(cp)) break;
    count += 3;
  }
  return count;
}

void ThaiSegmenter::ensureInit() {
  if (initialized_) return;
  initialized_ = true;
  init();
}

bool ThaiSegmenter::init() {
  constexpr char SD_PATH[] = "/thai_dict_extended.bin";

  if (!Storage.openFileForRead("THAI", SD_PATH, sdFile_)) {
    LOG_DBG("THAI", "SD dict not found, using Flash-only segmentation");
    return false;
  }

  uint8_t hdr[16];
  if (sdFile_.read(hdr, 16) != 16) {
    LOG_ERR("THAI", "Failed to read SD dict header");
    sdFile_.close();
    return false;
  }

  uint32_t magic, version, wordCount, blockSize;
  memcpy(&magic,     hdr + 0,  4);
  memcpy(&version,   hdr + 4,  4);
  memcpy(&wordCount, hdr + 8,  4);
  memcpy(&blockSize, hdr + 12, 4);

  if (magic != SD_MAGIC || version != SD_VERSION) {
    LOG_ERR("THAI", "SD dict bad header (magic=%08lX ver=%lu)",
            static_cast<unsigned long>(magic), static_cast<unsigned long>(version));
    sdFile_.close();
    return false;
  }

  sdWordCount_ = wordCount;
  sdBlockSize_ = blockSize;
  sdNumBlocks_ = static_cast<int>((wordCount + blockSize - 1) / blockSize);

  if (sdNumBlocks_ > MAX_COARSE_ENTRIES) {
    LOG_ERR("THAI", "SD dict too large (%d blocks > %d max)", sdNumBlocks_, MAX_COARSE_ENTRIES);
    sdFile_.close();
    return false;
  }

  for (int i = 0; i < sdNumBlocks_; i++) {
    uint8_t entryBuf[4];
    if (sdFile_.read(entryBuf, 4) != 4) {
      LOG_ERR("THAI", "Coarse index read failed at entry %d", i);
      sdFile_.close();
      return false;
    }
    memcpy(&coarseIndex_[i].offset, entryBuf, 4);
    if (sdFile_.read(coarseIndex_[i].key, KEY_LEN) != KEY_LEN) {
      LOG_ERR("THAI", "Coarse key read failed at entry %d", i);
      sdFile_.close();
      return false;
    }
  }

  sdAvailable_ = true;
  LOG_DBG("THAI", "SD dict ready: %lu words, %d blocks",
          static_cast<unsigned long>(wordCount), sdNumBlocks_);
  return true;
}

void ThaiSegmenter::segment(const char* text, int len,
                             void (*cb)(const char*, int, bool, void*), void* ctx) {
  ensureInit();

  int pos = 0;
  bool firstToken = true;

  while (pos < len) {
    const int remaining = len - pos;

    // Thai codepoints are all 3-byte UTF-8 sequences; try candidate lengths
    // from longest to shortest, stepping by 3 bytes.
    int maxCand = (remaining < MAX_THAI_WORD_BYTES) ? remaining : MAX_THAI_WORD_BYTES;
    maxCand = (maxCand / 3) * 3;

    int matchLen = 0;
    for (int candLen = maxCand; candLen >= 3; candLen -= 3) {
      if (lookup(text + pos, candLen)) {
        matchLen = candLen;
        break;
      }
    }

    if (matchLen == 0) {
      // No dictionary match: emit one grapheme cluster (base char + diacritics).
      matchLen = nextCluster(text + pos, remaining);
      if (matchLen <= 0) matchLen = 1;  // Safety: always make forward progress.
    }

    cb(text + pos, matchLen, !firstToken, ctx);
    pos += matchLen;
    firstToken = false;
  }
}
