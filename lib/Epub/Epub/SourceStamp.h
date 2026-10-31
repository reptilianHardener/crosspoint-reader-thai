#pragma once

#include <string>

// A book's cache directory remembers which file it was built from: the file's size and FAT
// modification time in <cacheDir>/source.bin. A book replaced on the card at the same path
// (a corrected language tag, a new edition) then no longer matches, and the reader indexes
// it again. Caches written before this stamp existed have no file and report Unknown.
namespace SourceStamp {

enum class State { Unknown, Same, Changed };

// Compares the stamp with the file at sourcePath. Unknown when there is no stamp or the file
// cannot be opened. A side without a timestamp (0) compares size only.
State check(const std::string& cacheDir, const std::string& sourcePath);

// Writes the stamp for the file at sourcePath. False when the file cannot be opened or the
// stamp cannot be written.
bool write(const std::string& cacheDir, const std::string& sourcePath);

}  // namespace SourceStamp
