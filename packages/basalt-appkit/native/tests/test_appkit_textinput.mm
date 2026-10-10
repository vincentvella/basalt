// Tests for <TextInput>'s controlled-value loop.
//
// The hard part of a text field is not typing. React Native's <TextInput> is a
// controlled component: JavaScript owns the value, the field reports every
// change, JavaScript re-renders and pushes the value back down as a prop. If
// applying that prop looks like the user typing, the two chase each other
// forever; if a prop that arrives late is applied anyway, a fast typist watches
// characters reorder themselves.
//
// Both of those are state machines rather than AppKit, and both are tested
// here through real mutations, because they are only wrong in combination.
//
// Props are built through React Native's own RawProps parser rather than by
// assigning fields: TextInputProps' members are const and only reachable that
// way, which is also how the GTK suite builds them.

#include "TestHarness.h"
#include "EventRecorder.h"

#import "AppKitMountingManager.h"
#import "AppKitTextPeer.h"
#import "RnAppKitView.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/RawProps.h>
#include <react/renderer/core/RawPropsParser.h>

#include <sstream>

using facebook::react::ContextContainer;
using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::PropsParserContext;
using facebook::react::RawProps;
using facebook::react::RawPropsParser;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TextInputEventEmitter;
using facebook::react::TextInputProps;
using facebook::react::TransactionTelemetry;

namespace {

constexpr SurfaceId kSurfaceId = 1;

// One parser, prepared once: preparing walks every prop TextInputProps knows,
// which is not something to redo per test. RawProps has to be parsed before it
// can be read -- constructing one and handing it straight to a Props
// constructor trips an assertion inside RawProps rather than producing empty
// props, which is a better failure than it sounds.
const RawPropsParser &textInputParser() {
  static const RawPropsParser parser = []() {
    RawPropsParser prepared;
    prepared.prepare<TextInputProps>();
    return prepared;
  }();
  return parser;
}

ShadowView makeTextInput(Tag tag,
                         folly::dynamic props,
                         facebook::react::SharedEventEmitter emitter = nullptr) {
  static const auto contextContainer = std::make_shared<const ContextContainer>();
  PropsParserContext context{kSurfaceId, *contextContainer};

  RawProps rawProps{std::move(props)};
  rawProps.parse(textInputParser());
  auto parsed = std::make_shared<const TextInputProps>(context, TextInputProps{}, rawProps);

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0}, .size = {.width = 200, .height = 40}};

  ShadowView view;
  view.componentName = "TextInput";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = parsed;
  view.layoutMetrics = metrics;
  // Null for most tests, which is what a hand-built shadow view carries and
  // why nothing here could see an event until EventRecorder.h.
  view.eventEmitter = std::move(emitter);
  return view;
}

void apply(basalt::AppKitMountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

// Mounts one TextInput under the surface root and hands back its view.
RnAppKitView *mountField(basalt::AppKitMountingManager &manager,
                         Tag tag,
                         folly::dynamic props,
                         facebook::react::SharedEventEmitter emitter = nullptr) {
  RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
  [root setRnFrameX:0 y:0 width:400 height:300];

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeTextInput(tag, props, emitter)));
  mutations.push_back(
      ShadowViewMutation::InsertMutation(kSurfaceId, makeTextInput(tag, props, emitter), 0));
  apply(manager, std::move(mutations));

  return manager.viewForTag(tag);
}

// Through the seam, because a multiline peer is an NSTextView and asking it
// for NSTextField's properties does not compile.
NSView *fieldOf(RnAppKitView *view) {
  return view.rnEditable;
}

// What a person typing does, as far as the manager is concerned: the field's
// value changes and the delegate is told. AppKit posts that notification from
// NSControl and there is no field editor here to post it, so the test does what
// AppKit would -- which is also exactly the path a real keystroke takes.
void typeInto(NSView *field, NSString *text) {
  RnPeerSetText(field, [RnPeerText(field) stringByAppendingString:text]);
  // The notification a real NSTextField posts. Single line only: this helper
  // stands in for AppKit, and a multiline peer posts NSTextDidChangeNotification
  // through NSTextViewDelegate instead.
  [((NSTextField *)field).delegate
      controlTextDidChange:[NSNotification notificationWithName:NSControlTextDidChangeNotification
                                                         object:field]];
}

// What AppKit posts when editing ends -- the field losing focus, or the user
// pressing Tab. Single line only, for the same reason typeInto is.
void endEditing(NSView *field) {
  [((NSTextField *)field).delegate
      controlTextDidEndEditing:[NSNotification
                                   notificationWithName:NSControlTextDidEndEditingNotification
                                                 object:field]];
}

// One Update mutation for a field, which is what every re-render produces.
void updateField(basalt::AppKitMountingManager &manager,
                 Tag tag,
                 folly::dynamic before,
                 folly::dynamic after) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::UpdateMutation(
      makeTextInput(tag, std::move(before)), makeTextInput(tag, std::move(after)), kSurfaceId));
  apply(manager, std::move(mutations));
}

} // namespace

TEST(textinput_mounts_a_real_field) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 10, folly::dynamic::object("text", "hello"));

    NSView *field = fieldOf(view);
    EXPECT(field != nil);
    EXPECT([RnPeerText(field) isEqualToString:@"hello"]);
    // The view behind it draws the background and the border, so the field
    // itself must paint nothing -- an NSTextField's own bezel would sit on top
    // of whatever the style asked for.
    EXPECT(!((NSTextField *)field).isBordered);
    EXPECT(!((NSTextField *)field).drawsBackground);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// Applying the `text` prop must not look like the user typing. If it did, the
// change reported would provoke a re-render that sets it again, forever.
TEST(textinput_applying_a_prop_is_not_typing) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 20, folly::dynamic::object("text", "one"));

    ShadowViewMutationList update;
    update.push_back(ShadowViewMutation::UpdateMutation(
        makeTextInput(20, folly::dynamic::object("text", "one")),
        makeTextInput(20, folly::dynamic::object("text", "two")),
        kSurfaceId));
    apply(manager, std::move(update));

    EXPECT([RnPeerText(fieldOf(view)) isEqualToString:@"two"]);

    // No emitter is attached to these hand-built shadow views, so what this
    // pins is that the round trip did not throw or recurse -- the loop it would
    // enter has no exit, so a failure here is a hang rather than a wrong value.
    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// The field is placed inside the content inset, so paddingHorizontal on a field
