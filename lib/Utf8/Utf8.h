#pragma once

#include <cstdint>
#include <string>
#define REPLACEMENT_GLYPH 0xFFFD

uint32_t utf8NextCodepoint(const unsigned char** string);
// Appends a Unicode codepoint to a std::string in UTF-8 encoding.
void utf8AppendCodepoint(uint32_t cp, std::string& out);
// Remove the last UTF-8 codepoint from a std::string and return the new size.
size_t utf8RemoveLastChar(std::string& str);
// Truncate string by removing N UTF-8 codepoints from the end.
void utf8TruncateChars(std::string& str, size_t numChars);

// Canonical composition (NFC) for the Latin / Vietnamese range and Hangul:
// precomposes a base letter followed by combining diacritical mark(s), and
// conjoining Hangul jamo sequences (L+V[+T]), into single codepoints. Needed
// because the device fonts have no combining-mark positioning and carry only
// precomposed Hangul syllables, so NFD text (some EPUB chapter titles; every
// filename written by macOS) otherwise renders broken or blank.
// Report absorbed input codepoint positions when requested; return empty if the callback fails.
std::string utf8ComposeNfc(const std::string& in, bool (*onAbsorbed)(uint32_t, void*) = nullptr,
                           void* context = nullptr);

// Compose a null-terminated display buffer without allocating or growing it.
// Uncomposed bytes (including malformed UTF-8) are preserved unchanged.
void utf8ComposeNfcInPlace(char* buffer);

// The base letter a precomposed codepoint decomposes to, or 0 when there is
// none ("é" -> "e", but "ø" -> 0: it is a letter in its own right, not
// o-with-stroke). Lives here rather than in a caller because the compose table
// is a ~5 KB static array in the header: a second includer is a second copy in
// flash.
//
// A linear scan, unlike utf8ComposePair's binary search above it: the table is
// sorted by (base, mark), which this lookup searches against the grain. Adding
// a second table sorted by composed would cost more flash than sharing this one
// saves.
uint32_t utf8DecomposedBase(uint32_t cp);

// Truncate a raw char buffer to the last complete UTF-8 codepoint boundary.
// Returns the new length (<= len). If the buffer ends mid-sequence, the
// incomplete trailing bytes are excluded.
int utf8SafeTruncateBuffer(const char* buf, int len);

// Returns true for CJK characters that break without hyphenation. Covers CJK Unified Ideographs,
// Hiragana, Katakana, Hangul, CJK punctuation, and fullwidth forms. Han and Kana have implicit
// word boundaries per character; Hangul is space-delimited, so layout keeps Hangul words whole
// (see utf8IsHangul) and splits one without a hyphen only through the hyphenation path.
inline bool utf8IsCjkBreakable(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF)        // Hangul Jamo
         || (cp >= 0x3000 && cp <= 0x303F)     // CJK Symbols and Punctuation
         || (cp >= 0x3040 && cp <= 0x309F)     // Hiragana
         || (cp >= 0x30A0 && cp <= 0x30FF)     // Katakana
         || (cp >= 0x3130 && cp <= 0x318F)     // Hangul Compatibility Jamo
         || (cp >= 0x3400 && cp <= 0x4DBF)     // CJK Extension A
         || (cp >= 0x4E00 && cp <= 0x9FFF)     // CJK Unified Ideographs
         || (cp >= 0xAC00 && cp <= 0xD7AF)     // Hangul Syllables
         || (cp >= 0xD7B0 && cp <= 0xD7FF)     // Hangul Jamo Extended-B
         || (cp >= 0xF900 && cp <= 0xFAFF)     // CJK Compatibility Ideographs
         || (cp >= 0xFE30 && cp <= 0xFE4F)     // CJK Compatibility Forms
         || (cp >= 0xFF01 && cp <= 0xFF60)     // Fullwidth Latin / Punctuation
         || (cp >= 0xFF65 && cp <= 0xFFEF)     // Halfwidth Katakana / Hangul
         || (cp >= 0x20000 && cp <= 0x2A6DF)   // CJK Extension B
         || (cp >= 0x2A700 && cp <= 0x2B73F);  // CJK Extension C
}

// Returns true for Hangul letters. Korean separates words with spaces, so a boundary touching
// Hangul is never an implicit line-break opportunity (CSS word-break: keep-all).
inline bool utf8IsHangul(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF)      // Hangul Jamo
         || (cp >= 0x3130 && cp <= 0x318F)   // Hangul Compatibility Jamo
         || (cp >= 0xA960 && cp <= 0xA97F)   // Hangul Jamo Extended-A
         || (cp >= 0xAC00 && cp <= 0xD7FF)   // Hangul Syllables, Hangul Jamo Extended-B
         || (cp >= 0xFFA0 && cp <= 0xFFDC);  // Halfwidth Hangul
}

