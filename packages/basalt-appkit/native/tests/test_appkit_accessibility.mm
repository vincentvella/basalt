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

// For the paragraph the language tests read: a tag on a view with no text has
// nowhere to go, so those build a real layout.
#include "CoreTextLayout.h"
#include "RnTextLayout.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

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

// `accessibilityViewIsModal`, which AppKit spells `accessibilityModal`: inside
// a modal element VoiceOver stops offering the siblings behind it.
TEST(a_modal_view_sets_accessibility_modal) {
  @autoreleasepool {
    RnAppKitView *view = [RnAppKitView viewWithTag:1];
    EXPECT(!view.isAccessibilityModal);
    EXPECT(std::string([view describeTree].UTF8String).find(" modal") == std::string::npos);

    view.rnAccessibleModal = YES;
    EXPECT(view.isAccessibilityModal);
    EXPECT(std::string([view describeTree].UTF8String).find(" modal") != std::string::npos);

    // And back, because a <Modal> that closes has to stop trapping the reader.
    view.rnAccessibleModal = NO;
    EXPECT(!view.isAccessibilityModal);
    EXPECT(std::string([view describeTree].UTF8String).find(" modal") == std::string::npos);
  }
}

// `experimental_accessibilityOrder`: the children a view wants read, in the
// order it gave rather than the order they mounted.
//
// AppKit expresses it by replacing `accessibilityChildren`, which is what
// VoiceOver walks in place of the view hierarchy. GTK says the same thing as an
// `aria-flowto` relation, its accessible tree following the widget tree with no
// equivalent override; both print the resolved tags, so the dumps compare.
TEST(an_accessibility_order_replaces_the_accessibility_children) {
  @autoreleasepool {
    RnAppKitView *parent = [RnAppKitView viewWithTag:10];
    RnAppKitView *first = [RnAppKitView viewWithTag:20];
    RnAppKitView *second = [RnAppKitView viewWithTag:30];

    // What AppKit answers for itself, which is not nil: `accessibilityChildren`
    // is computed from the view hierarchy when nothing overrides it, so the
    // assertion below is about the content and not about the property being
    // unset. Measured rather than assumed -- the first version of this test
    // expected nil and failed on the line before the feature was exercised.
    const NSUInteger byHierarchy = parent.accessibilityChildren.count;

    // Deliberately not the tag order: the prop exists to override it.
    [parent setRnAccessibilityOrder:@[second, first]];
    EXPECT(parent.accessibilityChildren.count == 2);
    EXPECT(parent.accessibilityChildren.firstObject == second);
    EXPECT(parent.accessibilityChildren.lastObject == first);
    EXPECT(std::string([parent describeTree].UTF8String).find("a11y-order=30,20")
           != std::string::npos);

    // Taken away again puts the view hierarchy back, which is what a view that
    // stopped asking means. The setter passes nil rather than an empty array:
    // an empty array is a claim that the view has no accessible children at
    // all, where nil is "ask the hierarchy", which is what this reads back.
    [parent setRnAccessibilityOrder:@[]];
    EXPECT_EQ(parent.accessibilityChildren.count, byHierarchy);
    EXPECT(std::string([parent describeTree].UTF8String).find("a11y-order=")
           == std::string::npos);
  }
}

// Subroles, which are how macOS says the four things a role cannot.
//
// The role maps a search field to a text field, a switch and a toggle button to
// check boxes, and a tab to a radio button -- each losing the part that says
// what it is. GTK needs none of this, those being roles of their own there,
// which is why it was a macOS entry.
TEST(a_role_that_macos_says_with_a_subrole_gets_one) {
  @autoreleasepool {
    RnAppKitView *search = viewWithRole(@"search");
    EXPECT_EQ(search.accessibilityRole, NSAccessibilityTextFieldRole);
    EXPECT_EQ(search.accessibilitySubrole, NSAccessibilitySearchFieldSubrole);

    RnAppKitView *switched = viewWithRole(@"switch");
    EXPECT_EQ(switched.accessibilityRole, NSAccessibilityCheckBoxRole);
    EXPECT_EQ(switched.accessibilitySubrole, NSAccessibilitySwitchSubrole);

    RnAppKitView *toggle = viewWithRole(@"togglebutton");
    EXPECT_EQ(toggle.accessibilityRole, NSAccessibilityCheckBoxRole);
    EXPECT_EQ(toggle.accessibilitySubrole, NSAccessibilityToggleSubrole);

    RnAppKitView *tab = viewWithRole(@"tab");
    EXPECT_EQ(tab.accessibilityRole, NSAccessibilityRadioButtonRole);
    EXPECT_EQ(tab.accessibilitySubrole, NSAccessibilityTabButtonSubrole);
  }
}

