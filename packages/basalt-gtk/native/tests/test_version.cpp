// Parsing the React Native version this host was built against.
//
// It matters because React Native's development builds compare it against the
// bundle's own version and complain loudly when they disagree, and because
// ReactCxxPlatform hardcodes 1000.0.0, so the comparison can never pass without
// this. A prerelease tag is the interesting case: read carelessly, "0.88.0-rc.2"
// turns into patch 0 and a silently discarded tag, or worse, a patch parsed
// from "0-rc".

#include "TestHarness.h"

#include "PlatformConstantsModule.h"

using basalt::DesktopPlatformConstantsModule;

TEST(version_parses_a_stable_release) {
  const auto version = DesktopPlatformConstantsModule::parseVersion("0.87.1");
  EXPECT_EQ(version.major, 0);
  EXPECT_EQ(version.minor, 87);
  EXPECT_EQ(version.patch, 1);
  EXPECT(version.prerelease.empty());
}

TEST(version_parses_the_main_placeholder) {
  // What React Native calls itself on main, and the value this whole class
  // exists to stop being reported against a release.
  const auto version = DesktopPlatformConstantsModule::parseVersion("1000.0.0");
  EXPECT_EQ(version.major, 1000);
  EXPECT_EQ(version.minor, 0);
  EXPECT_EQ(version.patch, 0);
}

TEST(version_keeps_a_prerelease_tag_out_of_the_patch) {
  const auto version = DesktopPlatformConstantsModule::parseVersion("0.88.0-rc.2");
  EXPECT_EQ(version.major, 0);
  EXPECT_EQ(version.minor, 88);
  EXPECT_EQ(version.patch, 0);
  EXPECT_EQ(version.prerelease, std::string("rc.2"));
}

TEST(version_survives_something_it_does_not_understand) {
  // Never a crash and never a wrong number: a version that cannot be parsed
  // reports zeroes, which disagrees with every bundle and so fails loudly
  // rather than matching one by accident.
  const auto version = DesktopPlatformConstantsModule::parseVersion("");
  EXPECT_EQ(version.major, 0);
  EXPECT_EQ(version.minor, 0);
  EXPECT_EQ(version.patch, 0);
}