// Writes cp as UTF-8 (1-4 bytes, no terminator) and returns the position after it.
inline char* utf8AppendCodepoint(char* output, const uint32_t codepoint) {
  if (codepoint < 0x80) {
    *output++ = static_cast<char>(codepoint);
  } else if (codepoint < 0x800) {
    *output++ = static_cast<char>(0xC0 | (codepoint >> 6));
    *output++ = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x10000) {
    *output++ = static_cast<char>(0xE0 | (codepoint >> 12));
    *output++ = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    *output++ = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    *output++ = static_cast<char>(0xF0 | (codepoint >> 18));
    *output++ = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    *output++ = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    *output++ = static_cast<char>(0x80 | (codepoint & 0x3F));
  }
  return output;
}

// Uppercase partner of a lowercase Latin, Greek or Cyrillic letter; other codepoints are
// returned unchanged. Used to synthesize small caps, so letters without a single-codepoint
// uppercase form (e.g. U+00DF) stay as they are.
inline uint32_t utf8SmallCapsUpper(const uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') return cp - 0x20;
  if (cp < 0xE0) return cp;
  if (cp <= 0xFE) return cp == 0xF7 ? cp : cp - 0x20;  // Latin-1 (skips the division sign)
  if (cp == 0xFF) return 0x178;
  if (cp <= 0x17F) {
    // Latin Extended-A pairs: lowercase on odd codepoints, except the two odd-aligned runs.
    const bool oddAligned = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
    if (cp == 0x131 || cp == 0x138 || cp == 0x149 || cp == 0x17F) return cp;
    return (cp % 2 == (oddAligned ? 0u : 1u)) ? cp - 1 : cp;
  }
  if (cp >= 0x3B1 && cp <= 0x3C9) return cp == 0x3C2 ? 0x3A3 : cp - 0x20;  // Greek (final sigma -> Sigma)
  if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;                        // Cyrillic
  if (cp >= 0x450 && cp <= 0x45F) return cp - 0x50;
  return cp;
}

// Returns true for any codepoint in a CJK script block (Han, Kana, Hangul, Bopomofo,
// radicals, and CJK punctuation/compatibility/enclosed forms). Used for fallback font
// selection — deliberately broader than utf8IsCjkBreakable, whose ranges are tuned to
// implicit line-break opportunities and must not grow without rethinking layout.
inline bool utf8IsCjkCodepoint(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF)        // Hangul Jamo
         || (cp >= 0x2E80 && cp <= 0x2FDF)     // CJK Radicals Supplement, Kangxi Radicals
         || (cp >= 0x3000 && cp <= 0x33FF)     // CJK punctuation, Kana, Bopomofo, Hangul Compat
                                               // Jamo, Kanbun, strokes, enclosed + compat forms
         || (cp >= 0x3400 && cp <= 0x4DBF)     // CJK Extension A
         || (cp >= 0x4E00 && cp <= 0x9FFF)     // CJK Unified Ideographs
         || (cp >= 0xA960 && cp <= 0xA97F)     // Hangul Jamo Extended-A
         || (cp >= 0xAC00 && cp <= 0xD7FF)     // Hangul Syllables, Hangul Jamo Extended-B
         || (cp >= 0xF900 && cp <= 0xFAFF)     // CJK Compatibility Ideographs
         || (cp >= 0xFE10 && cp <= 0xFE1F)     // Vertical Forms
         || (cp >= 0xFE30 && cp <= 0xFE4F)     // CJK Compatibility Forms
         || (cp >= 0xFF01 && cp <= 0xFF60)     // Fullwidth Latin / Punctuation
         || (cp >= 0xFF65 && cp <= 0xFFEF)     // Halfwidth Katakana / Hangul
         || (cp >= 0x20000 && cp <= 0x2EBEF)   // CJK Extensions B-F
         || (cp >= 0x2F800 && cp <= 0x2FA1F)   // CJK Compatibility Ideographs Supplement
         || (cp >= 0x30000 && cp <= 0x323AF);  // CJK Extensions G-H
}