// And nothing else gets one: a subrole AppKit does not know tells VoiceOver
// less than none, and a view with no subrole is read by its role.
TEST(an_ordinary_role_gets_no_subrole) {
  @autoreleasepool {
    RnAppKitView *button = viewWithRole(@"button");
    EXPECT(button.accessibilitySubrole == nil);

    RnAppKitView *text = viewWithRole(@"text");
    EXPECT(text.accessibilitySubrole == nil);
  }
}

// A role that goes away takes its subrole with it. Without that a view whose
// prop was removed would keep announcing itself as a switch.
TEST(a_role_taken_away_takes_its_subrole) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"switch");
    EXPECT_EQ(view.accessibilitySubrole, NSAccessibilitySwitchSubrole);

    [view setRnAccessibleRole:nil];
    EXPECT(view.accessibilitySubrole == nil);
    EXPECT(!view.isAccessibilityElement);
  }
}

// And a role *changing* replaces the subrole rather than keeping the old one,
// which is the case AppKit allows and GTK cannot: a GtkAccessible's role is
// construct-only.
TEST(a_role_change_replaces_the_subrole) {
  @autoreleasepool {
    RnAppKitView *view = viewWithRole(@"switch");
    EXPECT_EQ(view.accessibilitySubrole, NSAccessibilitySwitchSubrole);

    [view setRnAccessibleRole:@"search"];
    EXPECT_EQ(view.accessibilitySubrole, NSAccessibilitySearchFieldSubrole);

    [view setRnAccessibleRole:@"button"];
    EXPECT(view.accessibilitySubrole == nil);
  }
}

// ---------------------------------------------------------------------------
// `accessibilityActions` and `onAccessibilityTap`.
//
// macOS is the only one of the three hosts that can have these:
// `NSAccessibilityCustomAction` is exactly the feature React Native's prop
// describes, GTK 4 ships no accessible-action interface at all -- its
// accessibility headers are accessible, hypertext, range and text -- and UIA
// has no custom-action pattern. backlog/accessibility.md has the measurement.
//
// Against the view rather than through a mounting transaction, the same
// division the role tests above make: what the manager reads off the props is
// its business, and whether VoiceOver is offered an action is this view's.
// ---------------------------------------------------------------------------

TEST(accessibility_actions_become_custom_actions) {
  @autoreleasepool {
    RnAppKitView *view = [RnAppKitView viewWithTag:1];
    __block NSString *performed = nil;

    [view setRnAccessibleActions:@[@"activate", @"longpress"]
                          labels:@[@"Play", @""]
                         perform:^(NSString *name) {
                           performed = name;
                         }];

    EXPECT_EQ((int)view.accessibilityCustomActions.count, 2);
    if (view.accessibilityCustomActions.count != 2) {
      // EXPECT does not stop the test, and the assertions below index the
      // array: a host that published none would crash here rather than fail.
      return;
    }
    // The label is what VoiceOver reads, and an action whose label React
    // Native left out falls back to its name rather than being announced as
    // nothing.
    EXPECT([view.accessibilityCustomActions[0].name isEqualToString:@"Play"]);
    EXPECT([view.accessibilityCustomActions[1].name isEqualToString:@"longpress"]);

    // And choosing one reports the *name*, which is what JavaScript matches
    // on -- not the label, which is prose for a listener.
    EXPECT(view.accessibilityCustomActions[0].handler());
    EXPECT(performed != nil && [performed isEqualToString:@"activate"]);
  }
}

TEST(accessibility_actions_are_cleared_when_a_view_stops_offering_them) {
  @autoreleasepool {
    RnAppKitView *view = [RnAppKitView viewWithTag:1];
    [view setRnAccessibleActions:@[@"activate"] labels:@[@"Play"] perform:^(NSString *) {
    }];
    EXPECT_EQ((int)view.accessibilityCustomActions.count, 1);

    [view setRnAccessibleActions:@[] labels:@[] perform:nil];
    EXPECT_EQ((int)view.accessibilityCustomActions.count, 0);
  }
}

TEST(accessibility_tap_is_reported_only_when_a_handler_is_attached) {
  @autoreleasepool {
    RnAppKitView *view = [RnAppKitView viewWithTag:1];

    // Without one, AppKit keeps its own behaviour: a view that never asked for
    // the prop must not start swallowing activations.
    EXPECT(![view accessibilityPerformPress]);

    __block int taps = 0;
    [view setRnAccessibleTapHandler:^{
      taps++;
    }];
    EXPECT([view accessibilityPerformPress]);
    EXPECT_EQ(taps, 1);
  }
}

