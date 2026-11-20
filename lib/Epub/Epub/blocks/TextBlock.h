#pragma once
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Block.h"
#include "BlockStyle.h"
#include "Epub/FootnoteEntry.h"

// Represents a line of text on a page.
//
// All per-word data lives in ONE flat heap allocation (the arena) instead of
// six parallel vectors: a resident page holds ~25-30 of these blocks, and the
// vector-of-string layout cost ~250 throwing allocations per page load, which
// was the primary driver of heap fragmentation on the ESP32-C3.
//
// Arena layout, in order (4-byte alignment holds by construction: source ranges come first, then 16-bit
// arrays and the arena base is allocator-aligned; RISC-V faults on
// unaligned multi-byte access):
//   SourceRange sourceRanges[wordCount]  chapter codepoint offsets, [start, end)
//   uint16_t textOff[wordCount]        byte offset of word i's text in text[]
//   int16_t  xpos[wordCount]
//   uint16_t focusSplitX[wordCount]    present only when focusPresent
//   uint16_t displayOff[wordCount]     present only when displayPresent; NO_DISPLAY = none
//   uint8_t  styles[wordCount]
//   uint8_t  focusBoundary[wordCount]  present only when focusPresent
//   char     text[textBytes]           all words back to back, NUL-terminated
//   char     display[displayBytes]     present only when displayPresent, NUL-terminated
//
// Each word is stored NUL-terminated so render() can hand `text + textOff[i]`
// straight to C APIs (drawText) with no std::string materialization.
//
// Display text: complex-script words are stored a second time in the form
// layout measured them in, usually shaped glyph tokens (see
// GfxRenderer::resolveForDisplay), so a page turn draws exactly that without
// running the shaper. wordText() stays the logical text for dictionary lookup
// and text extraction. Absent entirely when no word on the line needs it.
//
// Focus split semantics (unchanged from the vector layout): boundary N > 0
// means the first N bytes of word i render bold, the remainder in the base
// style. N is bounded to 9 codepoints (<= 36 UTF-8 bytes) by the clamp in
// ParsedText::addWord. focusSplitX is the pre-computed pixel offset from the
// word start to the second visual run (bold prefix in RTL, regular suffix in LTR).
// Both arrays are omitted when no word on the line has a split.
class TextBlock final : public Block {
 public:
  // The unused high bit of the cached style byte carries clipping metadata.
  static constexpr uint8_t DISCRETIONARY_HYPHEN_FLAG = 0x80;
  // Word font slot bit for CSS small caps; the style byte's high bit is the hyphen flag.
  static constexpr uint8_t SMALL_CAPS_SLOT_FLAG = 0x80;
  // Word font slot bit for a short run (25, A, !?) that a vertical line sets upright in one
  // cell (tate-chu-yoko). Set by layout; the word's xpos already allows one cell for it.
  // Not the next bit down from small caps, so an upstream flag is unlikely to land on it.
  static constexpr uint8_t COMBINE_UPRIGHT_SLOT_FLAG = 0x10;
  static constexpr uint8_t SLOT_FLAGS = SMALL_CAPS_SLOT_FLAG | COMBINE_UPRIGHT_SLOT_FLAG;
  struct SourceRange {
    uint32_t start = UINT32_MAX;
    uint32_t end = UINT32_MAX;
  };

  struct LinkSpan {
    char href[FOOTNOTE_HREF_LEN];
    int16_t x;
    int16_t width;
    int16_t topLift;
  };

 private:
  BlockStyle blockStyle;
  static constexpr uint16_t NO_DISPLAY = 0xFFFF;
  // Serialized flags byte.
  static constexpr uint8_t FLAG_FOCUS = 1;
  static constexpr uint8_t FLAG_DISPLAY = 2;

