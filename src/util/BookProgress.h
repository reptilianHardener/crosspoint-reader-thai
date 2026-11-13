#pragma once

#include <string>

class Epub;
class Xtc;

// Saved reading percentage, or -1 when unavailable. Never builds reading caches, and
// a book that was never opened costs one file check. Used by Home and the Library.
// atEnd, when given, is set when the saved position is the end of the book, the
// rule the reader uses for a finished book.
int loadBookProgress(const std::string& path, bool* atEnd = nullptr);

// The same for a book that is already loaded.
int loadEpubProgress(const Epub& epub, bool* atEnd = nullptr);
int loadXtcProgress(const Xtc& xtc, bool* atEnd = nullptr);
