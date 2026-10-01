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
