#include "src/network/FirmwareVersion.h"

#include <gtest/gtest.h>

namespace {

int cmp(const char* a, const char* b) {
  firmware_version::Version va, vb;
  EXPECT_TRUE(firmware_version::parse(a, va)) << a;
  EXPECT_TRUE(firmware_version::parse(b, vb)) << b;
  return firmware_version::compare(va, vb);
}

TEST(FirmwareVersion, ForkNumberOrdersBuildsOfTheSameUpstreamVersion) {
  EXPECT_LT(cmp("2.0.0.1", "2.0.0.2"), 0);
  EXPECT_GT(cmp("2.0.0.10", "2.0.0.9"), 0);
  EXPECT_EQ(cmp("2.0.0.3", "2.0.0.3"), 0);
}

TEST(FirmwareVersion, UpstreamNumbersOutrankTheForkNumber) {
  EXPECT_GT(cmp("2.0.1.1", "2.0.0.7"), 0);
  EXPECT_GT(cmp("2.1.0.1", "2.0.9.9"), 0);
}

TEST(FirmwareVersion, MissingForkNumberCountsAsZero) {
  EXPECT_GT(cmp("2.0.0.1", "2.0.0"), 0);
  EXPECT_EQ(cmp("2.0.0", "2.0.0.0"), 0);
}

TEST(FirmwareVersion, LeadingVAndBuildSuffixesAreIgnored) {
  EXPECT_EQ(cmp("v2.2.15", "2.2.15"), 0);
  EXPECT_EQ(cmp("2.0.0.1-dev-feature-x-abc1234", "2.0.0.1"), 0);
  EXPECT_EQ(cmp("2.0.0.1-rc+abc", "2.0.0.1"), 0);
}

TEST(FirmwareVersion, RejectsWhatIsNotAVersion) {
  firmware_version::Version v;
  EXPECT_FALSE(firmware_version::parse("CrossPoint_Halo_2_UI_Beta_2", v));
  EXPECT_FALSE(firmware_version::parse("2.0", v));
  EXPECT_FALSE(firmware_version::parse("", v));
  EXPECT_FALSE(firmware_version::parse(nullptr, v));
}

TEST(FirmwareVersion, PreReleaseMarkers) {
  EXPECT_TRUE(firmware_version::isPreRelease("2.0.0.1-rc+abc"));
  EXPECT_TRUE(firmware_version::isPreRelease("2.0.0.1-dev-main-abc"));
  EXPECT_FALSE(firmware_version::isPreRelease("2.0.0.1"));
}

}  // namespace
