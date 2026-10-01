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
