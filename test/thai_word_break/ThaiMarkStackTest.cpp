#include <gtest/gtest.h>

#include "lib/EpdFont/EpdFontData.h"
#include "lib/EpdFont/ThaiMarkStack.h"

// ============================================================================
// Thai marks without GPOS mark-to-mark positioning.
//
// Coordinates are pixels above the baseline (EpdGlyph::top convention): a mark
// with top=20, height=4 occupies [16, 20). LINE_ADVANCE 30 gives a 3 px gap
// between stacked tiers (lineAdvanceY / 10).
// ============================================================================

namespace {
constexpr int LINE_ADVANCE = 30;
constexpr uint32_t SARA_I = 0x0E34;   // level two: upper vowel
constexpr uint32_t MAI_EK = 0x0E48;   // level three: tone mark
constexpr uint32_t SARA_U = 0x0E38;   // below-base vowel
constexpr uint32_t MAI_HAN = 0x0E31;  // level two
constexpr uint32_t THANTHAKHAT = 0x0E4C;
}  // namespace

TEST(ThaiAnchor, ThaiMarksDrawAtThePenPosition) {
  using combiningMark::Anchor;
  using combiningMark::anchorFor;
  for (const uint32_t cp : {MAI_HAN, SARA_I, SARA_U, MAI_EK, THANTHAKHAT, 0x0E4Eu}) {
    EXPECT_EQ(anchorFor(cp), Anchor::PenNative) << std::hex << cp;
  }
  // Base consonants, spacing vowels and sara am are not marks.
  for (const uint32_t cp : {0x0E01u, 0x0E30u, 0x0E32u, 0x0E33u, 0x0E40u}) {
    EXPECT_EQ(anchorFor(cp), Anchor::CenterRaised) << std::hex << cp;
    EXPECT_FALSE(utf8IsCombiningMark(cp)) << std::hex << cp;
  }
  // The generic raise heuristic must leave pen-native marks alone.
  EXPECT_EQ(combiningMark::raiseAboveBase(Anchor::PenNative, 20, 4, 30), 0);
}

TEST(ThaiMarkStack, LevelTwoAndBelowMarksKeepFontHeight) {
  ThaiMarkStack stack;
  // Upper vowel overlapping the base top: by design, it nestles into the consonant.
  EXPECT_EQ(stack.raiseFor(SARA_I, 18, 5, 16, LINE_ADVANCE), 0);
  ThaiMarkStack below;
  EXPECT_EQ(below.raiseFor(SARA_U, -2, 5, 16, LINE_ADVANCE), 0);
  EXPECT_FALSE(below.hasStacked);
}

TEST(ThaiMarkStack, ToneMarkAloneOnlyClearsTheBase) {
  ThaiMarkStack stack;
  // Bottom at 17, base top 16: already clear.
  EXPECT_EQ(stack.raiseFor(MAI_EK, 22, 5, 16, LINE_ADVANCE), 0);

  ThaiMarkStack tall;
  // Bottom at 17 against a tall consonant (top 20): raise to leave a 1 px gap.
  EXPECT_EQ(tall.raiseFor(MAI_EK, 22, 5, 20, LINE_ADVANCE), 4);
}

TEST(ThaiMarkStack, ToneMarkIsRaisedAboveAnUpperVowel) {
  ThaiMarkStack stack;
  EXPECT_EQ(stack.raiseFor(SARA_I, 21, 5, 16, LINE_ADVANCE), 0);  // occupies [16, 21)
  // Tone mark natively at [18, 23) would sit inside the vowel; it must start at 21 + 3.
  EXPECT_EQ(stack.raiseFor(MAI_EK, 23, 5, 16, LINE_ADVANCE), 6);
}

TEST(ThaiMarkStack, ToneMarkAlreadyClearOfTheVowelIsNotMoved) {
  ThaiMarkStack stack;
  EXPECT_EQ(stack.raiseFor(SARA_I, 21, 5, 16, LINE_ADVANCE), 0);
  EXPECT_EQ(stack.raiseFor(MAI_EK, 30, 5, 16, LINE_ADVANCE), 0);  // bottom 25 >= 24
}

TEST(ThaiMarkStack, NewBaseStartsAFreshStack) {
  ThaiMarkStack stack;
  stack.raiseFor(SARA_I, 21, 5, 16, LINE_ADVANCE);
  stack.resetForBase();
  EXPECT_EQ(stack.raiseFor(MAI_EK, 23, 5, 16, LINE_ADVANCE), 0);
}

TEST(ThaiMarkStack, ToneMarkBeforeSaraAmClearsItsRing) {
  ThaiMarkStack stack;
  stack.seedFromSaraAm(22);  // nikhahit ring of the following sara am tops out at 22
  EXPECT_EQ(stack.raiseFor(MAI_EK, 23, 5, 16, LINE_ADVANCE), 7);  // bottom 18 -> 25
}
