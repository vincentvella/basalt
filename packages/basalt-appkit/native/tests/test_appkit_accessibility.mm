// Tests for accessibility, as VoiceOver would see it.
//
// These assert the *AppKit* role, not the React Native name. The name is what
// `describeTree` reports and what the cross-platform diff compares; whether the
// mapping onto NSAccessibility actually happened is a platform question, and
// this is where a platform question belongs. The GTK suite's
// test_accessibility.cpp asserts the same thing about GtkAccessibleRole.
//
// Against the view directly, with no Fabric: the mapping is a pure function of
// a string, and testing it through a mounting transaction would prove the same
// thing more slowly and less clearly.

#include "TestHarness.h"

#import "RnAppKitView.h"

#include <sstream>

namespace {

RnAppKitView *viewWithRole(NSString *role) {
  RnAppKitView *view = [RnAppKitView viewWithTag:1];
  [view setRnAccessibleRole:role];
  return view;
}

} // namespace

TEST(accessibility_maps_react_native_roles_onto_appkit) {
  @autoreleasepool {
    EXPECT(viewWithRole(@"button").accessibilityRole == NSAccessibilityButtonRole);
    EXPECT(viewWithRole(@"text").accessibilityRole == NSAccessibilityStaticTextRole);
    EXPECT(viewWithRole(@"image").accessibilityRole == NSAccessibilityImageRole);
    EXPECT(viewWithRole(@"link").accessibilityRole == NSAccessibilityLinkRole);
    EXPECT(viewWithRole(@"checkbox").accessibilityRole == NSAccessibilityCheckBoxRole);
    EXPECT(viewWithRole(@"list").accessibilityRole == NSAccessibilityListRole);
    EXPECT(viewWithRole(@"adjustable").accessibilityRole == NSAccessibilitySliderRole);
  }
}

// A role AppKit has no equivalent for, and one it has never heard of. Both
// become a group rather than a guess: a wrong role is worse for a screen reader
// than a vague one, because it makes the view announce itself as something it
// is not.
TEST(accessibility_falls_back_to_a_group) {
  @autoreleasepool {
    EXPECT(viewWithRole(@"header").accessibilityRole == NSAccessibilityGroupRole);
    EXPECT(viewWithRole(@"somethingnobodyhasheardof").accessibilityRole ==
           NSAccessibilityGroupRole);
  }
}

// A plain <View> is scenery. Leaving every one of them in the tree would bury
// the handful that mean something under hundreds that do not.
TEST(accessibility_leaves_plain_views_out_of_the_tree) {
  @autoreleasepool {
    RnAppKitView *plain = [RnAppKitView viewWithTag:1];
    [plain setRnAccessibleRole:nil];
    EXPECT(!plain.isAccessibilityElement);

    // ...but a role puts it back in.
    EXPECT(viewWithRole(@"button").isAccessibilityElement);

    // ...and so does a label on its own, which is the common case for an
    // icon-only <Pressable>.
    RnAppKitView *labelled = [RnAppKitView viewWithTag:2];
    [labelled setRnAccessibleRole:nil];
    [labelled setRnAccessibleLabel:@"Close" hint:nil];
    EXPECT(labelled.isAccessibilityElement);
  }
}

// `none` and `presentation` mean "do not announce this", which is a different
// thing from having no role: the app asked for silence explicitly.
TEST(accessibility_honours_a_presentational_role) {
  @autoreleasepool {
    EXPECT(!viewWithRole(@"none").isAccessibilityElement);
    EXPECT(!viewWithRole(@"presentation").isAccessibilityElement);
  }
}

TEST(accessibility_label_and_hint_reach_appkit) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"button");
    [view setRnAccessibleLabel:@"Save" hint:@"Writes the file to disk"];

    EXPECT([view.accessibilityLabel isEqualToString:@"Save"]);
    // The hint is help, not value: React Native's hint is the supplementary
    // description, which on a Mac is what a help tag carries.
    EXPECT([view.accessibilityHelp isEqualToString:@"Writes the file to disk"]);

    // Empty clears rather than setting an empty string, which VoiceOver would
    // read as a pause.
    [view setRnAccessibleLabel:@"" hint:@""];
    EXPECT(view.accessibilityLabel == nil);
    EXPECT(view.accessibilityHelp == nil);
  }
}

