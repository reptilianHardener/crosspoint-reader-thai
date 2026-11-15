#include <Epub/ParsedText.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

struct Word {
  const char* text;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  bool attachToPrevious = false;
};

struct Line {
  std::vector<std::string> words;
  std::vector<int16_t> xpos;
  std::vector<bool> combined;
  std::vector<uint32_t> sourceStarts;
  std::vector<std::string> combinedDraws;
};

// Fixture metrics (stub renderer): every glyph is 8 px wide and the font's em, the length of
// one upright cell, is 16 px unless emPixels says otherwise.
BlockStyle leftAligned() {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textIndentDefined = true;
  return style;
}

// Appends the words of each emitted line to `line`, so a paragraph may span lines and calls.
auto collectInto(Line& line, GfxRenderer& renderer) {
  return [&line, &renderer](std::unique_ptr<TextBlock> block, auto) {
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      line.words.emplace_back(block->wordText(i));
      line.xpos.push_back(block->wordXpos(i));
      line.combined.push_back(block->wordIsCombinedUpright(i));
      line.sourceStarts.push_back(block->wordSourceRange(i).start);
    }
    block->render(renderer, 0, 100, 0, /*vertical=*/true);
    line.combinedDraws = renderer.combinedUprightDraws;
  };
}

Line layout(const std::vector<Word>& words, const bool vertical = true, const float emPixels = 16.0f,
            const uint32_t firstOffset = 0, const int8_t characterSpacing = 0) {
  GfxRenderer renderer;
  renderer.fontSizePixels[0] = emPixels;
  ParsedText text(false, false, leftAligned(), /*paragraphIndentSpaces=*/0);
  text.setVerticalText(vertical);
  for (const auto& word : words) {
    text.addWord(word.text, word.style, false, word.attachToPrevious, firstOffset);
  }
  Line line;
  text.layoutAndExtractLines(renderer, 0, 400, collectInto(line, renderer), true, characterSpacing);
  return line;
}

using Words = std::vector<std::string>;
using Flags = std::vector<bool>;

}  // namespace

TEST(VerticalUprightRuns, TwoDigitsBetweenKanjiTakeOneCell) {
  const Line line = layout({{"平成25年"}}, true, 16.0f, /*firstOffset=*/10);
  ASSERT_EQ(line.words, (Words{"平", "成", "25", "年"}));
  EXPECT_EQ(line.combined, (Flags{false, false, true, false}));
  // The two digits measure 16 px and their cell is 16 px: the kanji after them starts at 32.
  EXPECT_EQ(line.xpos, (std::vector<int16_t>{0, 8, 16, 32}));
  EXPECT_EQ(line.sourceStarts, (std::vector<uint32_t>{10, 11, 12, 14}));
  EXPECT_EQ(line.combinedDraws, (Words{"25"}));
}

TEST(VerticalUprightRuns, OneDigitTakesAWholeCell) {
  const Line line = layout({{"第3章"}});
  ASSERT_EQ(line.words, (Words{"第", "3", "章"}));
  EXPECT_EQ(line.combined, (Flags{false, true, false}));
  EXPECT_EQ(line.xpos, (std::vector<int16_t>{0, 8, 24}));
}

TEST(VerticalUprightRuns, SeveralRunsInOneWord) {
  const Line line = layout({{"12月25日"}});
  ASSERT_EQ(line.words, (Words{"12", "月", "25", "日"}));
  EXPECT_EQ(line.combined, (Flags{true, false, true, false}));
}

TEST(VerticalUprightRuns, StaysGluedToBracketsAndCommas) {
  const Line line = layout({{"「12、3」"}});
  ASSERT_EQ(line.words, (Words{"「", "12", "、", "3", "」"}));
  EXPECT_EQ(line.combined, (Flags{false, true, false, true, false}));
  // Glued tokens add no gap: bracket 8, cell 16, comma 8, cell 16.
  EXPECT_EQ(line.xpos, (std::vector<int16_t>{0, 8, 24, 32, 48}));
}

