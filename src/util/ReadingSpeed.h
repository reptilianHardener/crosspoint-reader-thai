#pragma once

#include <cstddef>
#include <cstdint>

// Learned reading speed for the status bar's "time left". Pure scalar state:
// the reader feeds it one dwell per hand-turned forward page, and persists
// msPerPage()/samples() itself (once per reading session, not per page).
class ReadingSpeed {
 public:
  // Pages learned (persisted ones included) before an estimate is shown.
  static constexpr uint8_t MIN_SAMPLES = 5;

  void restore(uint32_t msPerPage, uint8_t samples);
  // dwellMs: time from one page's display to the next. Quick flips and idle
  // gaps are ignored; after warm-up one sample can move the estimate only so far.
  void addPage(uint32_t dwellMs);

  bool ready() const { return sampleCount >= MIN_SAMPLES && estimateMs > 0; }
  uint32_t msPerPage() const { return estimateMs; }
  uint8_t samples() const { return sampleCount; }
  // Minutes to read `pages` pages, rounded up; -1 when not ready or pages < 0.
  int minutesFor(float pages) const;

 private:
  uint32_t estimateMs = 0;
  uint8_t sampleCount = 0;
};

// Whole-book pages left, using the current chapter's bytes per page for the
// spine bytes after it. -1 when the chapter is too short, or too image-heavy
// (few bytes per page), to give a usable ratio.
float estimateBookPagesLeft(float chapterPagesLeft, float chapterPages, size_t chapterBytes, size_t bytesAfterChapter);

// "12m" or "1h 05m" style text; the formats take (minutes) and (hours, minutes).
void formatTimeLeft(char* out, size_t outSize, int minutes, const char* hoursMinutesFormat, const char* minutesFormat);
