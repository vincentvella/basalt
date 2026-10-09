// The `mixBlendMode` keyword table, against React Native's own parser.
//
// core/BlendModes.h is seventeen lines of switch, which is seventeen chances to
// return the wrong neighbour's name -- and a wrong name is not a build error on
// either host: it is a view that blends with the wrong mode, or stops blending.
//
// So the assertion is a round trip rather than a list compared with itself.
// React Native parses these keywords with `blendModeFromString`, which is what
// turns the app's style into the enum in the first place, so feeding the name
// back through it has to give the value it came from. Nothing in this test
// agrees with the table by construction.

#include "TestHarness.h"

#include "BlendModes.h"

#include <set>
#include <sstream>
#include <string>

using facebook::react::BlendMode;
using facebook::react::blendModeFromString;

namespace {

// The enum is dense and PlusLighter is its last member, which is what makes a
// loop over it legitimate: a mode added upstream lands inside this range and the
// count below is what notices.
constexpr int kModeCount = static_cast<int>(BlendMode::PlusLighter) + 1;

} // namespace

TEST(blend_modes_every_mode_round_trips_through_react_natives_parser) {
  for (int i = 0; i < kModeCount; i++) {
    const BlendMode mode = static_cast<BlendMode>(i);
    const char *name = basalt::blendModeName(mode);
    if (mode == BlendMode::Normal) {
      // `normal` is the default and asks for nothing, so it crosses as nothing.
      EXPECT(name == nullptr);
      continue;
    }
    EXPECT(name != nullptr);
    if (name == nullptr) {
      continue;
    }
    const std::optional<BlendMode> parsed = blendModeFromString(name);
    EXPECT(parsed.has_value());
    EXPECT(parsed.has_value() && *parsed == mode);
  }
}

// And no two modes share a name, which the round trip above would catch only if
// the parser were injective -- it is, but this says the thing directly, and it is
// the failure a copy-paste in a sixteen-line table actually produces.
TEST(blend_modes_no_two_modes_share_a_name) {
  std::set<std::string> seen;
  int named = 0;
  for (int i = 0; i < kModeCount; i++) {
    const char *name = basalt::blendModeName(static_cast<BlendMode>(i));
    if (name == nullptr) {
      continue;
    }
    named++;
    seen.insert(name);
  }
  EXPECT_EQ(named, kModeCount - 1);
  EXPECT_EQ(static_cast<int>(seen.size()), named);
}

// The count, so that a mode added upstream is a failing test here rather than a
// view that silently does not blend. CSS has sixteen separable and non-separable
// blend modes plus `normal`, and React Native carries all seventeen.
TEST(blend_modes_there_are_seventeen_of_them) {
  EXPECT_EQ(kModeCount, 17);
  // The ends of the enum, so a reordering upstream is not silently fine either.
  EXPECT(basalt::blendModeName(BlendMode::Multiply) != nullptr);
  EXPECT(std::string(basalt::blendModeName(BlendMode::Multiply)) == "multiply");
  EXPECT(std::string(basalt::blendModeName(BlendMode::PlusLighter)) == "plus-lighter");
}