TEST(VerticalUprightRuns, UprightSymbolsCountAsNeighbors) {
  // The reference mark is not CJK, but it stands upright in a vertical line.
  const Line line =
      layout({{"\xe2\x80\xbb"
               "1"}});
  ASSERT_EQ(line.words.size(), 2u);
  EXPECT_EQ(line.words[1], "1");
  EXPECT_EQ(line.combined, (Flags{false, true}));
}

TEST(VerticalUprightRuns, DashesLeadersAndBulletsCountAsNeighbors) {
  // U+2015 horizontal bar, U+2026 ellipsis, U+2022 bullet.
  for (const char* word : {"\xe2\x80\x95\xe2\x80\x95"
                           "12歳",
                           "\xe2\x80\xa6"
                           "3日後"}) {
    const Line line = layout({{word}});
    ASSERT_GE(line.words.size(), 2u) << word;
    EXPECT_TRUE(line.combined[1]) << word;
  }
  const Line listItem = layout({{"\xe2\x80\xa2"}, {"12月"}});
  ASSERT_EQ(listItem.words, (Words{"\xe2\x80\xa2", "12", "月"}));
  EXPECT_EQ(listItem.combined, (Flags{false, true, false}));
}

TEST(VerticalUprightRuns, LongNumbersStaySideways) {
  const Line line = layout({{"2024年"}});
  ASSERT_EQ(line.words, (Words{"2024", "年"}));
  EXPECT_EQ(line.combined, (Flags{false, false}));
  EXPECT_TRUE(line.combinedDraws.empty());
}

TEST(VerticalUprightRuns, DigitsBesideAsciiStaySideways) {
  for (const char* word : {"3.5倍", "10-20年", "A12型", "25%増"}) {
    const Line line = layout({{word}});
    for (size_t i = 0; i < line.words.size(); ++i) {
      EXPECT_FALSE(line.combined[i]) << word << " token " << line.words[i];
    }
  }
}

TEST(VerticalUprightRuns, DigitsBesideLatinWordsStaySideways) {
  const Line after = layout({{"iPhone"}, {"15"}, {"を"}});
  ASSERT_EQ(after.words, (Words{"iPhone", "15", "を"}));
  EXPECT_EQ(after.combined, (Flags{false, false, false}));

  const Line before = layout({{"は"}, {"12"}, {"monkeys"}});
  ASSERT_EQ(before.words, (Words{"は", "12", "monkeys"}));
  EXPECT_EQ(before.combined, (Flags{false, false, false}));
}

TEST(VerticalUprightRuns, DigitsBetweenSpacedKanjiAndAloneAreUpright) {
  const Line spaced = layout({{"第"}, {"12"}, {"章"}});
  EXPECT_EQ(spaced.combined, (Flags{false, true, false}));

  const Line alone = layout({{"7"}});
  EXPECT_EQ(alone.combined, (Flags{true}));
}

TEST(VerticalUprightRuns, SuperscriptNoteMarksStaySmall) {
  const Line line = layout({{"注"}, {"1", EpdFontFamily::SUP, /*attachToPrevious=*/true}});
  ASSERT_EQ(line.words, (Words{"注", "1"}));
  EXPECT_EQ(line.combined, (Flags{false, false}));
}

TEST(VerticalUprightRuns, DigitsMuchWiderThanTheCellStaySideways) {
  // An 8 px em: two digits (16 px) would spill far out of the line, one digit still fits.
  const Line line = layout({{"12月3日"}}, true, 8.0f);
  ASSERT_EQ(line.words, (Words{"12", "月", "3", "日"}));
  EXPECT_EQ(line.combined, (Flags{false, false, true, false}));
  EXPECT_EQ(line.xpos, (std::vector<int16_t>{0, 16, 24, 32}));
}

TEST(VerticalUprightRuns, HorizontalPagesAreUnchanged) {
  const Line line = layout({{"平成25年"}}, /*vertical=*/false);
  ASSERT_EQ(line.words, (Words{"平", "成", "25", "年"}));
  EXPECT_EQ(line.combined, (Flags{false, false, false, false}));
  EXPECT_TRUE(line.combinedDraws.empty());
}

