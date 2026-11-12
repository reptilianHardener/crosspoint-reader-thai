#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "HyphenationCommon.h"

class ThaiWordBreaker {
 public:
  // Returns legal Thai line-break positions as codepoint indexes inside cps.
  // Breaks are emitted only at dictionary-derived word boundaries unless
  // includeFallback is enabled, in which case unknown runs additionally expose
  // safe Thai cluster boundaries as a last resort. hintIndexes are positions where
  // the source had a zero-width space or soft hyphen; one counts as a break only
  // inside text the dictionary does not know, and only at a cluster boundary.
  // No break is ever returned inside a user dictionary word.
  static std::vector<size_t> breakIndexes(const std::vector<CodepointInfo>& cps, bool includeFallback,
                                          const std::vector<size_t>* hintIndexes = nullptr);

  // Load a user dictionary from a list of UTF-8 Thai words.
  // Words are stored as codepoint sequences for O(1) per-character matching.
  // Maximum 200 words to stay within ESP32-C3 memory constraints (~12 KB).
  static void setUserDictionary(const std::vector<std::string>& words);

  // True for a usable user word: at least two codepoints, all Thai, starting and ending where a
  // line may break. Other entries are dropped when the dictionary loads.
  static bool isValidUserWord(const std::string& word);

  // Byte offset in text[0, len) of the earliest user word that starts there but runs past len,
  // or len when there is none. Cutting text at or before it cannot split that word.
  static size_t userWordCutLimit(const char* text, size_t len);

  // Returns true if a user dictionary has been loaded.
  static bool hasUserDictionary();

  // Fingerprint of the loaded user dictionary: 0 when empty, otherwise a non-zero hash of
  // its words. Section caches key on it so that editing the dictionary re-lays-out books.
  static uint16_t userDictionaryRevision() { return userDictRevision_; }

  // Provides read access to the user dictionary for the DAWG matching function.
  static const std::vector<std::vector<uint16_t>>& getUserDictWords() { return userDictWords_; }

 private:
  // Each entry is a word stored as a vector of Unicode codepoints.
  static std::vector<std::vector<uint16_t>> userDictWords_;
  static uint16_t userDictRevision_;
};
