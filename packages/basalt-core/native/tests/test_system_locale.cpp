// `I18nManager.isRTL`, and the three things that decide it. See
// core/SystemLocale.h.

#include "TestHarness.h"

#include "SystemLocale.h"

#include <sstream>

using basalt::resolveRightToLeft;

TEST(rtl_follows_the_locale_when_nobody_has_said_otherwise) {
  EXPECT(resolveRightToLeft(true, std::nullopt, true));
  EXPECT(!resolveRightToLeft(true, std::nullopt, false));
}

// `forceRTL(true)` is for an app that wants to see its own right-to-left
// layout on a left-to-right machine, which is the only way most developers
// ever test one.
TEST(rtl_can_be_forced_against_the_locale) {
  EXPECT(resolveRightToLeft(true, true, false));
}

// And `forceRTL(false)` clears the force rather than forcing the other way,
// which is what React Native's own platforms do: an app turning it off goes
// back to following the system.
TEST(rtl_unforced_is_not_forced_left_to_right) {
  EXPECT(resolveRightToLeft(true, std::nullopt, true));
}

// `allowRTL(false)` wins over everything, which is how an app that has not
// been laid out for mirroring opts out on a machine whose owner reads Arabic.
TEST(rtl_can_be_refused_outright) {
  EXPECT(!resolveRightToLeft(false, std::nullopt, true));
  EXPECT(!resolveRightToLeft(false, true, true));
}