// Glyph orientation in vertical (tategaki) lines, after UAX #50. Upright codepoints stand
// as they do in horizontal text. Everything else is set sideways with the line: Latin and
// digits, and also brackets, dashes and the long vowel mark, for which a quarter turn is the
// vertical form.
inline bool utf8IsVerticalUpright(const uint32_t cp) {
  if (cp < 0x1100) return false;
  if ((cp >= 0x3008 && cp <= 0x3011) || (cp >= 0x3014 && cp <= 0x301F)) return false;  // brackets, wave dash
  if (cp == 0x3030 || cp == 0x30A0 || cp == 0x30FC) return false;  // wavy dash, double hyphen, long vowel mark
  if (cp >= 0xFF00 && cp <= 0xFFEF) {
    if (cp == 0xFF08 || cp == 0xFF09 || cp == 0xFF0D || cp == 0xFF3B || cp == 0xFF3D || cp == 0xFF3F) return false;
    if (cp >= 0xFF1A && cp <= 0xFF1E) return false;  // colon, semicolon, comparison signs
    if (cp >= 0xFF5B && cp <= 0xFFDF) return false;  // braces, tilde, halfwidth forms
    return cp != 0xFFE3 && cp <= 0xFFE7;
  }
  return utf8IsCjkCodepoint(cp) || cp == 0x203B  // reference mark
         || (cp >= 0x2460 && cp <= 0x24FF)       // enclosed alphanumerics
         || (cp >= 0x25A0 && cp <= 0x27BF);      // shapes, symbols, dingbats
}

// Where an upright glyph sits in its vertical cell when that differs from the horizontal cell.
enum class VerticalGlyphShift : uint8_t {
  None,
  Corner,  // ideographic comma and full stop: lower left of the cell becomes upper right
  Small,   // small kana: nudged toward the upper right
};

inline VerticalGlyphShift utf8VerticalGlyphShift(uint32_t cp) {
  if (cp == 0x3001 || cp == 0x3002 || cp == 0xFF0C || cp == 0xFF0E) return VerticalGlyphShift::Corner;
  if (cp >= 0x30A1 && cp <= 0x30F6) cp -= 0x60;  // katakana mirrors the hiragana block
  switch (cp) {
    case 0x3041:
    case 0x3043:
    case 0x3045:
    case 0x3047:
    case 0x3049:
    case 0x3063:
    case 0x3083:
    case 0x3085:
    case 0x3087:
    case 0x308E:
    case 0x3095:
    case 0x3096:
      return VerticalGlyphShift::Small;
    default:
      return VerticalGlyphShift::None;
  }
}

// Thai (U+0E00-U+0E7F). Thai is written without spaces between words and stacks up to two
// marks above a consonant, so layout and rendering both need to classify its codepoints.
inline bool utf8IsThaiCodepoint(const uint32_t cp) { return cp >= 0x0E00 && cp <= 0x0E7F; }

// Below-base vowels (sara u, sara uu) and phinthu.
inline bool utf8IsThaiLowerCombiningMark(const uint32_t cp) { return cp >= 0x0E38 && cp <= 0x0E3A; }

// First tier above the base: upper vowels, mai han akat, mai tai khu, nikhahit.
inline bool utf8IsThaiUpperLevelTwoMark(const uint32_t cp) {
  return cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E37) || cp == 0x0E47 || cp == 0x0E4D;
}

// Second tier above the base: tone marks, thanthakhat, yamakkan. These sit on top of a
// level-two mark when one is present.
inline bool utf8IsThaiUpperLevelThreeMark(const uint32_t cp) { return (cp >= 0x0E48 && cp <= 0x0E4C) || cp == 0x0E4E; }

inline bool utf8IsThaiUpperCombiningMark(const uint32_t cp) {
  return utf8IsThaiUpperLevelTwoMark(cp) || utf8IsThaiUpperLevelThreeMark(cp);
}

inline bool utf8IsThaiCombiningMark(const uint32_t cp) {
  return utf8IsThaiLowerCombiningMark(cp) || utf8IsThaiUpperCombiningMark(cp);
}

// Returns true for Unicode combining diacritical marks that should not advance the cursor.
inline bool utf8IsCombiningMark(const uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F)     // Combining Diacritical Marks
         || (cp >= 0x1DC0 && cp <= 0x1DFF)  // Combining Diacritical Marks Supplement
         || (cp >= 0x20D0 && cp <= 0x20FF)  // Combining Diacritical Marks for Symbols
         || (cp >= 0xFE20 && cp <= 0xFE2F)  // Combining Half Marks
         || utf8IsThaiCombiningMark(cp);    // Thai above/below vowels and tone marks
}
