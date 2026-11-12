#include "ReadingSpeed.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr uint32_t MIN_DWELL_MS = 2000;    // a quick flip, not reading
constexpr uint32_t MAX_DWELL_MS = 300000;  // over 5 minutes on one page: away, not reading
constexpr uint32_t OUTLIER_FACTOR = 3;     // a warmed-up estimate moves toward at most 3x / one third
constexpr uint8_t SMOOTHING = 8;           // EMA weight 1/8 once warmed up
constexpr int MAX_MINUTES = 999 * 60;
}  // namespace

void ReadingSpeed::restore(const uint32_t msPerPage, const uint8_t samples) {
  if (msPerPage < MIN_DWELL_MS || msPerPage > MAX_DWELL_MS || samples == 0) {
    estimateMs = 0;
    sampleCount = 0;
    return;
  }
  estimateMs = msPerPage;
  sampleCount = samples;
}

void ReadingSpeed::addPage(uint32_t dwellMs) {
  if (dwellMs < MIN_DWELL_MS || dwellMs > MAX_DWELL_MS) return;
  if (ready()) {
    dwellMs = std::clamp(dwellMs, estimateMs / OUTLIER_FACTOR, estimateMs * OUTLIER_FACTOR);
  }
  if (sampleCount < UINT8_MAX) sampleCount++;
  // Plain average for the first pages, then an exponential moving average.
  const int64_t weight = std::min<uint8_t>(sampleCount, SMOOTHING);
  const int64_t delta = static_cast<int64_t>(dwellMs) - static_cast<int64_t>(estimateMs);
  estimateMs = static_cast<uint32_t>(static_cast<int64_t>(estimateMs) + delta / weight);
}

int ReadingSpeed::minutesFor(const float pages) const {
  if (!ready() || !(pages >= 0.0f)) return -1;
  const float minutes = std::ceil(pages * static_cast<float>(estimateMs) / 60000.0f);
  return minutes >= MAX_MINUTES ? MAX_MINUTES : static_cast<int>(minutes);
}

float estimateBookPagesLeft(const float chapterPagesLeft, const float chapterPages, const size_t chapterBytes,
                            const size_t bytesAfterChapter) {
  if (bytesAfterChapter == 0) return chapterPagesLeft;
  // A cover or title page has too few pages to scale the rest of the book.
  constexpr float MIN_CHAPTER_PAGES = 3.0f;
  // Image pages carry almost no XHTML; a text page has well over this even at
  // large font sizes. Below it the ratio would turn the rest of the book into
  // thousands of pages.
  constexpr float MIN_BYTES_PER_PAGE = 300.0f;
  if (chapterPages < MIN_CHAPTER_PAGES || static_cast<float>(chapterBytes) < chapterPages * MIN_BYTES_PER_PAGE) {
    return -1.0f;
  }
  return chapterPagesLeft + static_cast<float>(bytesAfterChapter) * chapterPages / static_cast<float>(chapterBytes);
}

void formatTimeLeft(char* out, const size_t outSize, const int minutes, const char* hoursMinutesFormat,
                    const char* minutesFormat) {
  if (outSize == 0) return;
  if (minutes < 0) {
    out[0] = '\0';
  } else if (minutes < 60) {
    snprintf(out, outSize, minutesFormat, minutes);
  } else {
    snprintf(out, outSize, hoursMinutesFormat, minutes / 60, minutes % 60);
  }
}
