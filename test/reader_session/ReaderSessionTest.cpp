#include <gtest/gtest.h>

#include "ReaderSession.h"

namespace {

TEST(ReaderSession, UsesOnlyBoundedScalarState) { EXPECT_LE(sizeof(ReaderSession), 48u); }

void anchor(ReaderSession& session, uint32_t ms, int64_t epoch, int progress) {
  session.onRenderComplete(ms, epoch, progress);
}

void turnAndRender(ReaderSession& session, bool forward, bool succeeded, uint32_t ms, int64_t epoch, int progress) {
  session.noteTurn(forward, succeeded, true, ms);
  session.onRenderComplete(ms, epoch, progress);
}

TEST(ReaderSession, IgnoresDwellsUnderTwoSeconds) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, true, true, 2999, 102, 1100);

  EXPECT_EQ(session.durationSeconds(), 0u);
  EXPECT_FALSE(session.isEmitWorthy());
}

TEST(ReaderSession, AccumulatesSuccessfulForwardDwellsAndCompactsEndTime) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, true, true, 6000, 105, 1100);
  turnAndRender(session, true, true, 13000, 112, 1200);

  EXPECT_EQ(session.startTime(), 100);
  EXPECT_EQ(session.durationSeconds(), 12u);
  EXPECT_EQ(session.endTime(), 112);
  EXPECT_EQ(session.endTime(), session.startTime() + session.durationSeconds());
  EXPECT_EQ(session.startProgressBp(), 1000);
  EXPECT_EQ(session.endProgressBp(), 1200);
  EXPECT_TRUE(session.isEmitWorthy());
}

TEST(ReaderSession, ExcludesBackwardSkipAndFailedTurns) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, false, true, 11000, 110, 900);
  turnAndRender(session, true, false, 21000, 120, 1100);
  session.noteTurn(false, false, true, 31000);  // owner uses this signal for skips
  session.onRenderComplete(31000, 130, 2000);

  EXPECT_EQ(session.durationSeconds(), 0u);
}

TEST(ReaderSession, CapsOneDwellAtThirtyMinutes) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, true, true, 3601000, 3700, 2000);

  EXPECT_EQ(session.durationSeconds(), 1800u);
  EXPECT_EQ(session.endTime(), 1900);
}

TEST(ReaderSession, KeepsFirstEpochAndProgressAcrossContributions) {
  ReaderSession session;
  anchor(session, 1000, 1000, 3000);
  turnAndRender(session, true, true, 5000, 1004, 3100);
  turnAndRender(session, true, true, 11000, 1010, 3300);

  EXPECT_EQ(session.startTime(), 1000);
  EXPECT_EQ(session.startProgressBp(), 3000);
  EXPECT_EQ(session.endProgressBp(), 3300);
  EXPECT_EQ(session.durationSeconds(), 10u);
}

TEST(ReaderSession, ClampsProgressRangeAndMonotonicEnd) {
  ReaderSession session;
  anchor(session, 1000, 100, -50);
  turnAndRender(session, true, true, 7000, 106, 12000);
  turnAndRender(session, true, true, 13000, 112, 5000);

  EXPECT_EQ(session.startProgressBp(), 0);
  EXPECT_EQ(session.endProgressBp(), 10000);
}

TEST(ReaderSession, RequiresTenSecondsAndTrustedStart) {
  ReaderSession session;
  anchor(session, 1000, 0, 1000);
  turnAndRender(session, true, true, 11000, 10, 1100);
  EXPECT_EQ(session.durationSeconds(), 0u);

  turnAndRender(session, true, true, 20000, 19, 1200);
  EXPECT_EQ(session.durationSeconds(), 9u);
  EXPECT_FALSE(session.isEmitWorthy());

  turnAndRender(session, true, true, 22000, 21, 1300);
  EXPECT_EQ(session.durationSeconds(), 11u);
  EXPECT_TRUE(session.isEmitWorthy());
  EXPECT_GT(session.endTime(), session.startTime());
}

TEST(ReaderSession, ResetClearsSummaryAnchorAndPendingTurn) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  session.noteTurn(true, true, true, 5000);
  session.reset();
  session.onRenderComplete(12000, 111, 2000);

  EXPECT_EQ(session.startTime(), 0);
  EXPECT_EQ(session.durationSeconds(), 0u);
  EXPECT_EQ(session.startProgressBp(), 0);
  EXPECT_EQ(session.endProgressBp(), 0);
  EXPECT_FALSE(session.isEmitWorthy());
}

TEST(ReaderSession, MonotonicElapsedHandlesMillisWrap) {
  ReaderSession session;
  anchor(session, 0xfffffc18u, 100, 1000);
  turnAndRender(session, true, true, 4000u, 105, 1100);

  EXPECT_EQ(session.durationSeconds(), 5u);
}

TEST(ReaderSession, ReportsDwellOnlyForReaderPacedForwardTurns) {
  ReaderSession session;
  EXPECT_EQ(session.onRenderComplete(1000, 0, 1000), 0u);  // first render: no anchor yet

  session.noteTurn(true, true, true, 31000);
  EXPECT_EQ(session.onRenderComplete(31500, 0, 1100), 30000u);  // no trusted time needed

  session.noteTurn(true, true, false, 61500);  // automatic turn
  EXPECT_EQ(session.onRenderComplete(62000, 0, 1200), 0u);

  session.noteTurn(false, true, true, 92000);
  EXPECT_EQ(session.onRenderComplete(92500, 0, 1100), 0u);

  EXPECT_EQ(session.onRenderComplete(95000, 0, 1100), 0u);  // repaint without a turn
}

TEST(ReaderSession, DwellRunsFromPageShownToTurn) {
  ReaderSession session;
  session.onRenderComplete(1000, 0, 1000);

  // An overlay repaint and a failed turn leave the page shown at 1000.
  session.onRenderComplete(5000, 0, 1000);
  session.noteTurn(true, false, true, 8000);
  session.onRenderComplete(8100, 0, 1000);

  // A slow next-page render (chapter build) is not counted.
  session.noteTurn(true, true, true, 21000);
  EXPECT_EQ(session.onRenderComplete(29000, 0, 1100), 20000u);

  // The next sample starts when the turned page was shown, not at the turn.
  session.noteTurn(true, true, true, 39000);
  EXPECT_EQ(session.onRenderComplete(39500, 0, 1200), 10000u);

  // Two turns before one render: the first turn ends the sample.
  session.noteTurn(true, true, true, 45500);
  session.noteTurn(true, true, true, 46000);
  EXPECT_EQ(session.onRenderComplete(46500, 0, 1300), 6000u);

  // A jump or remote turn changes the position without noteTurn: the next sample starts at its render.
  EXPECT_EQ(session.onRenderComplete(100000, 0, 2500), 0u);
  session.noteTurn(true, true, true, 130000);
  EXPECT_EQ(session.onRenderComplete(130200, 0, 2600), 30000u);
}

}  // namespace
