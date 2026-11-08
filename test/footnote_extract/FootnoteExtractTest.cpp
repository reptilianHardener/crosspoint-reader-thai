// Host tests for FootnoteExtractor, which finds a link target's note text for the
// reader's footnote popup.

#include <gtest/gtest.h>

#include <string>

#include "FootnoteExtract.h"
#include "HtmlToPlainText.h"

namespace {

struct Extracted {
  FootnoteExtractor::Result result;
  std::string html;
  std::string text;
  bool truncated;
};

std::string plain(const std::string& html) {
  std::string text = htmlToPlainText(html, '\x01');
  const size_t first = text.find_first_not_of(" \n");
  if (first == std::string::npos) return "";
  return text.substr(first, text.find_last_not_of(" \n") - first + 1);
}

// Feeds the file in chunks of `chunk` bytes, as the zip stream does.
Extracted extract(const std::string& file, const std::string& anchor, const size_t chunk = 7) {
  FootnoteExtractor extractor(anchor);
  for (size_t i = 0; i < file.size(); i += chunk) {
    const size_t n = std::min(chunk, file.size() - i);
    if (!extractor.feed(reinterpret_cast<const uint8_t*>(file.data() + i), n)) break;
  }
  const auto result = extractor.result();
  const bool truncated = extractor.truncated();
  const std::string html = result == FootnoteExtractor::Result::Note ? extractor.takeHtml() : "";
  return {result, html, plain(html), truncated};
}

const std::string kSecond = plain("<p>Second note,</p><p>two paragraphs.</p>");

const std::string kNotes =
    "<?xml version=\"1.0\"?><html><body>"
    "<h1 id=\"notes\">Notes</h1>"
    "<!-- a comment with <p id=\"fn9\">bait</p> inside -->"
    "<p class=\"note\"><a id=\"fn1\" href=\"ch1.xhtml#r1\">1</a> First note &amp; more.</p>"
    "<aside epub:type=\"footnote\" id=\"fn2\"><p>Second note,</p><p>two paragraphs.</p></aside>"
    "<div id='fn3' title=\"a > b\"><span>Third</span> note</div>"
    "<p><a name=\"fn4\"/>Fourth note.</p>"
    "<section id=\"ch2\"><h2>Chapter Two</h2><p>Body</p></section>"
    "<div id=\"part\"><h2>Part</h2><p>Body</p></div>"
    "<section epub:type=\"endnotes\" id=\"en\"><p>Marked endnotes</p></section>"
    "</body></html>";

TEST(FootnoteExtract, InlineAnchorKeepsItsParagraph) {
  const auto e = extract(kNotes, "fn1");
  EXPECT_EQ(e.result, FootnoteExtractor::Result::Note);
  EXPECT_EQ(e.text, "1 First note & more.");
  // Balanced, with tag names only, for the strict chapter parser.
  EXPECT_EQ(e.html, "<p><a>1</a> First note &amp; more.</p>");
}

TEST(FootnoteExtract, AnchorInsideSuperscriptKeepsTheParagraph) {
  const std::string file = "<body><p class=\"fn\"><sup><a id=\"fn1\">1</a></sup> The note text.</p></body>";
  const auto e = extract(file, "fn1");
  EXPECT_EQ(e.text, "1 The note text.");
  EXPECT_EQ(e.html, "<p><sup><a>1</a></sup> The note text.</p>");
}

TEST(FootnoteExtract, AnchorLooseInTheBodyIsAChapterStart) {
  const std::string file = "<body><a id=\"filepos12\"></a><p>Chapter text</p></body>";
  EXPECT_EQ(extract(file, "filepos12").result, FootnoteExtractor::Result::NotANote);
}

TEST(FootnoteExtract, BackLinkToTheNoteReferenceIsNotANote) {
  const std::string file = "<body><p>Text<a id=\"ref1\" href=\"#fn1\">1</a> more.</p></body>";
  EXPECT_EQ(extract(file, "ref1").result, FootnoteExtractor::Result::NotANote);
  const std::string typed = "<body><p>Text<span id=\"ref2\" epub:type=\"noteref\">2</span></p></body>";
  EXPECT_EQ(extract(typed, "ref2").result, FootnoteExtractor::Result::NotANote);
}

TEST(FootnoteExtract, PunctuationBeforeABackLinkedNumberIsStillANote) {
  const std::string bracket = "<body><p class=\"note\">[<a id=\"fn1\" href=\"#r1\">1</a>] Text</p></body>";
  // The note starts at its anchor, so text before it in the paragraph is not kept.
  EXPECT_EQ(extract(bracket, "fn1").text, "1] Text");
  const std::string nbsp = "<body><p>&#160;<a id=\"fn2\" href=\"#r2\">2</a> Text</p></body>";
  EXPECT_EQ(extract(nbsp, "fn2").result, FootnoteExtractor::Result::Note);
  const std::string raw = "<body><p>\u00a0<a id=\"fn3\" href=\"#r3\">3</a> Text</p></body>";
  EXPECT_EQ(extract(raw, "fn3").result, FootnoteExtractor::Result::Note);
}

TEST(FootnoteExtract, PageBreakMarkerIsNotANote) {
  const std::string file = "<body><p>Text <span epub:type=\"pagebreak\" id=\"page_12\"/>more</p></body>";
  EXPECT_EQ(extract(file, "page_12").result, FootnoteExtractor::Result::NotANote);
}

TEST(FootnoteExtract, EmptyMarkerInADivIsAChapterStart) {
  const std::string file = "<body><div class=\"calibre\"><a id=\"filepos9\"/><p>Chapter</p></div></body>";
  EXPECT_EQ(extract(file, "filepos9").result, FootnoteExtractor::Result::NotANote);
}

TEST(FootnoteExtract, MetaNameDoesNotMatch) {
  const std::string file =
      "<html><head><meta name=\"author\" content=\"x\"/><title>T</title></head>"
      "<body><p id=\"author\">About the author</p></body></html>";
  EXPECT_EQ(extract(file, "author").text, "About the author");
}

TEST(FootnoteExtract, BlockTargetKeepsAllItsParagraphs) {
  const auto e = extract(kNotes, "fn2");
  EXPECT_EQ(e.result, FootnoteExtractor::Result::Note);
  EXPECT_EQ(e.text, kSecond);
}

TEST(FootnoteExtract, QuotedGreaterThanAndSingleQuotesAreHandled) {
  EXPECT_EQ(extract(kNotes, "fn3").text, "Third note");
}

TEST(FootnoteExtract, SelfClosingNameAnchorKeepsItsParagraph) {
  EXPECT_EQ(extract(kNotes, "fn4").text, "Fourth note.");
}

TEST(FootnoteExtract, CommentsAreSkipped) {
  EXPECT_EQ(extract(kNotes, "fn9").result, FootnoteExtractor::Result::NotFound);
}

TEST(FootnoteExtract, ChapterTargetsAreNotNotes) {
  EXPECT_EQ(extract(kNotes, "notes").result, FootnoteExtractor::Result::NotANote);
  EXPECT_EQ(extract(kNotes, "ch2").result, FootnoteExtractor::Result::NotANote);
  EXPECT_EQ(extract(kNotes, "part").result, FootnoteExtractor::Result::NotANote);
}

TEST(FootnoteExtract, MarkedNoteSectionIsANote) { EXPECT_EQ(extract(kNotes, "en").text, "Marked endnotes"); }

TEST(FootnoteExtract, MissingAnchorIsNotFound) {
  EXPECT_EQ(extract(kNotes, "nope").result, FootnoteExtractor::Result::NotFound);
  EXPECT_EQ(extract(kNotes, "fn").result, FootnoteExtractor::Result::NotFound);
}

TEST(FootnoteExtract, EveryChunkSizeGivesTheSameText) {
  for (size_t chunk : {1u, 2u, 3u, 64u, 4096u}) {
    EXPECT_EQ(extract(kNotes, "fn2", chunk).text, kSecond) << chunk;
  }
}

TEST(FootnoteExtract, ThaiTextSurvives) {
  const std::string file = "<body><p id=\"n1\">หมายเหตุ ภาษาไทย</p></body>";
  EXPECT_EQ(extract(file, "n1").text, "หมายเหตุ ภาษาไทย");
}

TEST(FootnoteExtract, LongNoteIsCutAndMarked) {
  const std::string file = "<body><p id=\"n1\">" + std::string(10000, 'x') + "</p></body>";
  const auto e = extract(file, "n1");
  EXPECT_EQ(e.result, FootnoteExtractor::Result::Note);
  EXPECT_TRUE(e.truncated);
  EXPECT_LE(e.html.size(), FootnoteExtractor::MAX_HTML_BYTES + 32);
  EXPECT_EQ(e.html.substr(e.html.size() - 7), "\u2026</p>");
}

TEST(FootnoteExtract, CutThaiNoteEndsOnAWholeCharacterAndClosesItsTags) {
  std::string thai;
  while (thai.size() < 6000) thai += "ก";
  const std::string file = "<body><div id=\"n1\"><p><i>" + thai + "</i></p></div></body>";
  const auto e = extract(file, "n1");
  ASSERT_TRUE(e.truncated);
  const std::string tail = "\u2026</i></p></div>";
  ASSERT_GE(e.html.size(), tail.size());
  EXPECT_EQ(e.html.substr(e.html.size() - tail.size()), tail);
  // Every character before the ellipsis is a whole three-byte Thai letter.
  const std::string body = e.html.substr(std::string("<div><p><i>").size());
  EXPECT_EQ(body.find("\u2026") % 3, 0u);
}

TEST(FootnoteExtract, CutInsideATagDropsTheWholeTag) {
  std::string file = "<body><p id=\"n1\">";
  for (int i = 0; i < 800; ++i) file += "<b>x</b>";
  file += "</p></body>";
  const auto e = extract(file, "n1");
  ASSERT_TRUE(e.truncated);
  size_t open = 0, close = 0;
  for (size_t p = 0; (p = e.html.find("<b>", p)) != std::string::npos; ++p) ++open;
  for (size_t p = 0; (p = e.html.find("</b>", p)) != std::string::npos; ++p) ++close;
  EXPECT_EQ(open, close);
  EXPECT_EQ(e.html.substr(e.html.size() - 4), "</p>");
}

TEST(FootnoteExtract, StopsReadingOnceTheNoteEnds) {
  FootnoteExtractor extractor("n1");
  const std::string head = "<body><p id=\"n1\">done</p>";
  EXPECT_FALSE(extractor.feed(reinterpret_cast<const uint8_t*>(head.data()), head.size()));
}

}  // namespace