TEST(VerticalUprightRuns, CharacterSpacingDoesNotWidenTheRun) {
  // A 13 px em allows a run up to 16.25 px. Two digits measure 16 px; the 2 px of character
  // spacing between them is not drawn inside the cell and must not count.
  const Line line = layout({{"12月"}}, true, 13.0f, 0, /*characterSpacing=*/2);
  ASSERT_EQ(line.words, (Words{"12", "月"}));
  EXPECT_EQ(line.combined, (Flags{true, false}));
}

TEST(VerticalUprightRuns, RubyOverADigitCellDoesNotShiftTheLine) {
  // The digit measures 8 px but its cell is 16 px, as wide as the two ruby glyphs.
  GfxRenderer renderer;
  renderer.fontSizePixels[0] = 16.0f;
  ParsedText text(false, false, leftAligned(), 0);
  text.setVerticalText(true);
  text.addWord("3日", EpdFontFamily::REGULAR);
  text.setRubyForWordAt(0, "みつ");
  Line line;
  text.layoutAndExtractLines(renderer, 0, 400, collectInto(line, renderer));
  ASSERT_EQ(line.words, (Words{"3", "日"}));
  EXPECT_EQ(line.combined, (Flags{true, false}));
  EXPECT_EQ(line.xpos, (std::vector<int16_t>{0, 16}));
}

TEST(VerticalUprightRuns, RunAtTheEndOfAPartialLayoutIsJudgedAgain) {
  GfxRenderer renderer;
  renderer.fontSizePixels[0] = 16.0f;
  ParsedText text(false, false, leftAligned(), 0);
  text.setVerticalText(true);
  text.addWord("第", EpdFontFamily::REGULAR);
  text.addWord("12", EpdFontFamily::REGULAR);
  Line line;
  // The paragraph is not finished: its last line is held back, with "12" still marked.
  text.layoutAndExtractLines(renderer, 0, 400, collectInto(line, renderer), /*includeLastLine=*/false);
  EXPECT_TRUE(line.words.empty());
  text.addWord("monkeys", EpdFontFamily::REGULAR);
  text.layoutAndExtractLines(renderer, 0, 400, collectInto(line, renderer));
  ASSERT_EQ(line.words, (Words{"第", "12", "monkeys"}));
  EXPECT_EQ(line.combined, (Flags{false, false, false}));
}

TEST(VerticalUprightRuns, DecisionSurvivesTheWordsBeforeItBeingConsumed) {
  GfxRenderer renderer;
  renderer.fontSizePixels[0] = 16.0f;
  ParsedText text(false, false, leftAligned(), 0);
  text.setVerticalText(true);
  // 30 px lines: "abc" fills the first, so "12" starts the second with nothing before it.
  text.addWord("abc", EpdFontFamily::REGULAR);
  text.addWord("12", EpdFontFamily::REGULAR);
  text.addWord("月", EpdFontFamily::REGULAR, false, /*attachToPrevious=*/true);
  Line line;
  text.layoutAndExtractLines(renderer, 0, 30, collectInto(line, renderer), /*includeLastLine=*/false);
  ASSERT_EQ(line.words, (Words{"abc"}));
  text.layoutAndExtractLines(renderer, 0, 30, collectInto(line, renderer));
  ASSERT_EQ(line.words, (Words{"abc", "12", "月"}));
  // "12" follows a Latin word, so it stays sideways even once that word is gone.
  EXPECT_EQ(line.combined, (Flags{false, false, false}));
}

TEST(VerticalUprightRuns, SingleLatinLetterBesideKanaIsUpright) {
  const Line line = layout({{"Aさん"}});
  ASSERT_EQ(line.words, (Words{"A", "さ", "ん"}));
  EXPECT_EQ(line.combined, (Flags{true, false, false}));
  EXPECT_EQ(line.xpos, (std::vector<int16_t>{0, 16, 24}));

  // U+30FB middle dot between initials.
  const Line initials = layout({{"J\xe3\x83\xbbK"}});
  ASSERT_EQ(initials.words.size(), 3u);
  EXPECT_EQ(initials.combined, (Flags{true, false, true}));
}

