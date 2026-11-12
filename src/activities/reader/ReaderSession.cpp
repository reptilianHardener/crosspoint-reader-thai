#include "ReaderSession.h"

#include <algorithm>

namespace {
constexpr uint32_t MIN_DWELL_SECONDS = 2;
constexpr uint32_t MAX_DWELL_SECONDS = 1800;
}  // namespace

uint16_t ReaderSession::clampProgress(const int progressBp) {
  return static_cast<uint16_t>(std::clamp(progressBp, 0, 10000));
}

void ReaderSession::noteTurn(const bool forward, const bool succeeded, const bool readerPaced, const uint32_t turnMs) {
  // Two turns before one render: keep the first turn's time.
  if (succeeded && !pendingMoved) pendingTurnMs = turnMs;
  pendingMoved = pendingMoved || succeeded;
  pendingForward = forward && succeeded;
  pendingPaced = readerPaced;
}

uint32_t ReaderSession::onRenderComplete(const uint32_t monotonicMs, const int64_t trustedEpoch, const int progressBp) {
  const uint16_t progress = clampProgress(progressBp);
  // Ends at the turn, so the next page's render time is not counted.
  const uint32_t pacedDwellMs = pendingForward && pendingPaced && hasAnchor ? pendingTurnMs - pageShownMs : 0;
  if (pendingForward && hasAnchor && anchorEpoch > 0) {
    const uint32_t elapsedSeconds = (monotonicMs - anchorMs) / 1000;
    if (elapsedSeconds >= MIN_DWELL_SECONDS) {
      const uint32_t contribution = std::min(elapsedSeconds, MAX_DWELL_SECONDS);
      if (duration == 0) {
        startEpoch = anchorEpoch;
        startProgress = anchorProgress;
        endProgress = anchorProgress;
      }
      duration += contribution;
      endProgress = std::max(endProgress, progress);
    }
  }

  // A new page starts its time: after a turn, or when the position changed without one (a jump from a menu,
  // bookmark or link, or a Bluetooth remote turn). An overlay repaint keeps the position, and the time.
  if (pendingMoved || !hasAnchor || progress != anchorProgress) pageShownMs = monotonicMs;
  anchorMs = monotonicMs;
  anchorEpoch = trustedEpoch;
  anchorProgress = progress;
  hasAnchor = true;
  pendingForward = false;
  pendingPaced = false;
  pendingMoved = false;
  return pacedDwellMs;
}

void ReaderSession::reset() {
  anchorMs = 0;
  anchorEpoch = 0;
  startEpoch = 0;
  duration = 0;
  anchorProgress = 0;
  startProgress = 0;
  endProgress = 0;
  pageShownMs = 0;
  pendingTurnMs = 0;
  hasAnchor = false;
  pendingForward = false;
  pendingPaced = false;
  pendingMoved = false;
}
