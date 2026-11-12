#include <gtest/gtest.h>

#include "ReadingSpeed.h"

namespace {

ReadingSpeed warmedUp(uint32_t ms) {
  ReadingSpeed speed;
  for (int i = 0; i < ReadingSpeed::MIN_SAMPLES; i++) speed.addPage(ms);
  return speed;
}

TEST(ReadingSpeed, NotReadyUntilEnoughPages) {
  ReadingSpeed speed;
  for (int i = 0; i < ReadingSpeed::MIN_SAMPLES - 1; i++) speed.addPage(30000);
  EXPECT_FALSE(speed.ready());
  EXPECT_EQ(speed.minutesFor(10), -1);
  speed.addPage(30000);
  EXPECT_TRUE(speed.ready());
  EXPECT_EQ(speed.msPerPage(), 30000u);
}

TEST(ReadingSpeed, AveragesFirstPages) {
  ReadingSpeed speed;
  speed.addPage(20000);
  speed.addPage(40000);
  EXPECT_EQ(speed.msPerPage(), 30000u);
}

TEST(ReadingSpeed, IgnoresQuickFlipsAndIdleGaps) {
  ReadingSpeed speed = warmedUp(30000);
  speed.addPage(1999);
  speed.addPage(300001);
  EXPECT_EQ(speed.msPerPage(), 30000u);
  EXPECT_EQ(speed.samples(), ReadingSpeed::MIN_SAMPLES);
}

TEST(ReadingSpeed, ClampsOutliersOnceWarmedUp) {
  ReadingSpeed speed = warmedUp(30000);
  speed.addPage(290000);  // counted as 3x = 90 s
  // 30000 + (90000 - 30000) / 6 samples
  EXPECT_EQ(speed.msPerPage(), 40000u);
}

TEST(ReadingSpeed, SmoothsWithEighthWeightAfterWarmUp) {
  ReadingSpeed speed = warmedUp(30000);
  for (int i = 0; i < 3; i++) speed.addPage(30000);  // 8 samples
  speed.addPage(38000);
  EXPECT_EQ(speed.msPerPage(), 31000u);
}

TEST(ReadingSpeed, RestoreKeepsLearnedSpeedAndRejectsJunk) {
  ReadingSpeed speed;
  speed.restore(45000, 20);
  EXPECT_TRUE(speed.ready());
  EXPECT_EQ(speed.minutesFor(4), 3);

  speed.restore(500, 20);  // below the quick-flip floor
  EXPECT_FALSE(speed.ready());
  EXPECT_EQ(speed.samples(), 0);
}

TEST(ReadingSpeed, SampleCountSaturates) {
  ReadingSpeed speed;
  speed.restore(30000, 255);
  speed.addPage(30000);
  EXPECT_EQ(speed.samples(), 255);
}

TEST(ReadingSpeed, MinutesRoundUp) {
  ReadingSpeed speed = warmedUp(30000);
  EXPECT_EQ(speed.minutesFor(0), 0);
  EXPECT_EQ(speed.minutesFor(1), 1);
  EXPECT_EQ(speed.minutesFor(2), 1);
  EXPECT_EQ(speed.minutesFor(3), 2);
  EXPECT_EQ(speed.minutesFor(-1), -1);
}

TEST(ReadingSpeed, MinutesAreCapped) {
  ReadingSpeed speed = warmedUp(300000);
  EXPECT_EQ(speed.minutesFor(1e6f), 999 * 60);
}

TEST(ReadingSpeed, BookPagesUseChapterBytesPerPage) {
  // 10-page chapter of 20000 bytes: 2000 bytes per page; 40000 bytes after it.
  EXPECT_FLOAT_EQ(estimateBookPagesLeft(4, 10, 20000, 40000), 24.0f);
  // Last chapter: only its own pages are left.
  EXPECT_FLOAT_EQ(estimateBookPagesLeft(2, 2, 100, 0), 2.0f);
  // A cover page or an empty chapter gives no usable ratio.
  EXPECT_LT(estimateBookPagesLeft(1, 1, 500, 40000), 0.0f);
  EXPECT_LT(estimateBookPagesLeft(4, 10, 0, 40000), 0.0f);
}

TEST(ReadingSpeed, BookEstimateHiddenForImageChapters) {
  // 20 image pages in 1 KB of XHTML would make 400 KB of text into 8000 pages.
  EXPECT_LT(estimateBookPagesLeft(5, 20, 1000, 400000), 0.0f);
  // At the limit (300 bytes per page) the estimate is still shown.
  EXPECT_FLOAT_EQ(estimateBookPagesLeft(5, 20, 6000, 6000), 25.0f);
  // The last chapter needs no ratio, whatever it holds.
  EXPECT_FLOAT_EQ(estimateBookPagesLeft(5, 20, 1000, 0), 5.0f);
}

TEST(ReadingSpeed, FormatsMinutesAndHours) {
  char buf[24];
  formatTimeLeft(buf, sizeof(buf), 12, "%dh %02dm", "%dm");
  EXPECT_STREQ(buf, "12m");
  formatTimeLeft(buf, sizeof(buf), 59, "%dh %02dm", "%dm");
  EXPECT_STREQ(buf, "59m");
  formatTimeLeft(buf, sizeof(buf), 65, "%dh %02dm", "%dm");
  EXPECT_STREQ(buf, "1h 05m");
  formatTimeLeft(buf, sizeof(buf), -1, "%dh %02dm", "%dm");
  EXPECT_STREQ(buf, "");
}

}  // namespace
