#pragma once

#include <string>

// Saved reading percentage, or -1 when unavailable. Never builds reading caches, and
// a book that was never opened costs one file check. Used by Home and the Library.
// atEnd, when given, is set when the saved position is the end of the book, the
// rule the reader uses for a finished book.
int loadBookProgress(const std::string& path, bool* atEnd = nullptr);
