#pragma once

#include <string>

// Saved reading percentage, or -1 when unavailable. Never builds reading caches, and
// a book that was never opened costs one file check. Used by Home and the Library.
int loadBookProgress(const std::string& path);