// means what it means on a <View>. Without this the text sits flush against the
// border, ignoring the style.
// An uncontrolled field keeps what was typed into it.
//
// React Native re-sends `mostRecentEventCount` on every change, which is an
// Update mutation on its own, and `text` for an uncontrolled field is the empty
// string forever -- `TextInput.js` sends `text={value ?? defaultValue}` and an
// uncontrolled field with no default sends undefined. So a manager that applies
// the prop whenever it differs from the field wipes it on the user's second
// keystroke. Applying it only when the *prop* changes is what makes this work;
// found on Windows in phase 46, and this platform had the same bug.
TEST(textinput_keeps_what_was_typed_into_an_uncontrolled_field) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 30, folly::dynamic::object("text", ""));

    typeInto(fieldOf(view), @"abc");
    EXPECT([RnPeerText(fieldOf(view)) isEqualToString:@"abc"]);

    // The re-render an uncontrolled field produces: no text, a bumped count.
    updateField(manager,
                30,
                folly::dynamic::object("text", ""),
                folly::dynamic::object("text", "")("mostRecentEventCount", 1));
    EXPECT([RnPeerText(fieldOf(view)) isEqualToString:@"abc"]);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// The same staleness rule the commands already follow, applied to the prop. A
// value React rendered before the user's latest keystroke must not be applied,
// or a fast typist watches characters reorder themselves -- and it must not be
// *forgotten* either, or the value is lost once JavaScript catches up.
TEST(textinput_drops_a_text_prop_older_than_what_was_typed) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 31, folly::dynamic::object("text", ""));

    typeInto(fieldOf(view), @"fast");

    updateField(manager,
                31,
                folly::dynamic::object("text", ""),
                folly::dynamic::object("text", "F")("mostRecentEventCount", 0));
    EXPECT([RnPeerText(fieldOf(view)) isEqualToString:@"fast"]);

    updateField(manager,
                31,
                folly::dynamic::object("text", ""),
                folly::dynamic::object("text", "F")("mostRecentEventCount", 1));
    EXPECT([RnPeerText(fieldOf(view)) isEqualToString:@"F"]);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// A `selection` prop on a field that has no focus.
//
// On AppKit a selection lives on the *field editor* -- a shared NSTextView the
// window lends to whichever field is first responder -- so an unfocused field
// has nowhere to put one. Props routinely arrive before focus does, so the
// only correct behaviour is to leave the text alone and not reach through a
// nil editor, which is what this pins.
//
// The other half, that a selection actually lands, needs a field editor and so
// needs a window, which this suite deliberately does not have. GTK's
// test_textinput.cpp covers that half on a host where the selection belongs to
// the widget itself.
TEST(textinput_a_selection_prop_without_focus_is_harmless) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 20, folly::dynamic::object("text", "abcdef"));

    ShadowViewMutationList update;
    update.push_back(ShadowViewMutation::UpdateMutation(
        makeTextInput(20, folly::dynamic::object("text", "abcdef")),
        makeTextInput(20,
                      folly::dynamic::object("text", "abcdef")(
                          "selection", folly::dynamic::object("start", 2)("end", 5))),
        kSurfaceId));
    apply(manager, std::move(update));

    EXPECT(RnPeerEditor(fieldOf(view)) == nil);
    EXPECT([RnPeerText(fieldOf(view)) isEqualToString:@"abcdef"]);
  }
}

TEST(textinput_sits_inside_the_content_inset) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    ShadowView shadowView = makeTextInput(30, folly::dynamic::object("text", ""));
    shadowView.layoutMetrics.contentInsets = {.left = 12, .top = 4, .right = 12, .bottom = 4};

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(shadowView));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, shadowView, 0));
    apply(manager, std::move(mutations));

    NSView *field = fieldOf(manager.viewForTag(30));
    EXPECT(field != nil);
    EXPECT_NEAR(field.frame.origin.x, 12.0, 0.001);
    EXPECT_NEAR(field.frame.origin.y, 4.0, 0.001);
    // 200 wide less 12 either side; 40 tall less 4 top and bottom.
    EXPECT_NEAR(field.frame.size.width, 176.0, 0.001);
    EXPECT_NEAR(field.frame.size.height, 32.0, 0.001);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// secureTextEntry is a different *class* on AppKit, not a property, so turning
// it on has to rebuild the field -- and the text has to survive that.
TEST(textinput_secure_entry_rebuilds_the_field) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 40, folly::dynamic::object("text", "shh"));

    EXPECT(![fieldOf(view) isKindOfClass:[NSSecureTextField class]]);

    ShadowViewMutationList update;
    update.push_back(ShadowViewMutation::UpdateMutation(
        makeTextInput(40, folly::dynamic::object("text", "shh")),
        makeTextInput(40, folly::dynamic::object("text", "shh")("secureTextEntry", true)),
        kSurfaceId));
    apply(manager, std::move(update));

    EXPECT([fieldOf(view) isKindOfClass:[NSSecureTextField class]]);
    EXPECT([RnPeerText(fieldOf(view)) isEqualToString:@"shh"]);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// `editable: false` and `readOnly: true` are the same request spelled two ways,
// and React Native honours both.
TEST(textinput_honours_both_spellings_of_read_only) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;

    RnAppKitView *plain = mountField(manager, 50, folly::dynamic::object("text", ""));
    EXPECT(((NSTextField *)fieldOf(plain)).isEditable);
    manager.destroySurfaceRoot(kSurfaceId);

    basalt::AppKitMountingManager second;
    RnAppKitView *locked =
        mountField(second, 51, folly::dynamic::object("text", "")("editable", false));
    EXPECT(!((NSTextField *)fieldOf(locked)).isEditable);
    second.destroySurfaceRoot(kSurfaceId);

    basalt::AppKitMountingManager third;
    RnAppKitView *readOnly =
        mountField(third, 52, folly::dynamic::object("text", "")("readOnly", true));
    EXPECT(!((NSTextField *)fieldOf(readOnly)).isEditable);
    third.destroySurfaceRoot(kSurfaceId);
  }
}

// The field's contents have to show up in the tree dump: they live in the
// NSTextField, not in anything the view draws, so they would otherwise be
// invisible to every test that reads the tree -- and to the cross-platform
// diff, which is where a difference in them would matter most.
TEST(textinput_contents_are_reported_in_the_tree) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    ShadowViewMutationList mutations;
    mutations.push_back(
        ShadowViewMutation::CreateMutation(makeTextInput(60, folly::dynamic::object("text", "abc"))));
    mutations.push_back(ShadowViewMutation::InsertMutation(
        kSurfaceId, makeTextInput(60, folly::dynamic::object("text", "abc")), 0));
    apply(manager, std::move(mutations));

    const std::string described = [root describeTree].UTF8String;
    EXPECT(described.find("editable=\"abc\"") != std::string::npos);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// A command for a field that was never mounted, and one that is not ours. Both
