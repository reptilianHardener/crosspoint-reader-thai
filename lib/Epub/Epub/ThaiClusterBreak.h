#pragma once

#include <cstddef>
#include <cstdint>

// Lightweight Thai syllable boundary helpers for line breaking on ESP32-C3 (no dictionary, flash-only).
// Uses a rule-based model: leading vowels, consonant clusters (กร-style), dependent marks, spacing vowels,
// and a single coda consonant. Not full lexicon segmentation.

bool thaiIsThaiScriptCp(uint32_t cp);

// Returns byte length of the first complete Thai syllable at the start of buf, or 0 if buf does not start
// with Thai, UTF-8 is incomplete, or the first syllable is not yet complete.
size_t thaiFirstCompleteSyllableUtf8Bytes(const char* buf, size_t len);

enum class ThaiScriptClass : uint8_t { None = 0, Latin, Thai, Other };

ThaiScriptClass thaiClassifyScript(uint32_t cp);

// Byte index in buf where the last complete UTF-8 codepoint begins (0 if len==0).
size_t thaiUtf8LastCodepointStart(const char* buf, size_t len);
