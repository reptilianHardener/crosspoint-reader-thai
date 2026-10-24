#include <Epub/ParsedText.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

struct Line {
  std::vector<std::string> words;
  std::vector<int16_t> xpos;
};

struct Word {
  const char* text;
  bool attachToPrevious = false;
  bool zeroWidthBreakBefore = false;
};

// Fixture metrics (stub renderer): every base glyph is 8 px wide, a space is 4 px, and
// combining marks (Thai vowels above/below and tone marks included) have no width.
std::vector<Line> layout(const std::vector<Word>& words, const bool hyphenation, const uint16_t width,
                         const CssTextAlign alignment = CssTextAlign::Left) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = alignment;
  style.textIndentDefined = true;
  ParsedText text(hyphenation, false, style, /*paragraphIndentSpaces=*/0);
  for (const auto& word : words) {
    text.addWord(word.text, EpdFontFamily::REGULAR, false, word.attachToPrevious, 0, 0, 0, false,
                 word.zeroWidthBreakBefore);
  }
  std::vector<Line> lines;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::unique_ptr<TextBlock> block, auto) {
    auto& line = lines.emplace_back();
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      line.words.emplace_back(block->wordText(i));
      line.xpos.push_back(block->wordXpos(i));
    }
  });
  return lines;
}

std::vector<std::vector<std::string>> wordsOf(const std::vector<Line>& lines) {
  std::vector<std::vector<std::string>> result;
  result.reserve(lines.size());
  for (const auto& line : lines) result.push_back(line.words);
  return result;
}

}  // namespace

// ใน|การ|ขับเคลื่อน|แทน: base-glyph widths 16, 24, 56 and 24 px (marks are zero-width).

TEST(ThaiLineBreaking, RunIsSplitIntoGaplessDictionaryWords) {
  Hyphenator::setPreferredLanguage("th");
  const auto lines = layout({{"ในการขับเคลื่อนแทน"}}, false, 400);
  const std::vector<std::vector<std::string>> expected{{"ใน", "การ", "ขับเคลื่อน", "แทน"}};
  ASSERT_EQ(wordsOf(lines), expected);
  // No space between words: each starts where the previous one ended.
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 16, 40, 96}));
}

TEST(ThaiLineBreaking, WrapsBetweenWordsNotInsideThem) {
  Hyphenator::setPreferredLanguage("th");
  // 60 px holds ใน + การ (40 px) but not ขับเคลื่อน as well; the long word moves down whole.
  for (const bool hyphenation : {false, true}) {
    const auto lines = layout({{"ในการขับเคลื่อนแทน"}}, hyphenation, 60);
    const std::vector<std::vector<std::string>> expected{{"ใน", "การ"}, {"ขับเคลื่อน"}, {"แทน"}};
    EXPECT_EQ(wordsOf(lines), expected) << "hyphenation=" << hyphenation;
  }
}

TEST(ThaiLineBreaking, RealSpacesBetweenThaiWordsAreKept) {
  Hyphenator::setPreferredLanguage("th");
  const auto lines = layout({{"ใน"}, {"การ"}}, false, 400);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 20}));  // 16 px word + 4 px space
}

TEST(ThaiLineBreaking, JustificationStretchesWordBoundaries) {
  Hyphenator::setPreferredLanguage("th");
  // First line ใน + การ is 40 px in a 60 px measure: the single boundary takes the 20 px.
  const auto lines = layout({{"ในการขับเคลื่อนแทน"}}, false, 60, CssTextAlign::Justify);
  ASSERT_GE(lines.size(), 2u);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 36}));
}

TEST(ThaiLineBreaking, ZeroWidthSpaceIsABreakOpportunityWithoutAGap) {
  Hyphenator::setPreferredLanguage("en");
  // "abcd" ZWSP "efgh": flush on one line, and allowed to wrap between the two.
  const std::vector<Word> words{{"abcd"}, {"efgh", false, true}};
  const auto wide = layout(words, false, 400);
  ASSERT_EQ(wide.size(), 1u);
  EXPECT_EQ(wide[0].xpos, (std::vector<int16_t>{0, 32}));

  const auto narrow = layout(words, false, 40);
  const std::vector<std::vector<std::string>> expected{{"abcd"}, {"efgh"}};
  EXPECT_EQ(wordsOf(narrow), expected);
}

TEST(ThaiLineBreaking, LatinTextIsUnaffected) {
  Hyphenator::setPreferredLanguage("en");
  const auto lines = layout({{"abc"}, {"def"}}, false, 400);
  const std::vector<std::vector<std::string>> expected{{"abc", "def"}};
  ASSERT_EQ(wordsOf(lines), expected);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 28}));
}
