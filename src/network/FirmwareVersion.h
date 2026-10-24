#pragma once

#include <cstddef>
#include <cstring>

// Firmware versions are upstream's major.minor.patch plus this fork's own fourth number
// (2.0.0.1 is upstream 2.0.0, fork build 1). A missing fourth number counts as 0, so a
// plain upstream-style tag still orders correctly, and a leading "v" (as in GitHub tags
// like v2.2.15) is ignored. Anything after the numbers ("-rc", "-dev-…") is not compared.
namespace firmware_version {

struct Version {
  long parts[4] = {0, 0, 0, 0};
};

// False unless at least major.minor.patch parse.
inline bool parse(const char* text, Version& out) {
  if (text == nullptr) return false;
  if (*text == 'v' || *text == 'V') ++text;
  size_t count = 0;
  while (count < 4) {
    if (*text < '0' || *text > '9') break;
    long value = 0;
    while (*text >= '0' && *text <= '9') {
      value = value * 10 + (*text - '0');
      ++text;
    }
    out.parts[count++] = value;
    if (*text != '.') break;
    ++text;
  }
  return count >= 3;
}

// -1, 0 or 1 as a orders before, equal to, or after b on the numeric parts.
inline int compare(const Version& a, const Version& b) {
  for (size_t i = 0; i < 4; i++) {
    if (a.parts[i] != b.parts[i]) return a.parts[i] < b.parts[i] ? -1 : 1;
  }
  return 0;
}

// A pre-release build ("-rc", "-dev") moves on to the release with the same number.
inline bool isPreRelease(const char* text) {
  return text != nullptr && (strstr(text, "-rc") != nullptr || strstr(text, "-dev") != nullptr);
}

}  // namespace firmware_version