// ---------------------------------------------------------------------------
// `accessibilityLanguage`, as the language of the text a screen reader reads.
//
// Published as `NSAccessibilityLanguageTextAttribute` on the attributed string
// the view answers for a range, which is macOS's per-run language and has been
// since 10.13. Not as an element attribute: macOS has none before 26, and the
// deprecated attribute API is not a way round it -- measured on macOS 26, a
// modern NSView does not list `AXDescription` among its legacy attribute names
// even with `accessibilityLabel` set, so AppKit answers a client through the
// modern protocol and an override of the old methods publishes nothing. That
// was the first implementation here and it is why these tests read the string.
//
// What they cannot pin is VoiceOver changing voice, which needs a person
// listening; see backlog/accessibility.md.
// ---------------------------------------------------------------------------

namespace {

// A view with a paragraph in it, which is what carries text to read. The
// fragment is built the way test_appkit_selection.mm builds one.
RnAppKitView *rnParagraphView(const char *text) {
  facebook::react::TextAttributes attributes;
  attributes.fontSize = 24.0F;
  facebook::react::AttributedString::Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;
  facebook::react::AttributedString string;
  string.appendFragment(std::move(fragment));

  RnAppKitView *view = [RnAppKitView viewWithTag:11];
  view.frame = NSMakeRect(0, 0, 400, 60);
  [view setRnTextLayout:basalt::buildTextLayout(string, facebook::react::ParagraphAttributes{})];
  return view;
}

// The language over the whole of what the view answered, or nil.
//
// The effective range is checked rather than just index 0, and that is not
// belt and braces: `accessibilityAttributedStringForRange:` is handed a range
// and answers a *substring*, so index 0 is the start of whatever was asked for
// and an attribute on the first character alone looks identical there. A
// sabotage that narrowed the tag to one character passed against a version of
// this helper that read index 0, which is how the check got here.
NSString *languageOf(RnAppKitView *view, NSRange range) {
  NSAttributedString *text = [view accessibilityAttributedStringForRange:range];
  if (text.length == 0) {
    return nil;
  }
  NSRange effective = NSMakeRange(0, 0);
  NSString *language = [text attribute:NSAccessibilityLanguageTextAttribute
                               atIndex:0
                        effectiveRange:&effective];
  if (language == nil || !NSEqualRanges(effective, NSMakeRange(0, text.length))) {
    return nil;
  }
  return language;
}

} // namespace

TEST(accessibility_a_language_reaches_the_text_a_screen_reader_reads) {
  @autoreleasepool {
    RnAppKitView *view = rnParagraphView("Bonjour le monde");
    [view setRnAccessibleLanguage:@"fr-CA"];

    NSString *language = languageOf(view, NSMakeRange(0, 7));
    EXPECT(language != nil && [language isEqualToString:@"fr-CA"]);
    // Over the whole run and not just its start, because VoiceOver asks for
    // the range it is about to read and that is rarely the first word.
    NSString *later = languageOf(view, NSMakeRange(8, 8));
    EXPECT(later != nil && [later isEqualToString:@"fr-CA"]);
  }
}

TEST(accessibility_no_language_leaves_the_text_unattributed) {
  @autoreleasepool {
    RnAppKitView *view = rnParagraphView("Bonjour le monde");
    EXPECT(languageOf(view, NSMakeRange(0, 7)) == nil);

    // An empty tag is nothing rather than an empty language: React Native's
    // prop defaults to "" and every paragraph would otherwise claim one.
    [view setRnAccessibleLanguage:@""];
    EXPECT(languageOf(view, NSMakeRange(0, 7)) == nil);
  }
}

// The run's own attributes have to survive. Replacing the attributed string
// instead of copying it would take the font with it, and a screen reader that
// cannot see the font cannot report the text's size or style.
TEST(accessibility_a_language_keeps_the_rest_of_the_run) {
  @autoreleasepool {
    RnAppKitView *view = rnParagraphView("Bonjour le monde");
    [view setRnAccessibleLanguage:@"fr-CA"];

    NSAttributedString *text = [view accessibilityAttributedStringForRange:NSMakeRange(0, 7)];
    EXPECT_EQ((int)text.length, 7);
    EXPECT([text attribute:NSFontAttributeName atIndex:0 effectiveRange:nullptr] != nil);
  }
}
