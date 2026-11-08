#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

// Finds the element an internal link points at in a streamed XHTML file and keeps a
// copy of its HTML, so the reader can show a footnote in a popup instead of jumping.
//
// The target is the element whose id (or name) equals the link's fragment. When the id
// sits on an inline element, such as <a id="fn1"> at the start of a note paragraph, the
// enclosing element is kept instead. A heading, a whole section, an anchor loose in one,
// a back-link (an element with href or a noteref), or anything holding a heading is not a
// note, unless it is marked as one (epub:type or role naming a footnote or endnote).
class FootnoteExtractor {
 public:
  enum class Result : uint8_t { NotFound, Note, NotANote };

  // Bound on the kept HTML; a longer note is cut, ended with an ellipsis and marked truncated.
  static constexpr size_t MAX_HTML_BYTES = 4096;

  explicit FootnoteExtractor(std::string anchor) : anchor(std::move(anchor)) {}

  // Feeds the next bytes of the file. Returns false once no more input is needed.
  bool feed(const uint8_t* data, size_t size);

  Result result() const;
  bool truncated() const { return cut; }
  // The kept HTML, tag names only, for the chapter parser; valid when result() is Note.
  std::string takeHtml() { return std::move(html); }

 private:
  enum class Mode : uint8_t { Text, Tag, Comment, Done };

  static constexpr size_t MAX_TAG_BYTES = 256;
  static constexpr size_t MAX_DEPTH = 48;
  static constexpr size_t NAME_BYTES = 12;

  void processTag();
  void startTag(const char* name, bool selfClosing);
  void endTag(const char* name);
  void startCapture();
  void appendTagName();
  void finishCut();
  void noteText(char c);
  void append(char c);

  const std::string anchor;
  std::string html;
  Mode mode = Mode::Text;
  bool found = false;
  bool notANote = false;
  bool cut = false;
  bool capturing = false;
  bool markedNote = false;
  bool appendingTag = false;
  bool textInBlock = false;  // text seen since the innermost block element opened
  bool inEntity = false;
  bool afterC2 = false;
  size_t tagStart = 0;  // where the tag being copied starts in html
  // The capture ends when the open-element depth falls back to this.
  size_t captureDepth = 0;

  char tag[MAX_TAG_BYTES + 1] = {};
  size_t tagLen = 0;
  char quote = 0;
  // Last two bytes seen inside a comment, to find "-->".
  char commentTail[2] = {};

  char stack[MAX_DEPTH][NAME_BYTES] = {};
  size_t depth = 0;  // may exceed MAX_DEPTH; names past it are not kept
};
