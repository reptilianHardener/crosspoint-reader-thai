#include "ThaiClusterBreak.h"

#include <Utf8.h>

bool thaiIsThaiScriptCp(const uint32_t cp) { return cp >= 0x0E00 && cp <= 0x0E7F; }

ThaiScriptClass thaiClassifyScript(const uint32_t cp) {
  if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
    return ThaiScriptClass::Latin;
  }
  if (thaiIsThaiScriptCp(cp)) {
    return ThaiScriptClass::Thai;
  }
  return ThaiScriptClass::Other;
}

size_t thaiUtf8LastCodepointStart(const char* buf, const size_t len) {
  if (len == 0) {
    return 0;
  }
  size_t i = len;
  while (i > 0 && (static_cast<uint8_t>(buf[i - 1]) & 0xC0) == 0x80) {
    --i;
  }
  return i;
}

namespace {

bool isLeadVowel(const uint32_t cp) { return cp >= 0x0E40 && cp <= 0x0E44; }

bool isMainConsonant(const uint32_t cp) {
  // Thai consonants +อ as vowel carrier (still onset)
  return (cp >= 0x0E01 && cp <= 0x0E2E) || cp == 0x0E2D;
}

// Common second consonant in historical digraph onsets (กร, กล, คว, …).
bool isClusterSecondConsonant(const uint32_t cp) {
  switch (cp) {
    case 0x0E22:  // ย
    case 0x0E23:  // ร
    case 0x0E25:  // ล
    case 0x0E27:  // ว
      return true;
    default:
      return false;
  }
}

bool isSpacingThaiVowelLetter(const uint32_t cp) {
  return cp == 0x0E30 || cp == 0x0E32 || cp == 0x0E33;  // sara a, aa, am
}

// Decode one codepoint; returns 0 on invalid/incomplete.
uint32_t peekCp(const unsigned char* p, const unsigned char* end, const unsigned char** outNext) {
  if (p >= end) {
    return 0;
  }
  const unsigned char* q = p;
  const uint32_t cp = utf8NextCodepoint(&q);
  if (cp == 0 || q > end) {
    return 0;
  }
  *outNext = q;
  return cp;
}

// Consumes one Thai syllable from [p, end). Returns new p after syllable, or nullptr if incomplete/invalid.
const unsigned char* consumeThaiSyllable(const unsigned char* p, const unsigned char* end) {
  const unsigned char* const start = p;
  if (p >= end) {
    return nullptr;
  }

  const unsigned char* next = nullptr;
  uint32_t cp = peekCp(p, end, &next);
  if (cp == 0 || !thaiIsThaiScriptCp(cp)) {
    return nullptr;
  }

  bool hasLeadVowel = false;
  if (isLeadVowel(cp)) {
    hasLeadVowel = true;
    p = next;
    if (p >= end) {
      return nullptr;
    }
    cp = peekCp(p, end, &next);
    if (!isMainConsonant(cp)) {
      return nullptr;
    }
  } else if (!isMainConsonant(cp)) {
    // Thai punctuation, digits, etc. — not a syllable start here
    return nullptr;
  }

  // Onset: first consonant (after optional lead vowel already consumed)
  p = next;

  bool hadSpacingVowel = hasLeadVowel;
  bool hadCluster = false;

  if (p < end) {
    const unsigned char* clusterNext = nullptr;
    const uint32_t cp2 = peekCp(p, end, &clusterNext);
    if (isMainConsonant(cp2) && isClusterSecondConsonant(cp2)) {
      p = clusterNext;
      hadCluster = true;
    }
  }

  // Dependent vowel marks, tone marks, other combining marks (per Utf8.h helper)
  while (p < end) {
    const unsigned char* markNext = nullptr;
    const uint32_t m = peekCp(p, end, &markNext);
    if (m == 0) {
      return nullptr;
    }
    if (utf8IsThaiCombiningMark(m)) {
      p = markNext;
      continue;
    }
    if (isSpacingThaiVowelLetter(m)) {
      p = markNext;
      hadSpacingVowel = true;
      continue;
    }
    break;
  }

  // Optional coda consonant only for implicit-vowel syllables (e.g. กง, กรง). When a spacing vowel
  // ends the nucleus, the following consonant starts the next syllable — do not attach as coda.
  if (p < end && !hadSpacingVowel && !hasLeadVowel) {
    const unsigned char* codaNext = nullptr;
    const uint32_t coda = peekCp(p, end, &codaNext);
    if (coda == 0) {
      return nullptr;
    }
    if (isMainConsonant(coda)) {
      const bool takeImplicitCoda = hadCluster || !isClusterSecondConsonant(coda);
      if (takeImplicitCoda) {
        p = codaNext;
      }
    }
  }

  return (p > start) ? p : nullptr;
}

}  // namespace

size_t thaiFirstCompleteSyllableUtf8Bytes(const char* buf, const size_t len) {
  if (!buf || len == 0) {
    return 0;
  }
  const int safe = utf8SafeTruncateBuffer(buf, static_cast<int>(len));
  if (safe <= 0) {
    return 0;
  }
  const auto* p = reinterpret_cast<const unsigned char*>(buf);
  const auto* end = p + static_cast<size_t>(safe);
  const unsigned char* after = consumeThaiSyllable(p, end);
  if (after == nullptr) {
    return 0;
  }
  return static_cast<size_t>(after - p);
}
