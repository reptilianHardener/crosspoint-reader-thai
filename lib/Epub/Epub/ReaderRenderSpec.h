#pragma once
#include <cstdint>

// The resolved text-rendering configuration a reader hands to the layout
// engine. Section-cache validation keys on every field: a section file built
// with a different spec is discarded and rebuilt.
//
// Build one via CrossPointSettings::readerRenderSpec(width, height), which
// fills every field: the settings-derived ones from the store, the viewport
// from the caller. Taking the viewport as arguments is what keeps a spec from
// existing in a half-filled state — the 0 defaults below are a last-resort
// backstop (a 0x0 viewport lays out nothing), not an invitation to omit it.
struct ReaderRenderSpec {
  int fontId = 0;
  // Multiplier of font size in pixels. Thai and Japanese books also apply their script pitch
  // (see applyScriptLineSpacing).
  float lineCompression = 1.0f;
  bool extraParagraphSpacing = false;
  uint8_t paragraphIndentSpaces = 2;
  int8_t characterSpacing = 0;
  uint8_t wordSpacingPercent = 100;
  uint8_t paragraphAlignment = 0;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  bool embeddedStyle = true;
  uint8_t imageRendering = 0;
  bool focusReadingEnabled = false;
  // Lay out every run in the bold face regardless of the book's markup.
  bool forceBold = false;
  // Fingerprint of the Thai user dictionary (0 = none loaded). Word boundaries, and so
  // line breaks, depend on it, so editing the dictionary must invalidate cached sections.
  uint16_t thaiDictionaryRevision = 0;
  // Tategaki: the page is laid out as vertical lines stacked right to left. viewportWidth is
  // then the line length (down the screen) and viewportHeight the room for lines (across it).
  bool verticalText = false;
  // Share of the font's line height (advanceY) its glyphs can reach: usually below 1 in Japanese
  // books, whose lines are set tighter than a font with Thai marks (see applyScriptLineSpacing).
  float glyphExtentScale = 1.0f;
};

// Stable signature for clipping anchors. Page-local word ordinals are valid
// only for the exact layout that produced them.
inline uint32_t readerRenderSpecSignature(const ReaderRenderSpec& spec) {
  uint32_t signature = 2166136261U;
  const auto mix = [&signature](const uint32_t value) {
    signature ^= value;
    signature *= 16777619U;
  };
  // Bump when the same spec values lay out differently, so old clipping word ranges stop matching.
  constexpr uint32_t LAYOUT_SIGNATURE_VERSION = 1;
  mix(LAYOUT_SIGNATURE_VERSION);
  mix(static_cast<uint32_t>(spec.fontId));
  mix(static_cast<uint32_t>(spec.lineCompression * 1000.0f));
  mix(spec.extraParagraphSpacing);
  mix(static_cast<uint8_t>(spec.characterSpacing));
  mix(spec.wordSpacingPercent);
  mix(spec.paragraphAlignment);
  mix(spec.viewportWidth);
  mix(spec.viewportHeight);
  mix(spec.hyphenationEnabled);
  mix(spec.embeddedStyle);
  mix(spec.imageRendering);
  mix(spec.focusReadingEnabled);
  mix(spec.forceBold);
  mix(spec.thaiDictionaryRevision);
  mix(spec.verticalText);
  return signature == 0 ? 1 : signature;
}
