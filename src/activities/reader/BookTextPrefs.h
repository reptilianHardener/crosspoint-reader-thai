#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "ProgressFile.h"

// A book's own reader text settings, kept as `<cachePath>/textprefs.bin`:
//   [version][fontFamily][fontPointSize][lineSpacing][paragraphAlignment][sdFontFamilyName, 32 bytes]
namespace BookTextPrefs {

constexpr uint8_t VERSION = 1;
constexpr size_t NAME_LEN = sizeof(CrossPointSettings::ReaderTextPrefs::sdFontFamilyName);
constexpr size_t FILE_SIZE = 5 + NAME_LEN;
constexpr char FILE_NAME[] = "textprefs.bin";

// False when the book has no (readable, current-version) prefs file.
inline bool load(const std::string& cachePath, CrossPointSettings::ReaderTextPrefs& prefs) {
  const std::string path = cachePath + "/" + FILE_NAME;
  if (!Storage.exists(path.c_str())) return false;

  HalFile f;
  if (!Storage.openFileForRead("BTP", path, f)) return false;
  uint8_t data[FILE_SIZE + 1];
  if (f.read(data, sizeof(data)) != static_cast<int>(FILE_SIZE) || data[0] != VERSION) {
    LOG_ERR("BTP", "Ignoring unreadable text prefs: %s", path.c_str());
    return false;
  }

  prefs.fontFamily = data[1];
  prefs.fontPointSize = data[2];
  prefs.lineSpacing = data[3];
  prefs.paragraphAlignment = data[4];
  memcpy(prefs.sdFontFamilyName, data + 5, NAME_LEN);
  prefs.sdFontFamilyName[NAME_LEN - 1] = '\0';
  return true;
}

inline bool save(const std::string& cachePath, const CrossPointSettings::ReaderTextPrefs& prefs) {
  uint8_t data[FILE_SIZE] = {};
  data[0] = VERSION;
  data[1] = prefs.fontFamily;
  data[2] = prefs.fontPointSize;
  data[3] = prefs.lineSpacing;
  data[4] = prefs.paragraphAlignment;
  strncpy(reinterpret_cast<char*>(data + 5), prefs.sdFontFamilyName, NAME_LEN - 1);
  return ProgressFile::writeAtomic(cachePath, data, sizeof(data), FILE_NAME);
}

}  // namespace BookTextPrefs
