#pragma once

#include <cstddef>
#include <string>
#include <vector>

// The Thai user dictionary: extra words the built-in word list does not know, so the
// line breaker can keep them whole. Stored one word per line in a plain text file on
// the SD card so it can also be edited on a computer; lines starting with '#' are
// comments.
namespace ThaiUserDictionary {

constexpr char FILE_PATH[] = "/crosspoint/thai_dict.txt";
// ThaiWordBreaker scans the user words linearly at every candidate position, and each
// word costs a small heap block, so the list is capped.
constexpr size_t MAX_WORDS = 200;

// Reads the file into `words` (cleared first). A missing file is not an error and
// leaves `words` empty. Returns false only when the read buffer cannot be allocated.
bool load(std::vector<std::string>& words);

// Writes `words` back, one per line. Returns false if the file cannot be opened.
bool save(const std::vector<std::string>& words);

// Loads the file and hands the words to ThaiWordBreaker. Called once at boot.
void applyFromStorage();

}  // namespace ThaiUserDictionary
