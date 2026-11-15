#include <Epub/ParsedText.h>
#include <Epub/blocks/TextBlock.h>
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

struct Options {
  bool vertical = true;
  bool hyphenation = false;  // selects the greedy line breaker
  CssTextAlign alignment = CssTextAlign::Left;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  uint8_t indentSpaces = 0;
};

// Fixture metrics (stub renderer): every glyph is 8 px wide, so the cell of a comma or full
// stop that ends a line may pass the line end by 4 px. A space is 4 px.
std::vector<Line> layout(const std::vector<const char*>& gluedWords, const uint16_t width,
                         const Options& options = {}) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = options.alignment;
  style.textIndentDefined = true;
  ParsedText paragraph(options.hyphenation, false, style, options.indentSpaces);
  paragraph.setVerticalText(options.vertical);
  bool first = true;
  for (const char* word : gluedWords) {
    paragraph.addWord(word, options.style, false, /*attachToPrevious=*/!first);
    first = false;
  }
  std::vector<Line> lines;
  paragraph.layoutAndExtractLines(renderer, 0, width, [&](std::unique_ptr<TextBlock> block, auto) {
    auto& line = lines.emplace_back();
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      line.words.emplace_back(block->wordText(i));
      line.xpos.push_back(block->wordXpos(i));
    }
  });
  return lines;
}

std::vector<Line> layout(const char* text, const uint16_t width, const Options& options = {}) {
  return layout(std::vector<const char*>{text}, width, options);
}

using Words = std::vector<std::string>;

}  // namespace

// あいうえ is 32 px and お、 is 16 px: 48 px in all, 44 px without the comma's empty half.

TEST(VerticalLineEndMarks, CommaAtTheLineEndGivesUpHalfItsCell) {
  for (const bool hyphenation : {false, true}) {
    Options options;
    options.hyphenation = hyphenation;
    const auto lines = layout("あいうえお、かき", 44, options);
    ASSERT_EQ(lines.size(), 2u) << "hyphenation=" << hyphenation;
    EXPECT_EQ(lines[0].words, (Words{"あ", "い", "う", "え", "お、"})) << "hyphenation=" << hyphenation;
    EXPECT_EQ(lines[1].words, (Words{"か", "き"})) << "hyphenation=" << hyphenation;
  }
}

TEST(VerticalLineEndMarks, FullStopIsSetLikeTheComma) {
  const auto lines = layout("あいうえお。かき", 44);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].words.back(), "お。");
}

TEST(VerticalLineEndMarks, MoreThanHalfACellDoesNotFit) {
  // 43 px: the comma itself would cross the line end.
  const auto lines = layout("あいうえお、かき", 43);
  ASSERT_GE(lines.size(), 2u);
  EXPECT_EQ(lines[0].words, (Words{"あ", "い", "う", "え"}));
}

TEST(VerticalLineEndMarks, HorizontalPagesAreUnchanged) {
  Options options;
  options.vertical = false;
  for (const bool hyphenation : {false, true}) {
    options.hyphenation = hyphenation;
    const auto lines = layout("あいうえお、かき", 44, options);
    ASSERT_GE(lines.size(), 2u);
    EXPECT_EQ(lines[0].words, (Words{"あ", "い", "う", "え"})) << "hyphenation=" << hyphenation;
  }
}

TEST(VerticalLineEndMarks, OtherLineEndPunctuationKeepsItsCell) {
  // Only the comma and full stop are handled; a closing bracket keeps its whole cell.
  const auto lines = layout("あいうえお」かき", 44);
  ASSERT_GE(lines.size(), 2u);
  EXPECT_EQ(lines[0].words, (Words{"あ", "い", "う", "え"}));
}

TEST(VerticalLineEndMarks, SuperscriptMarksKeepTheirCell) {
  Options options;
  options.style = EpdFontFamily::SUP;
  const auto lines = layout("あいうえお、かき", 44, options);
  ASSERT_GE(lines.size(), 2u);
  EXPECT_EQ(lines[0].words, (Words{"あ", "い", "う", "え"}));
}