  uint16_t numWords = 0;
  uint16_t paragraphStartWord = UINT16_MAX;  // Visual index of the paragraph's first logical word.
  uint16_t textBytes = 0;                    // total size of the text region, including NULs
  uint16_t displayBytes = 0;                 // total size of the display region, including NULs
  bool focusPresent = false;
  bool displayPresent = false;
  bool isValid = true;
  // The ONLY allocation: makeUniqueNoThrow, so OOM yields an invalid block
  // instead of abort() (bare new is not nothrow with -fno-exceptions).
  std::unique_ptr<uint8_t[]> arena;
  // Typed views into the arena, bound once after the arena is filled. All
  // 16-bit bases sit at even offsets, so direct dereference is alignment-safe.
  const SourceRange* sourceRanges = nullptr;
  const uint16_t* textOffArr = nullptr;
  const int16_t* xposArr = nullptr;
  const uint16_t* focusSplitXArr = nullptr;  // null when !focusPresent
  const uint8_t* stylesArr = nullptr;
  const uint8_t* focusBoundaryArr = nullptr;  // null when !focusPresent
  const char* textArr = nullptr;
  std::vector<std::string> rubyTexts;
  // Words sized apart from the line (CSS font-size spans): MAX_WORD_FONTS int32 font ids,
  // then one slot per word (0 = block font, n = font n-1). Null when every word uses the
  // block font, so ordinary lines pay only this pointer.
  std::unique_ptr<uint8_t[]> wordFontData;
  // Layout-only metadata. ChapterHtmlSlimParser moves it into Page::links
  // immediately; cached TextBlocks therefore keep the same compact format.
  std::vector<LinkSpan> linkSpans;

  TextBlock() = default;  // deserialize() fills the fields directly
  static size_t arenaSize(uint16_t wordCount, bool hasFocus, uint16_t textBytes, bool hasDisplay,
                          uint16_t displayBytes);
  void bindArenaPointers();
  // Valid when displayPresent: offsets precede styles, and display text follows logical text.
  const uint16_t* displayOffsets() const {
    return reinterpret_cast<const uint16_t*>(stylesArr - static_cast<size_t>(numWords) * sizeof(uint16_t));
  }
  const char* displayText() const { return textArr + textBytes; }