// have to be survivable: a focus arriving between a Remove and its Delete is
// exactly this shape.
// --- multiline ---------------------------------------------------------------
//
// Deliberately the same four questions test_textinput.cpp asks of GTK, because
// both hosts now reach their peer through a seam and what these really check
// is whether the two seams agree.

TEST(textinput_multiline_builds_a_text_view) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view =
        mountField(manager, 60, folly::dynamic::object("text", "hello")("multiline", true));

    NSView *peer = fieldOf(view);
    EXPECT(peer != nil);
    EXPECT(RnPeerIsMultiline(peer));
    EXPECT([peer isKindOfClass:[NSTextView class]]);

    // And a plain field is still an NSTextField, which is the half that must
    // not have moved.
    RnAppKitView *single = mountField(manager, 61, folly::dynamic::object("text", "hello"));
    EXPECT(!RnPeerIsMultiline(fieldOf(single)));
    EXPECT([fieldOf(single) isKindOfClass:[NSTextField class]]);
  }
}

TEST(textinput_multiline_text_round_trips) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view =
        mountField(manager, 62, folly::dynamic::object("text", "one")("multiline", true));
    EXPECT([RnPeerText(fieldOf(view)) isEqualToString:@"one"]);

    // Newlines are the point, and are what a single-line field would refuse.
    ShadowViewMutationList update;
    update.push_back(ShadowViewMutation::UpdateMutation(
        makeTextInput(62, folly::dynamic::object("text", "one")("multiline", true)),
        makeTextInput(62, folly::dynamic::object("text", "one\ntwo")("multiline", true)),
        kSurfaceId));
    apply(manager, std::move(update));
    EXPECT([RnPeerText(fieldOf(view)) isEqualToString:@"one\ntwo"]);
  }
}

TEST(textinput_multiline_owns_its_selection) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view =
        mountField(manager, 63, folly::dynamic::object("text", "abcdef")("multiline", true));

    // The one place multiline is simpler: an NSTextView *is* the editor, so it
    // has a selection whether or not anything has focus -- where an unfocused
    // NSTextField has no field editor and so none at all.
    NSView *peer = fieldOf(view);
    EXPECT(RnPeerEditor(peer) != nil);

    RnPeerSetSelection(peer, NSMakeRange(1, 3));
    const NSRange selected = RnPeerSelection(peer);
    EXPECT_EQ((long)selected.location, 1L);
    EXPECT_EQ((long)selected.length, 3L);
  }
}

TEST(textinput_switching_multiline_rebuilds_the_peer) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 64, folly::dynamic::object("text", "hello"));
    EXPECT(!RnPeerIsMultiline(fieldOf(view)));

    // Not a property that can be flipped, so the widget is replaced -- and the
    // text has to survive that, because React did not ask for it to be cleared.
    ShadowViewMutationList update;
    update.push_back(ShadowViewMutation::UpdateMutation(
        makeTextInput(64, folly::dynamic::object("text", "hello")),
        makeTextInput(64, folly::dynamic::object("text", "hello")("multiline", true)),
        kSurfaceId));
    apply(manager, std::move(update));

    EXPECT(RnPeerIsMultiline(fieldOf(view)));
    EXPECT([RnPeerText(fieldOf(view)) isEqualToString:@"hello"]);
  }
}

// A single-line field clips rather than wrapping, which is what it does on
// every other platform -- it scrolls horizontally instead. An NSTextField
// wraps by default, and that was invisible here until a field held more than
// it could show.
TEST(textinput_single_line_does_not_wrap) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 65, folly::dynamic::object("text", "hello"));
    NSTextField *field = (NSTextField *)fieldOf(view);
    EXPECT(field.usesSingleLineMode);
    EXPECT(!field.cell.wraps);
  }
}

// maxLength, which AppKit offers nothing for on either peer.
TEST(textinput_max_length_is_enforced_on_both_peers) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;

    // A single-line field takes a formatter, which is what AppKit offers.
    RnAppKitView *single =
        mountField(manager, 70, folly::dynamic::object("text", "")("maxLength", 5));
    NSTextField *field = (NSTextField *)fieldOf(single);
    EXPECT(field.formatter != nil);
    NSString *replacement = nil;
    NSString *error = nil;
    EXPECT([field.formatter isPartialStringValid:@"abcde"
                                newEditingString:&replacement
                                errorDescription:&error]);
    EXPECT(![field.formatter isPartialStringValid:@"abcdef"
                                 newEditingString:&replacement
                                 errorDescription:&error]);

    // A multiline one has no formatter, so the limit is asked about instead --
    // refused whole rather than truncated.
    RnAppKitView *multi = mountField(
        manager, 71, folly::dynamic::object("text", "abcd")("multiline", true)("maxLength", 5));
    NSView *peer = fieldOf(multi);
    EXPECT(RnPeerAllowsChange(peer, NSMakeRange(4, 0), @"e"));
    EXPECT(!RnPeerAllowsChange(peer, NSMakeRange(4, 0), @"efgh"));
    // Replacing a selection makes room, so length alone is not the question.
    EXPECT(RnPeerAllowsChange(peer, NSMakeRange(0, 4), @"vwxyz"));

    // And no limit means no limit.
    RnAppKitView *free = mountField(manager, 72, folly::dynamic::object("text", "")("multiline", true));
    EXPECT(RnPeerAllowsChange(fieldOf(free), NSMakeRange(0, 0), @"as long as you like"));
  }
}

// A placeholder on the peer that has no property for it.
TEST(textinput_multiline_takes_a_placeholder) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(
        manager, 73,
        folly::dynamic::object("text", "")("multiline", true)("placeholder", "type here"));

    NSAttributedString *placeholder = RnPeerPlaceholder(fieldOf(view));
    EXPECT(placeholder != nil);
    EXPECT([placeholder.string isEqualToString:@"type here"]);

    // And the single-line peer still takes its own, through the property
    // AppKit gives it.
    RnAppKitView *single =
        mountField(manager, 74, folly::dynamic::object("text", "")("placeholder", "type here"));
    EXPECT([RnPeerPlaceholder(fieldOf(single)).string isEqualToString:@"type here"]);
  }
}

