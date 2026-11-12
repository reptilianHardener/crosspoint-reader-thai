#pragma once

#include <cstdint>

// Fixed-scalar active-reading summary. The owner reports turn outcomes before
// render and completed pages after render; no allocation or I/O occurs here.
class ReaderSession {
 public:
  // readerPaced is false for automatic turns: they say nothing about reading speed.
  // turnMs is when the reader asked for the turn (millis()).
  void noteTurn(bool forward, bool succeeded, bool readerPaced, uint32_t turnMs);
  // For a reader-paced forward turn, returns the time from the previous turn's
  // page display to this turn, else 0. Feeds ReadingSpeed.
  uint32_t onRenderComplete(uint32_t monotonicMs, int64_t trustedEpoch, int progressBp);
  void reset();

  bool isEmitWorthy() const { return startEpoch > 0 && duration >= 10 && endTime() > startEpoch; }
  int64_t startTime() const { return startEpoch; }
  int64_t endTime() const { return startEpoch > 0 ? startEpoch + duration : 0; }
  uint32_t durationSeconds() const { return duration; }
  uint16_t startProgressBp() const { return startProgress; }
  uint16_t endProgressBp() const { return endProgress; }

 private:
  static uint16_t clampProgress(int progressBp);

  int64_t anchorEpoch = 0;
  int64_t startEpoch = 0;
  uint32_t anchorMs = 0;
  uint32_t duration = 0;
  // Reading-speed anchor: moves only when a turn changed the page, so overlay
  // repaints do not shorten the next sample.
  uint32_t pageShownMs = 0;
  uint32_t pendingTurnMs = 0;
  uint16_t anchorProgress = 0;
  uint16_t startProgress = 0;
  uint16_t endProgress = 0;
  bool hasAnchor = false;
  bool pendingForward = false;
  bool pendingPaced = false;
  bool pendingMoved = false;
};