TEST(accessibility_states_reach_appkit) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"checkbox");

    [view setRnAccessibleStateDisabled:RnAppKitAccessibleTrue
                               checked:RnAppKitAccessibleTrue
                              selected:RnAppKitAccessibleTrue
                              expanded:RnAppKitAccessibleTrue
                                  busy:RnAppKitAccessibleUnset];

    EXPECT(!view.isAccessibilityEnabled);
    EXPECT(view.isAccessibilitySelected);
    EXPECT(view.isAccessibilityExpanded);
    // Checked is a value on a Mac, the way a checkbox reports one, rather than
    // a state of its own.
    EXPECT([view.accessibilityValue isEqual:@YES]);

    [view setRnAccessibleStateDisabled:RnAppKitAccessibleFalse
                               checked:RnAppKitAccessibleFalse
                              selected:RnAppKitAccessibleFalse
                              expanded:RnAppKitAccessibleFalse
                                  busy:RnAppKitAccessibleUnset];

    EXPECT(view.isAccessibilityEnabled);
    EXPECT(!view.isAccessibilitySelected);
    EXPECT([view.accessibilityValue isEqual:@NO]);
  }
}

// Unset is not false. A view whose app never mentioned `disabled` must keep
// AppKit's default rather than being marked enabled, because the two are
// different claims and only one of them was made.
TEST(accessibility_unset_leaves_appkit_alone) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"button");
    view.accessibilityEnabled = NO;

    [view setRnAccessibleStateDisabled:RnAppKitAccessibleUnset
                               checked:RnAppKitAccessibleUnset
                              selected:RnAppKitAccessibleUnset
                              expanded:RnAppKitAccessibleUnset
                                  busy:RnAppKitAccessibleUnset];

    EXPECT(!view.isAccessibilityEnabled);
    EXPECT(view.accessibilityValue == nil);
  }
}

TEST(accessibility_hidden_takes_a_view_out_of_the_tree) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"button");
    EXPECT(view.isAccessibilityElement);

    [view setRnAccessibleHidden:YES];
    EXPECT(!view.isAccessibilityElement);
  }
}

// The dump reports React Native's name, which is the whole reason the view
// keeps one: the two hosts' trees are diffed line by line.
TEST(accessibility_role_is_reported_in_the_tree) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"button");
    const std::string described = [view describeTree].UTF8String;
    EXPECT(described.find("role=button") != std::string::npos);

    RnAppKitView *plain = [RnAppKitView viewWithTag:2];
    [plain setRnAccessibleRole:nil];
    EXPECT(std::string([plain describeTree].UTF8String).find("role=") == std::string::npos);
  }
}

// accessibilityValue: a range, a position in it, and a text form a screen reader
// prefers over the number. The parts are independent in React Native, which is
// the difficulty: an absent one must stay absent rather than become zero.

TEST(accessibility_value_reaches_appkit) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"adjustable");

    [view setRnAccessibleValueMin:@0 max:@10 now:@7 text:nil];

    EXPECT([view.accessibilityMinValue isEqual:@0]);
    EXPECT([view.accessibilityMaxValue isEqual:@10]);
    EXPECT([view.accessibilityValue isEqual:@7]);
    EXPECT(view.isAccessibilityElement);
  }
}

TEST(an_accessibility_value_without_a_range_does_not_invent_one) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"adjustable");

    // `now` alone. A view that never said what its range is must not be
    // announced as sitting at the bottom of one.
    [view setRnAccessibleValueMin:nil max:nil now:@3 text:nil];

    EXPECT(view.accessibilityMinValue == nil);
    EXPECT(view.accessibilityMaxValue == nil);
    EXPECT([view.accessibilityValue isEqual:@3]);
  }
}

TEST(a_text_accessibility_value_is_preferred_over_the_number) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"adjustable");

    // "Tuesday" reads better than "3", which is why React Native carries both.
    [view setRnAccessibleValueMin:@0 max:@6 now:@3 text:@"Tuesday"];

    EXPECT([view.accessibilityValue isEqual:@"Tuesday"]);
    // The range still stands: a screen reader may announce position as well.
    EXPECT([view.accessibilityMaxValue isEqual:@6]);
  }
}

TEST(an_explicit_accessibility_value_wins_over_a_checked_state) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"checkbox");

    // AppKit carries both in one property, so the order matters and this is the
    // assertion that pins it: the app's own value is not overwritten by a value
    // derived from a checkbox.
    [view setRnAccessibleStateDisabled:RnAppKitAccessibleUnset
                               checked:RnAppKitAccessibleTrue
                              selected:RnAppKitAccessibleUnset
                              expanded:RnAppKitAccessibleUnset
                                  busy:RnAppKitAccessibleUnset];
    EXPECT([view.accessibilityValue isEqual:@YES]);

    [view setRnAccessibleValueMin:nil max:nil now:nil text:@"Mixed"];
    EXPECT([view.accessibilityValue isEqual:@"Mixed"]);
  }
}