TEST(textinput_commands_survive_an_unknown_tag) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    mountField(manager, 70, folly::dynamic::object("text", ""));

    manager.applyCommand(9999, "focus", folly::dynamic::array());
    manager.applyCommand(70, "somethingElse", folly::dynamic::array());
    manager.applyCommand(70, "blur", folly::dynamic::array());

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// --- What React actually hears ---------------------------------------------
//
// Everything above pins the field's own state. These pin the events, which
// nothing below the end-to-end suite could see until EventRecorder.h -- see
// that header for why a stub emitter cannot do this and a real dispatcher can.

TEST(textinput_change_reaches_the_emitter) {
  @autoreleasepool {
    basalt::testing::EventRecorder recorder;
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager,
                                    80,
                                    folly::dynamic::object("text", ""),
                                    recorder.emitter<TextInputEventEmitter>());

    typeInto(fieldOf(view), @"a");

    const auto seen = recorder.seen();
    EXPECT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen.empty() ? std::string{} : seen[0], std::string{"topChange"});

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// A genuine ordering assertion, and the first one in either suite: one call
// site emits two events, and React Native's contract is blur before
// endEditing. Nothing checked that they both fired, never mind in which
// order.
TEST(textinput_blur_reports_blur_before_end_editing) {
  @autoreleasepool {
    basalt::testing::EventRecorder recorder;
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager,
                                    81,
                                    folly::dynamic::object("text", "hello"),
                                    recorder.emitter<TextInputEventEmitter>());

    endEditing(fieldOf(view));

    const auto seen = recorder.seen();
    EXPECT_EQ(seen.size(), 2u);
    if (seen.size() == 2) {
      EXPECT_EQ(seen[0], std::string{"topBlur"});
      EXPECT_EQ(seen[1], std::string{"topEndEditing"});
    }

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// A field nobody typed into reports nothing. Worth pinning because the
// recorder would be just as quiet if it were wired up wrong, and every
// assertion above rests on it hearing what it should.
TEST(textinput_mounting_alone_reports_nothing) {
  @autoreleasepool {
    basalt::testing::EventRecorder recorder;
    basalt::AppKitMountingManager manager;
    mountField(manager,
               82,
               folly::dynamic::object("text", "hello"),
               recorder.emitter<TextInputEventEmitter>());

    EXPECT(recorder.seen().empty());

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// --- Focus and keys, through a real window ----------------------------------
//
// These need a window, which the rest of this file does not: focus is
// AppKit's to give, and the manager watches keys with a local NSEvent monitor
// rather than through the delegate -- deliberately, so that it sees the key
// on its way to the responder chain, before the edit and therefore before
// onChange.
//
// The window is never ordered front, like the ones in
// test_appkit_titlebar.mm, and that is also the limit of what can be pinned
// here. NSApp routes a key to the *key* window, and a window belonging to a
// process that is not active cannot become key -- `makeKeyWindow` on one
// leaves `isKeyWindow` false. So the monitor can be driven and the edit it
// precedes cannot, and onKeyPress-before-onChange stays the end-to-end
// suite's to check rather than being arranged here and called a test.
//
// Payload values are out of reach for the same reason everywhere else: which
// key it was lives in a jsi::Object. What is checked is that a key press is
// reported, and for which keys it is not.

namespace {

// A field that is first responder in a window nobody can see.
NSWindow *windowAround(basalt::AppKitMountingManager &manager, NSView *field) {
  NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300)
                                                 styleMask:NSWindowStyleMaskTitled
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  [window.contentView addSubview:manager.getSurfaceRoot(kSurfaceId)];
  [window makeFirstResponder:field];
  return window;
}

// One key, through NSApp -- which is what invokes a local event monitor.
// Posted and pulled rather than handed straight to sendEvent: so that it
// travels the queue a real keystroke travels.
void pressKey(NSWindow *window, unsigned short keyCode, NSString *characters) {
  NSEvent *key = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                  location:NSZeroPoint
                             modifierFlags:0
                                 timestamp:0
                              windowNumber:window.windowNumber
                                   context:nil
                                characters:characters
               charactersIgnoringModifiers:characters
                                 isARepeat:NO
                                   keyCode:keyCode];
  [NSApp postEvent:key atStart:YES];
  NSEvent *pulled = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:[NSDate distantPast]
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES];
  if (pulled != nil) {
    [NSApp sendEvent:pulled];
  }
}

} // namespace

