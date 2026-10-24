#pragma once

#include <Utf8.h>

#include <algorithm>
#include <cstdint>

/// Vertical placement of Thai marks above a base consonant.
///
/// Thai stacks up to two marks above a consonant: an upper vowel (level two) and a
/// tone mark (level three) on top of it. Fonts solve this with GPOS mark-to-mark
/// positioning, which the device fonts do not carry, so a tone mark drawn at its
/// font-native height can collide with the vowel beneath it. This tracks the top
/// of what has been stacked on the current base and returns how far to raise the
/// next mark to clear it.
///
/// All values are in pixels relative to the baseline with Y pointing up, matching
/// EpdGlyph::top. One instance lives on the stack of a text run; no heap use.
struct ThaiMarkStack {
  int stackedTop = 0;
  bool hasStacked = false;

  /// Call when a new base glyph is drawn: marks attach to the most recent base.
  void resetForBase() { hasStacked = false; }

  /// Sara am (U+0E33) carries its own nikhahit ring above the base. When a tone mark
  /// is followed by sara am, seed the stack with the ring's top so the tone mark is
  /// raised above it instead of sinking into it.
  void seedFromSaraAm(const int saraAmTop) {
    stackedTop = hasStacked ? std::max(stackedTop, saraAmTop) : saraAmTop;
    hasStacked = true;
  }

  /// Returns how many pixels to raise the mark `cp` above its font-native position.
  /// lineAdvanceY is the font's line advance, used to scale the gap between tiers.
  int raiseFor(const uint32_t cp, const int markTop, const int markHeight, const int baseTop, const int lineAdvanceY) {
    // Below-base marks hang under the baseline; level-two marks are designed to nestle
    // into the consonant's upper strokes. Both keep their font-native height.
    int raiseBy = 0;
    if (utf8IsThaiUpperLevelThreeMark(cp)) {
      constexpr int MIN_BASE_GAP_PX = 1;
      const int baseGap = markTop - markHeight - baseTop;
      if (baseGap < MIN_BASE_GAP_PX) {
        raiseBy = MIN_BASE_GAP_PX - baseGap;
      }
      if (hasStacked) {
        const int minStackGap = std::max(1, lineAdvanceY / 10);
        const int markBottom = markTop - markHeight + raiseBy;
        if (markBottom < stackedTop + minStackGap) {
          raiseBy += stackedTop + minStackGap - markBottom;
        }
      }
    }
    if (utf8IsThaiUpperCombiningMark(cp)) {
      const int raisedTop = markTop + raiseBy;
      stackedTop = hasStacked ? std::max(stackedTop, raisedTop) : raisedTop;
      hasStacked = true;
    }
    return raiseBy;
  }
};