TEST(VerticalLineEndMarks, JustifiedLineIsSpacedOutToTheLineEndMark) {
  // あいう (24 px) + え、 (16 px) = 40 px on a 44 px line, then a second line.
  Options options;
  options.alignment = CssTextAlign::Justify;
  const auto marked = layout("あいうえ、かきくけこ", 44, options);
  ASSERT_EQ(marked.size(), 2u);
  ASSERT_EQ(marked[0].words, (Words{"あ", "い", "う", "え、"}));
  // The line is set to 48 px: 8 px of slack over three gaps, 2 px each (the rest is dropped).
  EXPECT_EQ(marked[0].xpos, (std::vector<int16_t>{0, 10, 20, 30}));

  options.vertical = false;
  const auto plain = layout("あいうえ、かきくけこ", 44, options);
  ASSERT_EQ(plain[0].words, (Words{"あ", "い", "う", "え、"}));
  EXPECT_EQ(plain[0].xpos, (std::vector<int16_t>{0, 9, 18, 27}));
}

TEST(VerticalLineEndMarks, MarkGluedToAClosingBracketKeepsItsCell) {
  // The bracket arrives as its own glued word (a separate inline style): the comma no
  // longer ends the line, so its whole cell counts and お、」 moves down together.
  for (const bool hyphenation : {false, true}) {
    Options options;
    options.hyphenation = hyphenation;
    const auto lines = layout({"あいうえお、", "」かき"}, 44, options);
    ASSERT_GE(lines.size(), 2u) << "hyphenation=" << hyphenation;
    EXPECT_EQ(lines[0].words, (Words{"あ", "い", "う", "え"})) << "hyphenation=" << hyphenation;
  }
}

TEST(VerticalLineEndMarks, RightAndCenteredLinesFollowTheLongerLine) {
  // あいう、 is 32 px. On a 48 px line set 4 px longer: right-aligned it starts at 20,
  // centered at 10.
  Options options;
  options.alignment = CssTextAlign::Right;
  const auto right = layout("あいう、", 48, options);
  ASSERT_EQ(right.size(), 1u);
  EXPECT_EQ(right[0].xpos.front(), 20);

  options.alignment = CssTextAlign::Center;
  const auto centered = layout("あいう、", 48, options);
  ASSERT_EQ(centered.size(), 1u);
  EXPECT_EQ(centered[0].xpos.front(), 10);
}

TEST(VerticalLineEndMarks, FirstLineIndentStillCounts) {
  // Two indent spaces take 8 px of the first line: 8 + 32 + 16 = 56 px, 52 px without the
  // comma's blank half.
  Options options;
  options.indentSpaces = 2;
  const auto fits = layout("あいうえお、かき", 52, options);
  ASSERT_EQ(fits.size(), 2u);
  EXPECT_EQ(fits[0].words.back(), "お、");
  EXPECT_EQ(fits[0].xpos.front(), 8);

  const auto tooNarrow = layout("あいうえお、かき", 51, options);
  ASSERT_GE(tooNarrow.size(), 2u);
  EXPECT_EQ(tooNarrow[0].words.back(), "え");
}

TEST(VerticalLineEndMarks, BoxesOverTheLastWordStopAtTheMark) {
  GfxRenderer renderer;
  BlockStyle style;
  style.textIndentDefined = true;
  ParsedText paragraph(false, false, style, 0);
  paragraph.setVerticalText(true);
  paragraph.addWord("あい、", EpdFontFamily::REGULAR);
  bool checked = false;
  paragraph.layoutAndExtractLines(renderer, 0, 400, [&](std::unique_ptr<TextBlock> block, auto) {
    const uint16_t last = block->wordCount() - 1;
    EXPECT_EQ(block->wordLineEndTrim(renderer, last, 0, /*vertical=*/true), 4);
    EXPECT_EQ(block->wordLineEndTrim(renderer, last, 0, /*vertical=*/false), 0);
    EXPECT_EQ(block->wordLineEndTrim(renderer, 0, 0, /*vertical=*/true), 0);
    checked = true;
  });
  EXPECT_TRUE(checked);
}