// Focus is AppKit's to give and React's to hear about. Nothing checked that
// it was passed on.
TEST(textinput_focus_reaches_the_emitter) {
  @autoreleasepool {
    [NSApplication sharedApplication];

    basalt::testing::EventRecorder recorder;
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager,
                                    83,
                                    folly::dynamic::object("text", ""),
                                    recorder.emitter<TextInputEventEmitter>());

    NSWindow *window = windowAround(manager, fieldOf(view));
    EXPECT(window.firstResponder != window);

    const auto seen = recorder.seen();
    EXPECT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen.empty() ? std::string{} : seen[0], std::string{"topFocus"});

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(textinput_reports_a_key_press_from_the_event_queue) {
  @autoreleasepool {
    [NSApplication sharedApplication];

    basalt::testing::EventRecorder recorder;
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager,
                                    84,
                                    folly::dynamic::object("text", ""),
                                    recorder.emitter<TextInputEventEmitter>());

    NSWindow *window = windowAround(manager, fieldOf(view));
    recorder.clear();  // The focus above, which the test before this one owns.

    pressKey(window, 0, @"a");

    const auto seen = recorder.seen();
    EXPECT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen.empty() ? std::string{} : seen[0], std::string{"topKeyPress"});

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// A key that types nothing reports nothing, which is what iOS does. The
// arrows and the function keys live in the Unicode private use area, and the
// manager filters on that -- a filter nothing exercised.
TEST(textinput_says_nothing_about_a_key_that_types_nothing) {
  @autoreleasepool {
    [NSApplication sharedApplication];

    basalt::testing::EventRecorder recorder;
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager,
                                    85,
                                    folly::dynamic::object("text", ""),
                                    recorder.emitter<TextInputEventEmitter>());

    NSWindow *window = windowAround(manager, fieldOf(view));
    recorder.clear();

    // Left arrow: keyCode 123, and NSLeftArrowFunctionKey as its character.
    pressKey(window, 123, [NSString stringWithFormat:@"%C", (unichar)NSLeftArrowFunctionKey]);

    EXPECT(recorder.seen().empty());

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// `spellCheck` and `autoCorrect`, which on this platform are two properties of
// an NSTextView: a multiline field *is* one, and a single-line field borrows
// one as its field editor while it is focused.
//
// What is asserted unconditionally is the *request*, because AppKit may decline
// it: macOS gates continuous spell checking on a user-wide setting, and when
// the person has turned spelling off for everything it refuses a per-view
// request. Measured rather than guessed -- `NSAllowContinuousSpellChecking` is
// 0 on the machine this was written on, and `setContinuousSpellCheckingEnabled:
// YES` was ignored on a bare NSTextView with and without a window. AppKit's own
// answer is asserted where the machine allows it, and the autocorrection flag
// has no such gate and is asserted always.
namespace {

bool continuousSpellCheckingAllowed() {
  NSNumber *allowed =
      [NSUserDefaults.standardUserDefaults objectForKey:@"NSAllowContinuousSpellChecking"];
  return allowed == nil || allowed.boolValue;
}

} // namespace

TEST(textinput_spell_check_and_autocorrect_are_asked_for_on_a_multiline_field) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager,
                                    10,
                                    folly::dynamic::object("multiline", true)(
                                        "spellCheck", true)("autoCorrect", true));

    NSView *field = fieldOf(view);
    EXPECT(RnPeerIsMultiline(field));
    EXPECT(RnPeerSpellCheck(field) == RnTextCheckingOn);
    EXPECT(RnPeerAutoCorrect(field) == RnTextCheckingOn);

    NSTextView *editor = (NSTextView *)field;
    // No gate on this one.
    EXPECT(editor.isAutomaticSpellingCorrectionEnabled);
    if (continuousSpellCheckingAllowed()) {
      EXPECT(editor.isContinuousSpellCheckingEnabled);
    }

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(textinput_spell_check_and_autocorrect_can_be_refused) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager,
                                    10,
                                    folly::dynamic::object("multiline", true)(
                                        "spellCheck", false)("autoCorrect", false));

    NSTextView *editor = (NSTextView *)fieldOf(view);
    EXPECT(RnPeerSpellCheck(editor) == RnTextCheckingOff);
    EXPECT(RnPeerAutoCorrect(editor) == RnTextCheckingOff);
    // Refusing always sticks: the user setting above can only take the feature
    // away, never force it on.
    EXPECT(!editor.isContinuousSpellCheckingEnabled);
    EXPECT(!editor.isAutomaticSpellingCorrectionEnabled);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// Unset is not false, which is the rule core/TextChecking.h exists for: a field
// that says nothing keeps whatever AppKit does by default, and this asserts the
// host did not decide for it.
TEST(textinput_an_unset_checking_flag_leaves_appkits_own_answer) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;

    // AppKit's defaults, read off a peer of its own rather than assumed: they
    // are AppKit's business and have changed before.
    NSTextView *bare = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 100, 40)];
    const BOOL spellByDefault = bare.isContinuousSpellCheckingEnabled;
    const BOOL correctByDefault = bare.isAutomaticSpellingCorrectionEnabled;

    RnAppKitView *view = mountField(manager, 10, folly::dynamic::object("multiline", true));
    NSTextView *editor = (NSTextView *)fieldOf(view);
    EXPECT(RnPeerSpellCheck(editor) == RnTextCheckingUnset);
    EXPECT(RnPeerAutoCorrect(editor) == RnTextCheckingUnset);
    EXPECT_EQ(editor.isContinuousSpellCheckingEnabled, spellByDefault);
    EXPECT_EQ(editor.isAutomaticSpellingCorrectionEnabled, correctByDefault);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// And the single-line half, which is the awkward one: an NSTextField has
// neither property, so the peer remembers the request and applies it to the
// field editor when one appears.
TEST(textinput_a_single_line_field_checks_its_editor_when_it_focuses) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view =
        mountField(manager, 10, folly::dynamic::object("spellCheck", false)("autoCorrect", false));
    NSView *field = fieldOf(view);
    EXPECT(!RnPeerIsMultiline(field));
    EXPECT(RnPeerSpellCheck(field) == RnTextCheckingOff);

    // A window, because a field editor belongs to the *window* and an
    // unparented field never gets one.
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300)
                                                   styleMask:NSWindowStyleMaskTitled
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    // Not released when closed: see test_appkit_services.mm for the double
    // release that taught this suite to say so.
    window.releasedWhenClosed = NO;
    [window.contentView addSubview:manager.getSurfaceRoot(kSurfaceId)];
    EXPECT([window makeFirstResponder:field]);

    NSTextView *editor = (NSTextView *)((NSTextField *)field).currentEditor;
    EXPECT(editor != nil);
    EXPECT(editor != nil && !editor.isContinuousSpellCheckingEnabled);
    EXPECT(editor != nil && !editor.isAutomaticSpellingCorrectionEnabled);

    [window orderOut:nil];
    [window close];
    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// `autoCapitalize` and `keyboardType`, which this platform reports and cannot
// act on: there is no per-field automatic capitalisation on macOS
// (`NSSpellChecker`'s is the person's system setting) and no software keyboard
// to choose a layout for.
//
// Asserted anyway, and in the words GTK prints: the dumps are diffed line by
// line, so a field that asked for something has to say the same thing on both
// hosts and the difference belongs in the support page rather than in a missing
// line.
TEST(textinput_reports_the_capitalisation_and_keyboard_it_cannot_honour) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager,
                                    10,
                                    folly::dynamic::object("autoCapitalize", "characters")(
                                        "keyboardType", "email-address"));

    const std::string dumped = [view describeTree].UTF8String;
    EXPECT(dumped.find("autocapitalize=characters") != std::string::npos);
    EXPECT(dumped.find("keyboard=email-address") != std::string::npos);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// And the default, which React Native decides rather than this platform:
// `sentences`, as on iOS. A field that mentions neither prop still reports
// both, because both are plain enums with a default rather than optionals.
TEST(textinput_reports_react_natives_own_defaults_for_the_pair) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 10, folly::dynamic::object("text", "plain"));

    const std::string dumped = [view describeTree].UTF8String;
    EXPECT(dumped.find("autocapitalize=sentences") != std::string::npos);
    EXPECT(dumped.find("keyboard=default") != std::string::npos);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// --- The three colours a field has ------------------------------------------
//
// `placeholderTextColor`, `selectionColor` and `cursorColor`, which this host
// read none of until 2026-10-10 -- and which the backlog recorded as done for
// the first of them, because the system's placeholder grey looks like an
// answer. The per-row support audit is what caught that.
//
// Each is a third state rather than a default: `SharedColor`'s unset value is
// zero, so "the app said nothing" has to stay distinguishable from "the app
// said black". nil is what crosses, and nil leaves AppKit's own colour alone.