 public:
  // Flatten-on-construct: copies the layout-time vectors into the arena; the
  // vectors die with the caller. On arena OOM the block is empty and valid()
  // is false -- callers must check and fail the line instead of using it.
  // displayWords is empty, or sized like words with "" for words that draw as is.
  explicit TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& focusBoundary,
                     const std::vector<uint16_t>& focusSplitX, const BlockStyle& blockStyle = BlockStyle(),
                     std::vector<std::string> rubyTexts = {}, std::vector<LinkSpan> linkSpans = {},
                     const std::vector<SourceRange>& ranges = {}, uint16_t paragraphStartWord = UINT16_MAX,
                     const std::vector<std::string>& displayWords = {});
  ~TextBlock() override = default;
  TextBlock(const TextBlock&) = delete;
  TextBlock& operator=(const TextBlock&) = delete;

  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  const BlockStyle& getBlockStyle() const { return blockStyle; }
  // The font this line was laid out in: a CSS font-size override, else the section font.
  int blockFontId(const int sectionFontId) const { return blockStyle.fontId ? blockStyle.fontId : sectionFontId; }

  static constexpr uint8_t MAX_WORD_FONTS = 3;
  // slots[i] picks word i's font: 0 = the block font, n = fonts[n-1]; the SLOT_FLAGS bits
  // mark small caps and upright runs. Returns false on OOM.
  bool setWordFonts(const uint8_t* slots, const int32_t (&fonts)[MAX_WORD_FONTS]);
  int wordFontId(uint16_t i, int sectionFontId) const;
  bool wordHasSmallCaps(uint16_t i) const {
    return wordFontData && (wordFontData[sizeof(int32_t) * MAX_WORD_FONTS + i] & SMALL_CAPS_SLOT_FLAG);
  }
  bool wordIsCombinedUpright(uint16_t i) const {
    return wordFontData && (wordFontData[sizeof(int32_t) * MAX_WORD_FONTS + i] & COMBINE_UPRIGHT_SLOT_FLAG);
  }
  // Vertical pages: a comma or full stop is drawn in the first half of its cell, so when the
  // word ends a line the blank second half may pass the line end. Returns that blank part
  // for `word`, or 0 when it does not end in such a mark. No ink leaves the text area: this
  // is line-end trimming, not hanging punctuation. `word` must be NUL-terminated.
  static int lineEndTrim(const GfxRenderer& renderer, int fontId, std::string_view word, EpdFontFamily::Style style);
  // lineEndTrim() for word i when it is the last word of a vertical line, else 0. Boxes and
  // rules drawn over the word stop short by this much, so they stay inside the text area.
  int wordLineEndTrim(const GfxRenderer& renderer, uint16_t i, int wordFont, bool vertical) const {
    if (!vertical || blockStyle.isRtl || i + 1 != numWords) return 0;
    return lineEndTrim(renderer, wordFont, {wordText(i), wordTextLen(i)}, wordStyle(i));
  }
  // fontId when it is (or can be) registered with the renderer, else the section font.
  static int renderFontId(const GfxRenderer& renderer, int fontId, int sectionFontId);
  // Ascent of the line's shared baseline: the tallest font among its words.
  int lineAscent(const GfxRenderer& renderer, int sectionFontId) const;
  // Offset from the line top at which to draw word i so every word shares the baseline.
  int wordYOffset(const GfxRenderer& renderer, int sectionFontId, uint16_t i) const;
  // Extra height the line needs beyond its block font for taller words.
  int extraAscent(const GfxRenderer& renderer, int sectionFontId) const;
  bool isEmpty() override { return numWords == 0; }
  bool valid() const { return isValid; }
  uint16_t wordCount() const { return numWords; }
  // NUL-terminated by construction; safe to pass to C APIs directly.
  const char* wordText(const uint16_t i) const { return textArr + textOffArr[i]; }
  // The form layout measured word i in (GfxRenderer::resolveForDisplay), or
  // nullptr when it measured wordText(i) and needs no stored form, or layout
  // dropped a form that measured differently.
  const char* displayForm(const uint16_t i) const {
    if (!displayPresent) return nullptr;
    const uint16_t offset = displayOffsets()[i];
    return offset != NO_DISPLAY ? displayText() + offset : nullptr;
  }
  // Word i as layout measured it, for measuring and drawing. A focus boundary
  // indexes the logical text: ParsedText never splits a word that has a
  // display form, but a corrupt cache could pair them.
  GfxRenderer::LaidOutText laidOutWord(const uint16_t i) const {
    return {wordText(i), focusBoundary(i) > 0 ? nullptr : displayForm(i)};
  }
  uint16_t wordTextLen(const uint16_t i) const {
    const uint16_t end = (i + 1 < numWords) ? textOffArr[i + 1] : textBytes;
    return end - textOffArr[i] - 1;  // exclude the NUL
  }
  SourceRange wordSourceRange(const uint16_t i) const { return sourceRanges[i]; }
  bool wordStartsParagraph(const uint16_t i) const { return i == paragraphStartWord; }
  int16_t wordXpos(const uint16_t i) const { return xposArr[i]; }
  EpdFontFamily::Style wordStyle(const uint16_t i) const {
    return static_cast<EpdFontFamily::Style>((stylesArr[i] & ~DISCRETIONARY_HYPHEN_FLAG) |
                                             (wordHasSmallCaps(i) ? EpdFontFamily::SMALL_CAPS : 0));
  }
  bool wordHasDiscretionaryHyphen(const uint16_t i) const { return (stylesArr[i] & DISCRETIONARY_HYPHEN_FLAG) != 0; }
  uint8_t focusBoundary(const uint16_t i) const { return focusPresent ? focusBoundaryArr[i] : 0; }
  uint16_t focusSplitX(const uint16_t i) const { return focusPresent ? focusSplitXArr[i] : 0; }
  bool hasRuby() const;
  int getRubyShift(int ascender) const { return hasRuby() ? (ascender / 2) : 0; }
  const std::vector<std::string>& getRubyTexts() const { return rubyTexts; }
  std::vector<LinkSpan> takeLinkSpans() { return std::move(linkSpans); }

  // vertical: draw the line as a tategaki column. (x, y) is then the screen position of the
  // line's start at its top edge, which faces right; see GfxRenderer::drawTextVertical.
  void render(const GfxRenderer& renderer, int fontId, int x, int y, bool vertical = false) const;
  BlockType getType() override { return TEXT_BLOCK; }
  bool serialize(HalFile& file) const;
  static std::unique_ptr<TextBlock> deserialize(HalFile& file);
};
