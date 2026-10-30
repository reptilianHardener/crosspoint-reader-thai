#include "ScriptLineSpacing.h"

#include <EpdFontData.h>
#include <EpdFontFamily.h>
#include <Epub/ReaderRenderSpec.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "ReaderLineSpacing.h"

namespace {
constexpr float JAPANESE_LINE_PITCH_EM = 1.45f;
}  // namespace

void applyScriptLineSpacing(ReaderRenderSpec& spec, const GfxRenderer& renderer, const LineScript script) {
  if (script == LineScript::Other) return;
  const auto fontIt = renderer.getFontMap().find(spec.fontId);
  if (fontIt == renderer.getFontMap().end()) return;
  const float fontLineHeight = static_cast<float>(renderer.getLineHeight(spec.fontId));
  float pitch = fontLineHeight;
  if (script == LineScript::Japanese) {
    int32_t advanceFP = 0;
    int top = 0;
    if (!renderer.getCodepointMetrics(spec.fontId, 0x4E00, EpdFontFamily::REGULAR, advanceFP, top)) return;
    pitch = std::min(pitch, JAPANESE_LINE_PITCH_EM * static_cast<float>(fp4::toPixel(advanceFP)));
  }
  const float defaultPitch =
      fontIt->second.getSizeInPixels() * LINE_SPACING_MULTIPLIERS[CrossPointSettings::LINE_SPACING_120];
  if (pitch <= 0.0f || defaultPitch <= 0.0f) return;
  spec.lineCompression *= pitch / defaultPitch;
  // Kanji and kana fill only the Japanese pitch, not the room a Thai-capable font keeps for marks.
  spec.glyphExtentScale = pitch / fontLineHeight;
}