TEST(textinput_placeholder_takes_the_colour_the_app_asked_for) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(
        manager,
        10,
        folly::dynamic::object("placeholder", "Search")("placeholderTextColor", 0xffff0000));
    NSView *field = fieldOf(view);

    NSAttributedString *placeholder = RnPeerPlaceholder(field);
    EXPECT(placeholder != nil);
    if (placeholder == nil) {
      manager.destroySurfaceRoot(kSurfaceId);
      return;
    }
    NSColor *colour = [placeholder attribute:NSForegroundColorAttributeName
                                     atIndex:0
                              effectiveRange:nullptr];
    EXPECT(colour != nil);
    // Red, in sRGB, which is what the app asked for rather than the system's
    // placeholder grey.
    NSColor *const sRGB = [colour colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    EXPECT(sRGB != nil);
    EXPECT(sRGB != nil && sRGB.redComponent > 0.9);
    EXPECT(sRGB != nil && sRGB.greenComponent < 0.1);
    EXPECT(sRGB != nil && sRGB.blueComponent < 0.1);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// And a field that asked for nothing keeps the system's grey, which is the
// third state: a host that resolved unset to a colour would paint every
// placeholder transparent black.
TEST(textinput_a_placeholder_with_no_colour_keeps_the_systems) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view =
        mountField(manager, 10, folly::dynamic::object("placeholder", "Search"));
    NSAttributedString *placeholder = RnPeerPlaceholder(fieldOf(view));
    EXPECT(placeholder != nil);
    if (placeholder != nil) {
      NSColor *colour = [placeholder attribute:NSForegroundColorAttributeName
                                       atIndex:0
                                effectiveRange:nullptr];
      EXPECT(colour == NSColor.placeholderTextColor);
    }
    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// The selection and the caret are remembered before they can be applied: a
// single-line field borrows the window's field editor and has none until it is
// focused, which is the same reason the spell-checking flags are remembered.
TEST(textinput_selection_and_cursor_colours_are_remembered_for_the_editor) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(
        manager,
        10,
        folly::dynamic::object("selectionColor", 0xff00ff00)("cursorColor", 0xff0000ff));
    NSView *field = fieldOf(view);

    NSColor *selection =
        [RnPeerSelectionColour(field) colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    NSColor *caret =
        [RnPeerCaretColour(field) colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    EXPECT(selection != nil);
    EXPECT(caret != nil);
    EXPECT(selection != nil && selection.greenComponent > 0.9);
    EXPECT(caret != nil && caret.blueComponent > 0.9);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// `cursorColor` falls back to `selectionColor`, which is React Native's
// contract: `selectionColor` is the highlight *and* the cursor, and
// `cursorColor` exists to override the second on its own. A field given only
// the first should not have a theme-coloured caret.
TEST(textinput_a_cursor_with_no_colour_of_its_own_follows_the_selection) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view =
        mountField(manager, 10, folly::dynamic::object("selectionColor", 0xff00ff00));
    NSView *field = fieldOf(view);

    NSColor *caret =
        [RnPeerCaretColour(field) colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    EXPECT(caret != nil);
    EXPECT(caret != nil && caret.greenComponent > 0.9);

    // And a field that asked for neither has neither, so AppKit's own colours
    // stand.
    RnAppKitView *plain = mountField(manager, 11, folly::dynamic::object("text", "x"));
    EXPECT(RnPeerSelectionColour(fieldOf(plain)) == nil);
    EXPECT(RnPeerCaretColour(fieldOf(plain)) == nil);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// And they reach the real field editor when the field is focused, which is the
// half the remembering exists for.
TEST(textinput_the_field_editor_takes_the_colours_when_it_appears) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(
        manager,
        10,
        folly::dynamic::object("selectionColor", 0xff00ff00)("cursorColor", 0xff0000ff));
    NSView *field = fieldOf(view);

    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300)
                                                   styleMask:NSWindowStyleMaskTitled
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.releasedWhenClosed = NO;
    [window.contentView addSubview:manager.getSurfaceRoot(kSurfaceId)];
    EXPECT([window makeFirstResponder:field]);

    NSTextView *editor = (NSTextView *)((NSTextField *)field).currentEditor;
    EXPECT(editor != nil);
    if (editor != nil) {
      NSColor *caret = [editor.insertionPointColor
          colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
      EXPECT(caret != nil && caret.blueComponent > 0.9);

      NSColor *selection = [editor.selectedTextAttributes[NSBackgroundColorAttributeName]
          colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
      EXPECT(selection != nil);
      EXPECT(selection != nil && selection.greenComponent > 0.9);
    }

    [window orderOut:nil];
    [window close];
    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// A multiline peer is its own editor, so the colours go straight on -- and are
// still remembered, so that reading them back says what the app asked for.
TEST(textinput_a_multiline_field_takes_the_colours_directly) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(
        manager,
        10,
        folly::dynamic::object("multiline", true)("selectionColor", 0xff00ff00)(
            "cursorColor", 0xff0000ff));
    NSView *field = fieldOf(view);
    EXPECT(RnPeerIsMultiline(field));

    NSTextView *editor = (NSTextView *)field;
    NSColor *caret =
        [editor.insertionPointColor colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    EXPECT(caret != nil && caret.blueComponent > 0.9);
    NSColor *selection = [editor.selectedTextAttributes[NSBackgroundColorAttributeName]
        colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    EXPECT(selection != nil && selection.greenComponent > 0.9);
    EXPECT(RnPeerSelectionColour(field) != nil);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// --- clearTextOnFocus and selectTextOnFocus ---------------------------------
//
// What happens when a field takes focus, which no host read until 2026-10-10.
// Both are about the moment focus arrives rather than about the props, so both
// need a real window: an NSTextField has no field editor until it is first
// responder, and a selection with nowhere to live is not a selection.

TEST(textinput_clear_text_on_focus_empties_the_field) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(
        manager, 10, folly::dynamic::object("text", "keep me")("clearTextOnFocus", true));
    NSView *field = fieldOf(view);
    EXPECT([RnPeerText(field) isEqualToString:@"keep me"]);

    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300)
                                                   styleMask:NSWindowStyleMaskTitled
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.releasedWhenClosed = NO;
    [window.contentView addSubview:manager.getSurfaceRoot(kSurfaceId)];
    EXPECT([window makeFirstResponder:field]);

    // Empty, and empty where the user can see it rather than only in the props.
    EXPECT([RnPeerText(field) isEqualToString:@""]);

    [window orderOut:nil];
    [window close];
    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// And a field that did not ask keeps its text, which is the control: a host
// that cleared unconditionally would pass the test above.
TEST(textinput_a_field_that_did_not_ask_keeps_its_text_on_focus) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 10, folly::dynamic::object("text", "keep me"));
    NSView *field = fieldOf(view);

    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300)
                                                   styleMask:NSWindowStyleMaskTitled
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.releasedWhenClosed = NO;
    [window.contentView addSubview:manager.getSurfaceRoot(kSurfaceId)];
    EXPECT([window makeFirstResponder:field]);

    EXPECT([RnPeerText(field) isEqualToString:@"keep me"]);

    [window orderOut:nil];
    [window close];
    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// **`selectTextOnFocus` is not observable on the user-focus path on this host**,
// and that is worth saying rather than pretending otherwise: an NSTextField
// selects all of its text on becoming first responder whatever the prop says,
// so a test that focused a field and found everything selected would pass with
// the prop removed. It is a sabotage run that establishes that -- the first
// version of this file had exactly that test, and it survived.
//
// What the prop changes is the *programmatic* path, where this host collapses
// the selection on purpose, so that is where it is asserted: below, both ways
// round.
//
// `autoFocus` with no `selectTextOnFocus` gets a caret at the end, which is
// what this host goes out of its way to arrange: an NSTextField selects
// everything on becoming first responder, and `flushAutoFocus` collapses that
// because putting a caret in a field is what focusing one means. The prop is
// what turns the collapse off, so this is the other side of the test above --
// and it needs the window *before* the mount, `flushAutoFocus` having nowhere
// to focus otherwise.
TEST(textinput_auto_focus_alone_leaves_a_caret_rather_than_a_selection) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300)
                                                   styleMask:NSWindowStyleMaskTitled
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.releasedWhenClosed = NO;
    [window.contentView addSubview:root];

    RnAppKitView *view =
        mountField(manager, 10, folly::dynamic::object("text", "select me")("autoFocus", true));
    NSView *field = fieldOf(view);
    EXPECT(window.firstResponder == RnPeerEditor(field));
    EXPECT(RnPeerSelection(field).length == 0);

    // And the same field asking for the prop keeps the whole selection, which
    // is the one line of difference between the two paths.
    RnAppKitView *selecting = mountField(
        manager,
        11,
        folly::dynamic::object("text", "select me")("autoFocus", true)("selectTextOnFocus", true));
    EXPECT(RnPeerSelection(fieldOf(selecting)).length == 9);

    [window orderOut:nil];
    [window close];
    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// Both at once, in the order that means something: cleared first, so there is
// nothing left to select. A host that selected first would leave a selection
// over text it then threw away.
TEST(textinput_clearing_and_selecting_at_once_leaves_an_empty_field) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager,
                                    10,
                                    folly::dynamic::object("text", "both")("clearTextOnFocus", true)(
                                        "selectTextOnFocus", true));
    NSView *field = fieldOf(view);

    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300)
                                                   styleMask:NSWindowStyleMaskTitled
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.releasedWhenClosed = NO;
    [window.contentView addSubview:manager.getSurfaceRoot(kSurfaceId)];
    EXPECT([window makeFirstResponder:field]);

    EXPECT([RnPeerText(field) isEqualToString:@""]);
    EXPECT(RnPeerSelection(field).length == 0);

    [window orderOut:nil];
    [window close];
    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// --- caretHidden and contextMenuHidden ---------------------------------------
//
// Two props with no AppKit property between them, going in by different routes:
// the caret through the same remembered colour `cursorColor` uses, the menu by
// the peer refusing to build one.

TEST(textinput_caret_hidden_asks_for_a_clear_insertion_point) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 10, folly::dynamic::object("caretHidden", true));
    NSView *field = fieldOf(view);

    // Clear rather than nil: nil means AppKit's own, which is a caret you can
    // see. An NSTextView draws its insertion point as a filled rectangle, so a
    // clear one is drawn and invisible.
    NSColor *const caret = RnPeerCaretColour(field);
    EXPECT(caret != nil);
    if (caret != nil) {
      EXPECT_NEAR([caret colorUsingColorSpace:NSColorSpace.sRGBColorSpace].alphaComponent,
                  0.0,
                  0.001);
    }

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(textinput_caret_hidden_wins_over_a_cursor_colour) {
  @autoreleasepool {
    // A field that asked for both means the hidden one: it cannot be both red
    // and invisible, and "hide it" is the more specific instruction.
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(
        manager, 10, folly::dynamic::object("caretHidden", true)("cursorColor", 0xffff0000));
    NSColor *const caret =
        [RnPeerCaretColour(fieldOf(view)) colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    EXPECT(caret != nil);
    if (caret != nil) {
      EXPECT_NEAR(caret.alphaComponent, 0.0, 0.001);
    }

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(textinput_a_field_that_did_not_ask_keeps_its_caret) {
  @autoreleasepool {
    // The control: nothing remembered, so AppKit's own caret.
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 10, folly::dynamic::object("text", "visible"));
    EXPECT(RnPeerCaretColour(fieldOf(view)) == nil);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

namespace {

// A right-click, which is what `-menuForEvent:` is asked about.
NSEvent *secondaryClick() {
  return [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
                            location:NSMakePoint(10, 10)
                       modifierFlags:0
                           timestamp:0
                        windowNumber:0
                             context:nil
                         eventNumber:1
                          clickCount:1
                            pressure:1];
}

} // namespace

// `contextMenuHidden` on a **multiline** field, which is the half of the prop
// this host can answer: the peer there is this file's own NSTextView subclass,
// and it is the view the click lands on.
//
// A single-line field is the half it cannot, and that is measured rather than
// assumed -- see the test below.
TEST(textinput_context_menu_hidden_refuses_to_build_one_on_a_multiline_field) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(
        manager, 10, folly::dynamic::object("multiline", true)("contextMenuHidden", true));
    NSView *area = fieldOf(view);
    EXPECT(RnPeerContextMenuHidden(area));
    // The behaviour rather than the flag: nil is how a view tells AppKit it has
    // no context menu.
    EXPECT([area menuForEvent:secondaryClick()] == nil);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(textinput_a_multiline_field_that_did_not_ask_keeps_its_context_menu) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 10, folly::dynamic::object("multiline", true));
    NSView *area = fieldOf(view);
    EXPECT(!RnPeerContextMenuHidden(area));
    EXPECT([area menuForEvent:secondaryClick()] != nil);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// And what a single-line field does with the prop, which is carry it: the menu
// a focused NSTextField shows belongs to the window's shared *field editor*,
// an NSTextView this file does not own, and that view rebuilds its menu inside
// `-menuForEvent:` -- assigning `menu = nil` on it does not stop it, which was
// measured rather than guessed.
//
// So the flag reaches the peer and the peer is not the view being asked.
// backlog/textinput.md names the route that would close it, which is supplying
// the field editor through `-windowWillReturnFieldEditor:toObject:`.
// And both are in the tree dump, which is where the cross-host diff reads them:
// each is the absence of something, so there is nothing else to compare.
TEST(textinput_the_hiding_props_are_reported_in_the_tree) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(
        manager, 10, folly::dynamic::object("caretHidden", true)("contextMenuHidden", true));
    const std::string hidden = [view describeTree].UTF8String;
    EXPECT(hidden.find("caret=hidden") != std::string::npos);
    EXPECT(hidden.find("context-menu=hidden") != std::string::npos);

    RnAppKitView *plain = mountField(manager, 11, folly::dynamic::object("text", "visible"));
    const std::string shown = [plain describeTree].UTF8String;
    EXPECT(shown.find("caret=hidden") == std::string::npos);
    EXPECT(shown.find("context-menu=hidden") == std::string::npos);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(textinput_a_single_line_field_carries_the_prop_it_cannot_act_on) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view =
        mountField(manager, 10, folly::dynamic::object("contextMenuHidden", true));
    EXPECT(RnPeerContextMenuHidden(fieldOf(view)));

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// --- submitBehavior ----------------------------------------------------------
//
// What Return does. React Native resolves the default itself --
// `getNonDefaultSubmitBehavior()` answers `blurAndSubmit` for a single-line
// field and `newline` for a multiline one -- and this host used to leave it to
// AppKit instead: an NSTextField keeps focus on Return, so every field behaved
// as `submit` whatever the prop said.
//
// Entered through the delegate callback AppKit uses, which is the seam the
// manager is wired to: a real Return needs a window, a key window and a run
// loop, and what it would reach is this method.
namespace {

// Return, as AppKit delivers it to a single-line field's delegate. Answers
// whether the key was used up, which is what tells AppKit to insert nothing.
bool pressReturn(basalt::AppKitMountingManager &manager, NSView *field) {
  id<NSTextFieldDelegate> delegate = ((NSTextField *)field).delegate;
  (void)manager;
  // The field editor is the `textView` AppKit passes, and the manager ignores
  // it -- which the cast here makes explicit rather than passing nil, since the
  // parameter is declared non-null.
  NSTextView *editor = (NSTextView *)((NSTextField *)field).currentEditor;
  if (editor == nil) {
    editor = [[NSTextView alloc] initWithFrame:NSZeroRect];
  }
  return [delegate control:(NSControl *)field
                        textView:editor
             doCommandBySelector:@selector(insertNewline:)] == YES;
}

} // namespace

TEST(textinput_return_submits_by_default) {
  @autoreleasepool {
    basalt::testing::EventRecorder recorder;
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager,
                                    90,
                                    folly::dynamic::object("text", "query"),
                                    recorder.emitter<TextInputEventEmitter>());

    EXPECT(pressReturn(manager, fieldOf(view)));

    const auto seen = recorder.seen();
    EXPECT(!seen.empty());
    EXPECT_EQ(seen.empty() ? std::string{} : seen.front(), std::string{"topSubmitEditing"});

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(textinput_submit_behavior_newline_reports_nothing) {
  @autoreleasepool {
    // The point of the prop: a chat input that inserts newlines must not send a
    // message on Return. And the key is *not* used up, which is what lets a
    // multiline field insert the newline itself.
    basalt::testing::EventRecorder recorder;
    basalt::AppKitMountingManager manager;
    RnAppKitView *view =
        mountField(manager,
                   91,
                   folly::dynamic::object("text", "query")("submitBehavior", "newline"),
                   recorder.emitter<TextInputEventEmitter>());

    EXPECT(!pressReturn(manager, fieldOf(view)));
    EXPECT(recorder.seen().empty());

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(textinput_submit_behavior_submit_reports_and_stays) {
  @autoreleasepool {
    basalt::testing::EventRecorder recorder;
    basalt::AppKitMountingManager manager;
    RnAppKitView *view =
        mountField(manager,
                   92,
                   folly::dynamic::object("text", "query")("submitBehavior", "submit"),
                   recorder.emitter<TextInputEventEmitter>());

    EXPECT(pressReturn(manager, fieldOf(view)));

    // One event and no blur: `submit` is the half of the prop that keeps focus,
    // and a blur would arrive as `topBlur` and `topEndEditing` through the
    // delegate -- which is what the `blurAndSubmit` test below sees.
    const auto seen = recorder.seen();
    EXPECT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen.empty() ? std::string{} : seen[0], std::string{"topSubmitEditing"});

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(textinput_submit_behavior_blur_and_submit_gives_up_focus) {
  @autoreleasepool {
    // A real window, because giving up focus is a window's business: the field
    // is made first responder and the key is what has to take that away.
    basalt::testing::EventRecorder recorder;
    basalt::AppKitMountingManager manager;
    RnAppKitView *view =
        mountField(manager,
                   93,
                   folly::dynamic::object("text", "query")("submitBehavior", "blurAndSubmit"),
                   recorder.emitter<TextInputEventEmitter>());

    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300)
                                                  styleMask:NSWindowStyleMaskTitled
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
    [window.contentView addSubview:view];
    [window makeKeyAndOrderFront:nil];
    NSView *field = fieldOf(view);
    [window makeFirstResponder:field];
    EXPECT(window.firstResponder != window);

    EXPECT(pressReturn(manager, field));

    // Focus handed back to the window, which is this host's "nothing is
    // focused" -- the same move the `blur` command makes.
    EXPECT(window.firstResponder == window);

    // Among the events rather than the first: making the field first
    // responder in a real window reports a focus and a selection on the way,
    // which is the price of needing a window to ask about focus at all.
    const auto seen = recorder.seen();
    EXPECT(std::find(seen.begin(), seen.end(), std::string{"topSubmitEditing"}) != seen.end());

    manager.destroySurfaceRoot(kSurfaceId);
  }
}