TEST(a_view_with_no_accessibility_value_keeps_none) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"adjustable");

    [view setRnAccessibleValueMin:nil max:nil now:nil text:nil];

    EXPECT(view.accessibilityMinValue == nil);
    EXPECT(view.accessibilityMaxValue == nil);
    EXPECT(view.accessibilityValue == nil);
  }
}

TEST(a_role_can_change_after_the_view_is_mounted) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"button");
    EXPECT([view.accessibilityRole isEqual:NSAccessibilityButtonRole]);

    // AppKit's role is a settable property, so unlike GTK, where it is
    // construct-only and the backlog records the limitation, the role itself
    // follows. This pins that rather than assuming it: the mounting manager
    // applies the role on every props update, and a change to apply it only at
    // creation would be invisible without this.
    [view setRnAccessibleRole:@"checkbox"];
    EXPECT([view.accessibilityRole isEqual:NSAccessibilityCheckBoxRole]);
  }
}

// `accessibilityLabelledBy`: another view, named by its `nativeID`, whose text
// names this one.
//
// The first half is the mapping, which is this file's business and is one
// property: AppKit's `accessibilityTitleUIElement` is a single element where the
// prop and GTK's relation are both lists, so the first is used.
//
// The second half -- resolving a `nativeID` to a view, and resolving it *late*,
// since Fabric mounts a field before the label that follows it -- is in
// core/LabelRegistry.h with its own tests, and the mounting manager's use of it
// is checked through a real transaction in test_appkit_mounting.mm.
TEST(accessibility_labelled_by_points_at_the_labelling_view) {
  @autoreleasepool {
    RnAppKitView *field = [RnAppKitView viewWithTag:10];
    RnAppKitView *label = [RnAppKitView viewWithTag:20];

    EXPECT(field.accessibilityTitleUIElement == nil);
    [field setRnLabelledBy:@[label]];
    EXPECT(field.accessibilityTitleUIElement == label);
    // And it is worth announcing now: its name lives on another view, so a field
    // left out of the tree would be unreachable with the label read out beside
    // nothing.
    EXPECT(field.isAccessibilityElement);

    // Taken off again, which is what an unmounted label leaves behind.
    [field setRnLabelledBy:@[]];
    EXPECT(field.accessibilityTitleUIElement == nil);
  }
}

// Several labels: AppKit takes the first and the rest are still reported, so the
// two hosts' dumps match and the difference stays in what each platform does.
TEST(accessibility_labelled_by_takes_the_first_of_several) {
  @autoreleasepool {
    RnAppKitView *field = [RnAppKitView viewWithTag:10];
    RnAppKitView *heading = [RnAppKitView viewWithTag:20];
    RnAppKitView *hint = [RnAppKitView viewWithTag:30];

    [field setRnLabelledBy:@[heading, hint]];
    EXPECT(field.accessibilityTitleUIElement == heading);

    const std::string described = [field describeTree].UTF8String;
    EXPECT(described.find("labelled-by=20,30") != std::string::npos);
  }
}

// `testID`, which on this platform is `accessibilityIdentifier`: what XCTest,
// Appium and the Accessibility Inspector look a view up by, and the nearest
// thing macOS has to UIA's automation id.
TEST(a_test_id_becomes_the_accessibility_identifier) {
  @autoreleasepool {
    RnAppKitView *view = [RnAppKitView viewWithTag:1];
    EXPECT(view.accessibilityIdentifier == nil || view.accessibilityIdentifier.length == 0);

    view.rnTestId = @"save-button";
    // Through a nil-safe read, because the sabotage that proves this test --
    // taking the identifier away -- would otherwise make `std::string(nullptr)`
    // crash the whole suite instead of failing this one test. A test that dies
    // reports nothing about the other 543.
    const char *identifier = view.accessibilityIdentifier.UTF8String;
    EXPECT_EQ(std::string(identifier != nullptr ? identifier : ""), std::string("save-button"));
    // And React Native's spelling in the dump, which is what the cross-platform
    // diff compares.
    EXPECT(std::string([view describeTree].UTF8String).find("testid=save-button")
           != std::string::npos);

    // Taken away again: nil rather than an empty identifier, because a runner
    // searching for "" would otherwise match the first view it saw.
    view.rnTestId = @"";
    EXPECT(view.accessibilityIdentifier == nil);
    EXPECT(std::string([view describeTree].UTF8String).find("testid=") == std::string::npos);
  }
}
