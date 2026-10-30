#pragma once

#include <cstdint>

class GfxRenderer;
struct ReaderRenderSpec;

// Scripts whose text is set at their own line pitch instead of a share of the font size.
enum class LineScript : uint8_t { Other, Thai, Japanese };

// Line spacing is a percentage of the font size, which suits Latin text. Thai and Japanese
// measure it from their script's own line pitch instead, so the default percentage keeps
// that pitch: Thai takes the font's line height, which leaves room for stacked tone marks;
// Japanese takes 1.45 times the width of a kanji (Noto Sans JP's proportion), or the font's
// line height if that is tighter. Scales spec.lineCompression and sets spec.glyphExtentScale.
void applyScriptLineSpacing(ReaderRenderSpec& spec, const GfxRenderer& renderer, LineScript script);
