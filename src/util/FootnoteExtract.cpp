#include "FootnoteExtract.h"

#include <cstring>

namespace {

bool nameIs(const char* name, const char* const* list) {
  for (; *list; ++list) {
    if (strcmp(name, *list) == 0) return true;
  }
  return false;
}

const char* const VOID_TAGS[] = {"br",  "img",  "hr",   "meta", "link",   "input",
                                 "col", "area", "base", "wbr",  "source", nullptr};
const char* const INLINE_TAGS[] = {"a",     "span", "sup",  "sub",   "b",      "i",    "em",   "strong",
                                   "small", "cite", "abbr", "label", "strike", "font", nullptr};
// Targets that stand for a chapter or a part of one, not a note.
const char* const CONTAINER_TAGS[] = {"html", "body", "section", "article", "nav", "header", "main", nullptr};

bool isHeading(const char* name) { return name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0'; }

char lower(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

// Calls visit(key, keyLen, value, valueLen) for each attribute in a start tag's text
// (the bytes between '<' and '>'). Stops early when visit returns true.
template <typename Visit>
bool forEachAttribute(const char* tag, Visit visit) {
  const char* p = tag;
  while (*p && !isSpace(*p) && *p != '/') ++p;  // tag name
  while (*p) {
    while (isSpace(*p) || *p == '/') ++p;
    const char* key = p;
    while (*p && !isSpace(*p) && *p != '=' && *p != '/') ++p;
    const size_t keyLen = static_cast<size_t>(p - key);
    if (keyLen == 0) break;
    while (isSpace(*p)) ++p;
    const char* value = p;
    size_t valueLen = 0;
    if (*p == '=') {
      ++p;
      while (isSpace(*p)) ++p;
      if (*p == '"' || *p == '\'') {
        const char q = *p++;
        value = p;
        while (*p && *p != q) ++p;
        valueLen = static_cast<size_t>(p - value);
        if (*p) ++p;
      } else {
        value = p;
        while (*p && !isSpace(*p)) ++p;
        valueLen = static_cast<size_t>(p - value);
      }
    }
    if (visit(key, keyLen, value, valueLen)) return true;
  }
  return false;
}

bool keyIs(const char* key, const size_t keyLen, const char* expected) {
  const size_t n = strlen(expected);
  if (keyLen != n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (lower(key[i]) != expected[i]) return false;
  }
  return true;
}

bool contains(const char* text, const size_t len, const char* needle) {
  const size_t n = strlen(needle);
  for (size_t i = 0; i + n <= len; ++i) {
    size_t j = 0;
    while (j < n && lower(text[i + j]) == needle[j]) ++j;
    if (j == n) return true;
  }
  return false;
}

// id on any element; the old name form only on <a>, so <meta name="author"> never matches.
bool hasAnchor(const char* tag, const char* name, const std::string& anchor) {
  const bool isLink = strcmp(name, "a") == 0;
  return forEachAttribute(tag, [&](const char* key, size_t keyLen, const char* value, size_t valueLen) {
    return (keyIs(key, keyLen, "id") || (isLink && keyIs(key, keyLen, "name"))) && valueLen == anchor.size() &&
           memcmp(value, anchor.data(), valueLen) == 0;
  });
}

bool hasHref(const char* tag) {
  return forEachAttribute(
      tag, [](const char* key, size_t keyLen, const char*, size_t) { return keyIs(key, keyLen, "href"); });
}

bool isNoteRef(const char* tag) {
  return forEachAttribute(tag, [](const char* key, size_t keyLen, const char* value, size_t valueLen) {
    return (keyIs(key, keyLen, "epub:type") || keyIs(key, keyLen, "role")) && contains(value, valueLen, "noteref");
  });
}

bool isPageBreak(const char* tag) {
  return forEachAttribute(tag, [](const char* key, size_t keyLen, const char* value, size_t valueLen) {
    return (keyIs(key, keyLen, "epub:type") || keyIs(key, keyLen, "role")) && contains(value, valueLen, "pagebreak");
  });
}

// epub:type="footnote", role="doc-endnote" and the like.
bool marksNote(const char* tag) {
  return forEachAttribute(tag, [](const char* key, size_t keyLen, const char* value, size_t valueLen) {
    if (!keyIs(key, keyLen, "epub:type") && !keyIs(key, keyLen, "role")) return false;
    return contains(value, valueLen, "footnote") || contains(value, valueLen, "endnote") ||
           contains(value, valueLen, "rearnote");
  });
}

}  // namespace

// Marks that a block holds text, ignoring what may stand before a note's number:
// spaces, entities such as &#160;, a raw no-break space and ASCII punctuation.
void FootnoteExtractor::noteText(const char c) {
  const auto b = static_cast<unsigned char>(c);
  if (inEntity) {
    if (c == ';') inEntity = false;
    return;
  }
  if (afterC2) {  // the second byte of U+00A0 and the other Latin-1 signs
    afterC2 = false;
    return;
  }
  if (c == '&') {
    inEntity = true;
  } else if (b == 0xC2) {
    afterC2 = true;
  } else if (!isSpace(c) && !(b < 0x80 && !((c >= '0' && c <= '9') || (lower(c) >= 'a' && lower(c) <= 'z')))) {
    textInBlock = true;
  }
}

bool FootnoteExtractor::feed(const uint8_t* data, const size_t size) {
  for (size_t i = 0; i < size; ++i) {
    const char c = static_cast<char>(data[i]);
    switch (mode) {
      case Mode::Done:
        return false;
      case Mode::Text:
        if (c == '<') {
          mode = Mode::Tag;
          tagLen = 0;
          quote = 0;
        } else {
          noteText(c);
          if (capturing) append(c);
        }
        break;
      case Mode::Tag:
        if (quote) {
          if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
          quote = c;
        } else if (c == '>') {
          tag[tagLen] = '\0';
          processTag();
          if (mode != Mode::Done) mode = Mode::Text;
          break;
        }
        if (tagLen < MAX_TAG_BYTES) tag[tagLen++] = c;
        if (tagLen == 3 && memcmp(tag, "!--", 3) == 0) {
          mode = Mode::Comment;
          commentTail[0] = commentTail[1] = 0;
        }
        break;
      case Mode::Comment:
        if (commentTail[0] == '-' && commentTail[1] == '-' && c == '>') {
          mode = Mode::Text;
        } else {
          commentTail[0] = commentTail[1];
          commentTail[1] = c;
        }
        break;
    }
  }
  return mode != Mode::Done;
}

FootnoteExtractor::Result FootnoteExtractor::result() const {
  if (notANote) return Result::NotANote;
  return found ? Result::Note : Result::NotFound;
}

void FootnoteExtractor::append(const char c) {
  if (!capturing) return;
  html.push_back(c);
  if (html.size() >= MAX_HTML_BYTES) finishCut();
}

// Ends a note cut at the size bound as well-formed XHTML: no half tag, entity or UTF-8
// sequence at the end, an ellipsis, and the open elements closed.
void FootnoteExtractor::finishCut() {
  if (appendingTag) html.resize(tagStart);
  appendingTag = false;
  // An entity is at most a few bytes; a bare '&' further back is not one being cut.
  const size_t amp = html.rfind('&');
  if (amp != std::string::npos && html.size() - amp <= 12 && html.find_first_of(";<", amp) == std::string::npos) {
    html.resize(amp);
  }
  size_t lead = html.size();
  while (lead > 0 && (static_cast<unsigned char>(html[lead - 1]) & 0xC0) == 0x80) --lead;
  if (lead > 0) {
    const auto b = static_cast<unsigned char>(html[lead - 1]);
    const size_t need = b < 0x80 ? 1 : (b >> 5) == 0x06 ? 2 : (b >> 4) == 0x0E ? 3 : 4;
    if (html.size() - (lead - 1) < need) html.resize(lead - 1);
  }
  html.append("\u2026");
  for (size_t d = depth; d > captureDepth; --d) {
    if (d > MAX_DEPTH) continue;
    html.append("</");
    html.append(stack[d - 1]);
    html.push_back('>');
  }
  cut = true;
  capturing = false;
  mode = Mode::Done;
}

void FootnoteExtractor::processTag() {
  if (tagLen == 0) return;
  if (capturing) {
    appendTagName();
    if (!capturing) return;
  }
  if (tag[0] == '!' || tag[0] == '?') return;

  const bool closing = tag[0] == '/';
  const char* p = closing ? tag + 1 : tag;
  char name[NAME_BYTES] = {};
  size_t n = 0;
  while (*p && !isSpace(*p) && *p != '/' && n + 1 < NAME_BYTES) name[n++] = lower(*p++);
  if (n == 0) return;
  if (closing) {
    endTag(name);
  } else {
    startTag(name, tag[tagLen - 1] == '/');
  }
}

// Only the tag name is kept: the plain-text pass needs no attributes, and dropping them
// keeps quoted '>' characters away from it.
void FootnoteExtractor::appendTagName() {
  // A cut inside this tag drops all of it: the open-element stack has not seen it yet.
  tagStart = html.size();
  appendingTag = true;
  append('<');
  for (size_t i = 0; i < tagLen && capturing && !isSpace(tag[i]) && (i == 0 || tag[i] != '/'); ++i) append(tag[i]);
  if (capturing && tagLen > 1 && tag[tagLen - 1] == '/') append('/');
  if (capturing) append('>');
  appendingTag = false;
}

void FootnoteExtractor::startCapture() {
  capturing = true;
  // Room for the closing tags a cut adds past the bound.
  html.reserve(MAX_HTML_BYTES + MAX_DEPTH * (NAME_BYTES + 3));
  appendTagName();
}

void FootnoteExtractor::startTag(const char* name, const bool selfClosing) {
  const bool isVoid = selfClosing || nameIs(name, VOID_TAGS);
  if (!found && hasAnchor(tag, name, anchor)) {
    found = true;
    markedNote = marksNote(tag);
    // A link after text in its paragraph is a note's reference in the book, the target of a
    // back-link; a note's own number links back too, but starts its paragraph.
    const bool backLinkTarget = isNoteRef(tag) || (hasHref(tag) && textInBlock) || isPageBreak(tag);
    if (!markedNote && (isHeading(name) || nameIs(name, CONTAINER_TAGS) || backLinkTarget)) {
      notANote = true;
      mode = Mode::Done;
      return;
    }
    // An id on an inline element marks the start of the note's paragraph: keep the first
    // enclosing element that is not itself inline, such as the <p> around <sup><a id>.
    captureDepth = depth;
    if (isVoid || nameIs(name, INLINE_TAGS)) {
      size_t d = depth;
      while (d > 0 && d <= MAX_DEPTH && nameIs(stack[d - 1], INLINE_TAGS)) --d;
      // An anchor loose in the body or a section, or an empty marker in a <div>, starts a
      // chapter, not a note.
      const bool emptyInDiv = isVoid && d > 0 && d <= MAX_DEPTH && strcmp(stack[d - 1], "div") == 0;
      if (!markedNote && (d == 0 || (d <= MAX_DEPTH && nameIs(stack[d - 1], CONTAINER_TAGS)) || emptyInDiv)) {
        notANote = true;
        mode = Mode::Done;
        return;
      }
      captureDepth = d - 1;
      // Reopen the paragraph and its inline wrappers so the kept HTML is balanced.
      capturing = true;
      html.reserve(MAX_HTML_BYTES + MAX_DEPTH * (NAME_BYTES + 3));
      for (size_t k = captureDepth; k < depth && k < MAX_DEPTH; ++k) {
        html.push_back('<');
        html.append(stack[k]);
        html.push_back('>');
      }
    }
    startCapture();
  } else if (capturing && !markedNote && isHeading(name)) {
    notANote = true;
    capturing = false;
    mode = Mode::Done;
    return;
  }
  if (isVoid) return;
  if (!nameIs(name, INLINE_TAGS)) textInBlock = false;
  if (depth < MAX_DEPTH) {
    memcpy(stack[depth], name, NAME_BYTES);
  }
  ++depth;
}

void FootnoteExtractor::endTag(const char* name) {
  if (depth == 0) return;
  if (depth > MAX_DEPTH) {
    --depth;
  } else {
    size_t k = depth;
    while (k > 0 && strcmp(stack[k - 1], name) != 0) --k;
    if (k == 0) return;  // a stray end tag
    depth = k - 1;
  }
  if (capturing && depth <= captureDepth) {
    capturing = false;
    mode = Mode::Done;
  }
}
