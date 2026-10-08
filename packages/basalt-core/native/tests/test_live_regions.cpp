// Noticing that a status message changed, which is what
// `accessibilityLiveRegion` asks for.
//
// The prop is not a property of a view on either desktop -- GTK announces at a
// moment, AppKit posts a notification -- so honouring it is change detection,
// and the two rules that make it bearable rather than deafening are asserted
// here. See core/LiveRegions.h.

#include "TestHarness.h"

#include "LiveRegions.h"

#include <sstream>

using basalt::LiveRegionPoliteness;
using basalt::LiveRegionRegistry;

// Mounting a status line is not news: nothing changed, the screen appeared. A
// host that announced here would have every screen with a status message read
// itself out on arrival.
TEST(live_regions_the_first_text_is_not_announced) {
  LiveRegionRegistry registry;
  registry.setPoliteness(10, LiveRegionPoliteness::Polite);

  EXPECT(!registry.noticed(10, "Saved").has_value());
}

TEST(live_regions_a_change_is_announced_with_the_politeness_asked_for) {
  LiveRegionRegistry registry;
  registry.setPoliteness(10, LiveRegionPoliteness::Polite);
  registry.noticed(10, "Saving");

  const auto polite = registry.noticed(10, "Saved");
  EXPECT(polite.has_value());
  EXPECT(polite.value() == LiveRegionPoliteness::Polite);

  registry.setPoliteness(11, LiveRegionPoliteness::Assertive);
  registry.noticed(11, "");
  const auto assertive = registry.noticed(11, "Offline");
  EXPECT(assertive.has_value());
  EXPECT(assertive.value() == LiveRegionPoliteness::Assertive);
}

// A view re-renders for every reason under the sun, and React Native re-sends
// identical props on each mutation. A region that announced every time would
// talk over the rest of the application.
TEST(live_regions_the_same_text_twice_is_not_announced) {
  LiveRegionRegistry registry;
  registry.setPoliteness(10, LiveRegionPoliteness::Polite);
  registry.noticed(10, "Saving");
  EXPECT(registry.noticed(10, "Saved").has_value());

  EXPECT(!registry.noticed(10, "Saved").has_value());
  EXPECT(!registry.noticed(10, "Saved").has_value());
  // And a change after that is news again.
  EXPECT(registry.noticed(10, "Saved just now").has_value());
}

// A region that cleared itself has nothing to read out. Announcing an empty
// string is silence with a beat of interruption in front of it.
TEST(live_regions_clearing_the_text_announces_nothing) {
  LiveRegionRegistry registry;
  registry.setPoliteness(10, LiveRegionPoliteness::Polite);
  registry.noticed(10, "Saved");
  EXPECT(!registry.noticed(10, "").has_value());
  // But the clearing was remembered, so the text coming back is a change.
  EXPECT(registry.noticed(10, "Saved").has_value());
}

// A view that is not a live region never announces, whatever its text does.
TEST(live_regions_a_view_without_the_prop_is_silent) {
  LiveRegionRegistry registry;
  EXPECT(!registry.noticed(10, "Saving").has_value());
  EXPECT(!registry.noticed(10, "Saved").has_value());

  registry.setPoliteness(10, LiveRegionPoliteness::Polite);
  registry.setPoliteness(10, LiveRegionPoliteness::None);
  EXPECT(!registry.noticed(10, "Saved again").has_value());
}

// A region that is unmounted and comes back is new again, rather than compared
// against the text of a screen that is no longer there.
TEST(live_regions_a_region_that_goes_away_forgets_what_it_said) {
  LiveRegionRegistry registry;
  registry.setPoliteness(10, LiveRegionPoliteness::Polite);
  registry.noticed(10, "Saved");

  registry.forget(10);
  registry.setPoliteness(10, LiveRegionPoliteness::Polite);
  EXPECT(!registry.noticed(10, "Offline").has_value());
  EXPECT(registry.noticed(10, "Saved").has_value());
}

// The hosts ask `empty()` before doing any work per transaction, collecting a
// region's text being a walk of its subtree.
TEST(live_regions_a_registry_with_no_regions_is_empty) {
  LiveRegionRegistry registry;
  EXPECT(registry.empty());
  EXPECT_EQ((long)registry.tags().size(), 0L);

  registry.setPoliteness(10, LiveRegionPoliteness::Assertive);
  EXPECT(!registry.empty());
  EXPECT_EQ((long)registry.tags().size(), 1L);
  EXPECT_EQ((long)registry.tags()[0], 10L);

  registry.setPoliteness(10, LiveRegionPoliteness::None);
  EXPECT(registry.empty());
}
