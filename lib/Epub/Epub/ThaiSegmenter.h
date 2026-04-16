#pragma once

#include <HalStorage.h>

#include <cstdint>

// Longest-match Thai word segmenter backed by a two-tier dictionary:
//   Tier 1 (Flash): top 5000 words in ThaiDictCore.h — zero RAM cost.
//   Tier 2 (SD):    ~56K extended words in /thai_dict_extended.bin,
//                   accessed via a coarse block index kept in static DRAM.
// Self-initialises the SD tier on first call to segment().
class ThaiSegmenter {
 public:
  static ThaiSegmenter& getInstance();

  // Segment a Thai text run (len bytes starting at text).
  // Calls cb(word, wordLen, attachToPrevious, ctx) for each token.
  // attachToPrevious is false only for the first token; use the caller's
  // own nextWordContinues flag for that first token.
  void segment(const char* text, int len, void (*cb)(const char*, int, bool, void*), void* ctx);

  // Returns true if text[0..] begins with a Thai codepoint (U+0E00-0E7F).
  static bool startsWithThai(const char* text, int len);

 private:
  ThaiSegmenter() = default;

  void ensureInit();
  bool init();

  static bool lookupFlash(const char* text, int wordLen);
  bool lookupSd(const char* text, int wordLen);
  bool lookup(const char* text, int wordLen);

  // Advance one grapheme cluster from text (base codepoint + any Thai diacritics).
  static int nextCluster(const char* text, int remaining);

  // Three-way compare: negative = dictWord < query, 0 = exact match, positive = dictWord > query.
  // dictWord is a null-terminated C-string; query has exactly wordLen bytes (not null-terminated).
  static int cmpDictQuery(const char* dictWord, const char* query, int wordLen);

  static constexpr int KEY_LEN = 32;
  static constexpr uint32_t SD_MAGIC = 0x49414854;  // 'THAI' LE
  static constexpr uint32_t SD_VERSION = 1;
  static constexpr int MAX_COARSE_ENTRIES = 500;  // supports up to 64 000 words at block_size=128
  static constexpr int MAX_THAI_WORD_BYTES = 90;  // 30 Thai codepoints × 3 bytes

  struct CoarseEntry {
    uint32_t offset;
    uint8_t key[KEY_LEN];
  };

  bool initialized_ = false;
  bool sdAvailable_ = false;
  FsFile sdFile_;
  uint32_t sdWordCount_ = 0;
  uint32_t sdBlockSize_ = 0;
  int sdNumBlocks_ = 0;
  CoarseEntry coarseIndex_[MAX_COARSE_ENTRIES];

  static ThaiSegmenter instance_;
};

#define THAI_SEGMENTER ThaiSegmenter::getInstance()