TEST(VerticalUprightRuns, LatinWordsAndLettersBesideThemStaySideways) {
  for (const char* word : {"DNA鑑定", "A4判", "3D映画"}) {
    const Line line = layout({{word}});
    for (size_t i = 0; i < line.words.size(); ++i) {
      EXPECT_FALSE(line.combined[i]) << word << " token " << line.words[i];
    }
  }
  const Line sentence = layout({{"は"}, {"A"}, {"cat"}});
  EXPECT_EQ(sentence.combined, (Flags{false, false, false}));
}

TEST(VerticalUprightRuns, ExclamationAndQuestionMarksAreUpright) {
  const Line pair = layout({{"何!?"}});
  ASSERT_EQ(pair.words, (Words{"何", "!?"}));
  EXPECT_EQ(pair.combined, (Flags{false, true}));
  // The pair measures 16 px and takes one 16 px cell.
  EXPECT_EQ(pair.combinedDraws, (Words{"!?"}));

  const Line single = layout({{"すごい!"}});
  ASSERT_EQ(single.words, (Words{"す", "ご", "い", "!"}));
  EXPECT_EQ(single.combined, (Flags{false, false, false, true}));

  const Line three = layout({{"え!!!"}});
  EXPECT_EQ(three.combined, (Flags(three.words.size(), false)));

  const Line latin = layout({{"OK!"}});
  EXPECT_EQ(latin.combined, (Flags(latin.words.size(), false)));
}

TEST(VerticalUprightRuns, MarksAfterAnEllipsisNeverStartALine) {
  // U+2026 twice, then !?. The pair is split off for its cell but must stay with the ellipsis.
  const char* text = "あいう\xe2\x80\xa6\xe2\x80\xa6!?";
  for (const bool hyphenation : {false, true}) {
    GfxRenderer renderer;
    renderer.fontSizePixels[0] = 16.0f;
    ParsedText paragraph(hyphenation, false, leftAligned(), 0);
    paragraph.setVerticalText(true);
    paragraph.addWord(text, EpdFontFamily::REGULAR);
    std::vector<Words> lines;
    // 40 px holds the three kana and the ellipses (5 glyphs), not the 16 px cell after them.
    paragraph.layoutAndExtractLines(renderer, 0, 40, [&](std::unique_ptr<TextBlock> block, auto) {
      auto& line = lines.emplace_back();
      for (uint16_t i = 0; i < block->wordCount(); ++i) line.emplace_back(block->wordText(i));
    });
    ASSERT_GE(lines.size(), 2u) << "hyphenation=" << hyphenation;
    for (const auto& line : lines) {
      EXPECT_NE(line.front(), "!?") << "hyphenation=" << hyphenation;
    }
  }
}

TEST(VerticalUprightRuns, LetterBetweenDashesStaysSideways) {
  // U+2014 em dash on both sides, as in English dialogue.
  const Line english = layout({{"Yes\xe2\x80\x94I\xe2\x80\x94think"}});
  EXPECT_EQ(english.combined, (Flags(english.words.size(), false)));

  // A number after a dash in Japanese prose is still upright.
  const Line japanese =
      layout({{"\xe2\x80\x95\xe2\x80\x95"
               "20年"}});
  ASSERT_GE(japanese.words.size(), 2u);
  EXPECT_TRUE(japanese.combined[1]);
}

TEST(VerticalUprightRuns, FocusReadingLeavesAnUprightLetterRegular) {
  GfxRenderer renderer;
  renderer.fontSizePixels[0] = 16.0f;
  ParsedText paragraph(false, /*focusReadingEnabled=*/true, leftAligned(), 0);
  paragraph.setVerticalText(true);
  paragraph.addWord("Aさん", EpdFontFamily::REGULAR);
  bool checked = false;
  paragraph.layoutAndExtractLines(renderer, 0, 400, [&](std::unique_ptr<TextBlock> block, auto) {
    ASSERT_GE(block->wordCount(), 1);
    EXPECT_STREQ(block->wordText(0), "A");
    EXPECT_TRUE(block->wordIsCombinedUpright(0));
    EXPECT_EQ(block->wordStyle(0) & EpdFontFamily::BOLD, 0);
    checked = true;
  });
  EXPECT_TRUE(checked);
}
