// Tests for the portable half of light-vs-dark.
//
// The first test in this project that belongs to *neither* platform. It lives
// with the shared core and is compiled into both suites, because what it checks
// -- that a JavaScript override beats the system, that releasing it hands
// control back, that listeners hear both -- is core's behaviour and would
// otherwise be asserted twice or not at all.
//
// It never asks what the system is actually set to. That is the seam, it
// differs by machine, and a test that depended on it would pass on a dark Mac
// and fail on a light one.

#include "TestHarness.h"

#include "ColorScheme.h"

#include <sstream>
#include <vector>

namespace {

// Whatever this machine is, expressed as the one it is not -- so these tests
// can flip to something that is genuinely a change.
basalt::ColorScheme opposite(basalt::ColorScheme scheme) {
  return scheme == basalt::ColorScheme::Dark ? basalt::ColorScheme::Light
                                             : basalt::ColorScheme::Dark;
}

} // namespace

TEST(color_scheme_names_match_react_natives_spelling) {
  EXPECT_EQ(std::string(basalt::colorSchemeName(basalt::ColorScheme::Light)), std::string("light"));
  EXPECT_EQ(std::string(basalt::colorSchemeName(basalt::ColorScheme::Dark)), std::string("dark"));
}

TEST(an_override_beats_the_system) {
  const auto system = basalt::systemColorScheme();
  const auto other = opposite(system);

  basalt::setColorSchemeOverride(basalt::colorSchemeName(other));
  EXPECT(basalt::effectiveColorScheme() == other);

  // And releasing it hands control back, rather than leaving the last override
  // in place -- which is what `Appearance.setColorScheme('auto')` asks for.
  basalt::clearColorSchemeOverride();
  EXPECT(basalt::effectiveColorScheme() == system);
}

TEST(listeners_hear_an_override_and_its_release) {
  std::vector<basalt::ColorScheme> heard;
  const int token = basalt::addColorSchemeObserver(
      [&heard](basalt::ColorScheme scheme) { heard.push_back(scheme); });

  const auto system = basalt::systemColorScheme();
  const auto other = opposite(system);

  basalt::setColorSchemeOverride(basalt::colorSchemeName(other));
  basalt::clearColorSchemeOverride();

  EXPECT_EQ(static_cast<int>(heard.size()), 2);
  if (heard.size() == 2) {
    EXPECT(heard[0] == other);
    EXPECT(heard[1] == system);
  }

  basalt::removeColorSchemeObserver(token);
}

// Nothing outside the process changed, so an override that changes nothing must
// still be a no-op rather than a spurious notification -- but releasing one that
// was never set must not notify either. The second is the one worth pinning:
// `Appearance.setColorScheme('auto')` on a fresh app is a perfectly ordinary
// call and should not wake every listener.
TEST(releasing_an_override_that_was_never_set_notifies_nobody) {
  int calls = 0;
  const int token =
      basalt::addColorSchemeObserver([&calls](basalt::ColorScheme) { calls++; });

  basalt::clearColorSchemeOverride();
  EXPECT_EQ(calls, 0);

  basalt::removeColorSchemeObserver(token);
}

TEST(a_removed_listener_stops_hearing) {
  int calls = 0;
  const int token =
      basalt::addColorSchemeObserver([&calls](basalt::ColorScheme) { calls++; });
  basalt::removeColorSchemeObserver(token);

  basalt::setColorSchemeOverride("dark");
  basalt::clearColorSchemeOverride();

  EXPECT_EQ(calls, 0);
}

// An empty string is how a caller says "no override" without knowing React
// Native's spelling for it. It must not be read as a scheme name and quietly
// resolve to light.
TEST(an_empty_override_clears_rather_than_meaning_light) {
  const auto system = basalt::systemColorScheme();

  basalt::setColorSchemeOverride(basalt::colorSchemeName(opposite(system)));
  basalt::setColorSchemeOverride("");

  EXPECT(basalt::effectiveColorScheme() == system);
}
