// Tests for <TextInput>: the GtkText peer and the controlled-value loop.
//
// The interesting bug in a text field is not typing, it is the loop. React
// Native's <TextInput> is controlled: JavaScript owns the value, the widget
// reports every change, JavaScript re-renders and sends the value back down. If
// applying that prop looks like the user typing, the two chase each other
// forever; if the cursor is not preserved while applying it, the caret jumps
// home on every keystroke. Both are asserted below, because both were wrong
// first.
//
// Props are built through React Native's own RawProps parser rather than by
// assigning fields, because the fields that matter -- `traits.editable`,
// `traits.secureTextEntry` -- are const and only reachable that way. That also
// means these tests exercise the same parse the real thing does.

#include "TestHarness.h"
#include "EventRecorder.h"

#include "GtkMountingManager.h"
#include "GtkTextInput.h"

#include "GtkTextPeer.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>
#include <react/renderer/core/RawPropsParser.h>

#include <folly/dynamic.h>

using facebook::react::ContextContainer;
using facebook::react::EventEmitter;
using facebook::react::LayoutMetrics;
using facebook::react::PropsParserContext;
using facebook::react::RawProps;
using facebook::react::RawPropsParser;
using facebook::react::ShadowView;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TextInputProps;

namespace {

constexpr SurfaceId kSurfaceId = 1;

// One parser, prepared once: preparing walks every prop TextInputProps knows,
// which is not something to redo per test.
const RawPropsParser &textInputParser() {
  static const RawPropsParser parser = []() {
    RawPropsParser prepared;
    prepared.prepare<TextInputProps>();
    return prepared;
  }();
  return parser;
}

std::shared_ptr<const TextInputProps> makeProps(folly::dynamic raw) {
  static const auto contextContainer = std::make_shared<const ContextContainer>();
  PropsParserContext context{kSurfaceId, *contextContainer};

  RawProps rawProps{std::move(raw)};
  rawProps.parse(textInputParser());
  return std::make_shared<const TextInputProps>(context, TextInputProps{}, rawProps);
}

ShadowView makeTextInput(Tag tag, folly::dynamic raw, float inset = 0.0F) {
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0.0F, .y = 0.0F}, .size = {.width = 200.0F, .height = 40.0F}};
  metrics.contentInsets = {.left = inset, .top = inset, .right = inset, .bottom = inset};

  ShadowView view;
  view.componentName = "TextInput";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = makeProps(std::move(raw));
  view.layoutMetrics = metrics;
  return view;
}

// No event emitter, which is right for everything asserted on the widget side
// of the loop: the field's own state does not depend on anyone listening.
basalt::GtkTextInputManager makeManager() {
  return basalt::GtkTextInputManager([](Tag) { return EventEmitter::Shared{}; });
}

// And one that is listened to. A real emitter over a real EventDispatcher --
// see tests/EventRecorder.h for why that is possible without a JavaScript
// runtime, which the comment here used to say it was not.
basalt::GtkTextInputManager makeRecordedManager(const basalt::testing::EventRecorder &recorder) {
  auto emitter = recorder.emitter<facebook::react::TextInputEventEmitter>();
  return basalt::GtkTextInputManager([emitter](Tag) { return emitter; });
}

// Through the seam, because a multiline peer is not a GtkEditable and asking
// it for one is a runtime critical rather than a compile error.
std::string textOf(RnView *view) {
  GtkWidget *editable = rn_view_get_editable(view);
  if (editable == nullptr) {
    return {};
  }
  char *owned = rn_peer_get_text(editable);
  std::string text = owned != nullptr ? owned : "";
  g_free(owned);
  return text;
}

// What a person typing does, as far as GtkText is concerned: an insertion the
// widget did not come from a prop.
void typeInto(RnView *view, const char *text, int position) {
  int cursor = position;
  gtk_editable_insert_text(
      GTK_EDITABLE(rn_view_get_editable(view)), text, static_cast<int>(strlen(text)), &cursor);
}

} // namespace

TEST(textinput_mounts_a_real_editable) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  EXPECT(rn_view_get_editable(view) == nullptr);
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "hello")));

  // A GtkText, not a PangoLayout with a cursor drawn on it: input methods,
  // selection, the clipboard and every Linux keybinding come with it.
  EXPECT(rn_view_get_editable(view) != nullptr);
  EXPECT(GTK_IS_TEXT(rn_view_get_editable(view)));
  EXPECT_EQ(textOf(view), std::string("hello"));

  g_object_unref(view);
}

TEST(textinput_applies_props_without_reporting_them_as_typing) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "a")));
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "ab")));
  EXPECT_EQ(textOf(view), std::string("ab"));

  // The event count is the observable half of the loop: applying a prop must
  // not raise it, or the next command from JavaScript looks stale and is
  // dropped. Nothing typed here, so it must still be zero -- which
  // setTextAndSelection at count 0 proves by being accepted.
  EXPECT(manager.dispatchCommand(
      10, "setTextAndSelection", folly::dynamic::array(0, "from js", 0, 0)));
  EXPECT_EQ(textOf(view), std::string("from js"));

  g_object_unref(view);
}

TEST(textinput_preserves_the_cursor_when_a_prop_arrives) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "abcd")));
  gtk_editable_set_position(GTK_EDITABLE(rn_view_get_editable(view)), 2);

  // A controlled field re-renders on every keystroke, so this happens
  // constantly. Assigning the text resets GtkText's cursor to the start, which
  // sends the caret home mid-word unless it is put back.
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "abXcd")));
  EXPECT_EQ(gtk_editable_get_position(GTK_EDITABLE(rn_view_get_editable(view))), 2);

  g_object_unref(view);
}

TEST(textinput_drops_a_command_older_than_what_was_typed) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "")));
  typeInto(view, "fast", 0);

  // The whole reason React Native counts events. JavaScript rendered against an
  // empty field and is only now asking for that state; the user has since
  // typed, so this must be ignored rather than undoing their work.
  EXPECT(manager.dispatchCommand(
      10, "setTextAndSelection", folly::dynamic::array(0, "stale", 0, 0)));
  EXPECT_EQ(textOf(view), std::string("fast"));

  // A command that knows about the typing is applied.
  EXPECT(manager.dispatchCommand(
      10, "setTextAndSelection", folly::dynamic::array(99, "current", 0, 0)));
  EXPECT_EQ(textOf(view), std::string("current"));

  g_object_unref(view);
}

// An uncontrolled field keeps what was typed into it.
//
// React Native re-sends `mostRecentEventCount` on every change, which is an
// Update mutation on its own, and `text` for an uncontrolled field is the empty
// string forever -- `TextInput.js` sends `text={value ?? defaultValue}` and an
// uncontrolled field with no default sends undefined. So a manager that applies
// the prop whenever it differs from the widget wipes the field on the user's
// second keystroke. Applying it only when the *prop* changes is what makes this
// work; found on Windows in phase 46, and this platform had the same bug.
TEST(textinput_keeps_what_was_typed_into_an_uncontrolled_field) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "")));
  typeInto(view, "abc", 0);
  EXPECT_EQ(textOf(view), std::string("abc"));

  // The re-render an uncontrolled field produces: no text, a bumped count.
  manager.update(
      view,
      makeTextInput(10, folly::dynamic::object("text", "")("mostRecentEventCount", 1)));
  EXPECT_EQ(textOf(view), std::string("abc"));

  g_object_unref(view);
}

// The same staleness rule the commands already follow, applied to the prop. A
// value React rendered before the user's latest keystroke must not be applied,
// or a fast typist watches characters reorder themselves -- and it must not be
// *forgotten* either, or the value is lost once JavaScript catches up.
// The whole selection reaches JavaScript, not only the caret. Before this the
// metrics reported {cursor, 0} whatever was selected, so `onSelectionChange`
// could not tell a caret from a selected word.
TEST(textinput_reports_the_whole_selection_not_only_the_caret) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "abcdef")));
  GtkWidget *editable = rn_view_get_editable(view);
  rn_peer_select_region(editable, 1, 4);

  int start = 0;
  int end = 0;
  EXPECT(rn_peer_get_selection_bounds(editable, &start, &end));
  EXPECT_EQ(start, 1);
  EXPECT_EQ(end, 4);

  g_object_unref(view);
}

// The `selection` prop is controlled the way `text` is, and under the same
// staleness rule.
TEST(textinput_applies_a_selection_prop) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "abcdef")));
  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "abcdef")(
                                   "selection", folly::dynamic::object("start", 2)("end", 5))));

  GtkWidget *editable = rn_view_get_editable(view);
  int start = 0;
  int end = 0;
  EXPECT(rn_peer_get_selection_bounds(editable, &start, &end));
  EXPECT_EQ(start, 2);
  EXPECT_EQ(end, 5);

  g_object_unref(view);
}

// A selection from JavaScript that predates the last keystroke is dropped, for
// the same reason a stale `text` is: it would drag the caret back to where
// JavaScript thought it was, mid-word.
TEST(textinput_drops_a_selection_prop_older_than_what_was_typed) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "abcdef")));
  GtkWidget *editable = rn_view_get_editable(view);

  // Type, which raises the event count past what the next props carry. The
  // position is in/out and must be a real variable: GtkEditable writes the
  // caret's new home back through it.
  int position = 6;
  rn_peer_insert_text(editable, "g", 1, &position);

  rn_peer_select_region(editable, 0, 0);
  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "abcdef")(
                                   "selection", folly::dynamic::object("start", 2)("end", 5))));

  int start = 0;
  int end = 0;
  rn_peer_get_selection_bounds(editable, &start, &end);
  EXPECT_EQ(start, 0);
  EXPECT_EQ(end, 0);

  g_object_unref(view);
}

TEST(textinput_drops_a_text_prop_older_than_what_was_typed) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "")));
  typeInto(view, "fast", 0);

  manager.update(
      view,
      makeTextInput(10, folly::dynamic::object("text", "F")("mostRecentEventCount", 0)));
  EXPECT_EQ(textOf(view), std::string("fast"));

  manager.update(
      view,
      makeTextInput(10, folly::dynamic::object("text", "F")("mostRecentEventCount", 1)));
  EXPECT_EQ(textOf(view), std::string("F"));

  g_object_unref(view);
}

TEST(textinput_honours_editable_and_secure_entry) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "secret")("editable", false)(
                                   "secureTextEntry", true)("placeholder", "type here")));

  GtkWidget *editable = rn_view_get_editable(view);
  EXPECT(gtk_editable_get_editable(GTK_EDITABLE(editable)) == FALSE);
  EXPECT(gtk_text_get_visibility(GTK_TEXT(editable)) == FALSE);
  EXPECT_EQ(std::string(gtk_text_get_placeholder_text(GTK_TEXT(editable))), std::string("type here"));

  // editable is a prop, not a permanent trait; turning it back on must work.
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "secret")));
  EXPECT(gtk_editable_get_editable(GTK_EDITABLE(editable)) == TRUE);
  EXPECT(gtk_text_get_visibility(GTK_TEXT(editable)) == TRUE);

  g_object_unref(view);
}

TEST(textinput_allocates_its_peer_inside_the_content_inset) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  // Yoga resolves border and padding into the content inset. A GtkText knows
  // nothing about either, so without this the text sits flush against the
  // field's own border and paddingHorizontal does nothing.
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "x"), 12.0F));
  rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 40.0F);
  gtk_widget_allocate(GTK_WIDGET(view), 200, 40, -1, nullptr);

  GtkWidget *peer = GTK_WIDGET(rn_view_get_editable(view));
  EXPECT_EQ(gtk_widget_get_width(peer), 176);
  EXPECT_EQ(gtk_widget_get_height(peer), 16);

  graphene_point_t origin;
  const graphene_point_t zero = {0.0F, 0.0F};
  EXPECT(gtk_widget_compute_point(peer, GTK_WIDGET(view), &zero, &origin));
  EXPECT_NEAR(origin.x, 12.0, 0.5);
  EXPECT_NEAR(origin.y, 12.0, 0.5);

  g_object_unref(view);
}

TEST(textinput_reports_its_value_in_the_widget_tree) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  // The end-to-end suite reads this tree, and a field's text lives in its peer
  // rather than in a PangoLayout, so it would otherwise be invisible there.
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "in the tree")));
  char *tree = rn_view_describe_tree(view);
  EXPECT(std::string(tree).find("editable=\"in the tree\"") != std::string::npos);
  g_free(tree);

  g_object_unref(view);
}

// --- multiline ---------------------------------------------------------------
//
// A multiline field is a different widget, not a property: GtkText is a
// GtkEditable and GtkTextView is not. These pin that the seam in GtkTextPeer.h
// really does hide that, because every one of the behaviours above goes
// through it.

TEST(textinput_multiline_builds_a_text_view) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "hello")("multiline", true)));

  GtkWidget *peer = rn_view_get_editable(view);
  EXPECT(peer != nullptr);
  EXPECT(rn_peer_is_multiline(peer));
  EXPECT(GTK_IS_TEXT_VIEW(peer));

  // And a plain field is still a GtkText, which is the half that must not have
  // moved.
  RnView *single = rn_view_new(11);
  g_object_ref_sink(single);
  manager.update(single, makeTextInput(11, folly::dynamic::object("text", "hello")));
  EXPECT(!rn_peer_is_multiline(rn_view_get_editable(single)));

  g_object_unref(single);
  g_object_unref(view);
}

TEST(textinput_multiline_text_round_trips) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "one")("multiline", true)));
  EXPECT_EQ(textOf(view), std::string("one"));

  // Newlines are the point of a multiline field, and are what a GtkText would
  // have refused to hold.
  manager.update(view,
                 makeTextInput(10, folly::dynamic::object("text", "one\ntwo")("multiline", true)));
  EXPECT_EQ(textOf(view), std::string("one\ntwo"));

  g_object_unref(view);
}

TEST(textinput_multiline_selection_and_caret_work) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10, folly::dynamic::object("text", "abcdef")("multiline", true)));
  GtkWidget *peer = rn_view_get_editable(view);

  rn_peer_select_region(peer, 1, 4);
  int start = 0;
  int end = 0;
  EXPECT(rn_peer_get_selection_bounds(peer, &start, &end));
  EXPECT_EQ(start, 1);
  EXPECT_EQ(end, 4);

  rn_peer_set_position(peer, 2);
  EXPECT_EQ(rn_peer_get_position(peer), 2);

  // Offsets are characters, not bytes: the seam says so and a buffer counts
  // both, so a non-ASCII string is where that promise is kept or broken.
  manager.update(view,
                 makeTextInput(10, folly::dynamic::object("text", "a\u00e9c")("multiline", true)));
  rn_peer_set_position(peer, 2);
  EXPECT_EQ(rn_peer_get_position(peer), 2);

  g_object_unref(view);
}

TEST(textinput_switching_multiline_rebuilds_the_peer) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "hello")));
  EXPECT(!rn_peer_is_multiline(rn_view_get_editable(view)));

  // Not a property that can be flipped, so the widget is replaced -- and the
  // text has to survive that, because React did not ask for it to be cleared.
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "hello")("multiline", true)));
  EXPECT(rn_peer_is_multiline(rn_view_get_editable(view)));
  EXPECT_EQ(textOf(view), std::string("hello"));

  g_object_unref(view);
}

// maxLength, on the peer that has no property for it. GtkText enforces its own;
// a buffer refuses the insertion that would cross the limit instead.
TEST(textinput_multiline_refuses_text_past_max_length) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10, folly::dynamic::object("text", "")("multiline", true)(
                                       "maxLength", 5)));
  GtkWidget *peer = rn_view_get_editable(view);

  int at = 0;
  rn_peer_insert_text(peer, "abcd", 4, &at);
  EXPECT_EQ(textOf(view), std::string("abcd"));

  // Refused whole rather than truncated, which is what GtkText does with its
  // own limit -- a paste that would overflow leaves what was there.
  at = 4;
  rn_peer_insert_text(peer, "efgh", 4, &at);
  EXPECT_EQ(textOf(view), std::string("abcd"));

  // And one that fits still lands.
  at = 4;
  rn_peer_insert_text(peer, "e", 1, &at);
  EXPECT_EQ(textOf(view), std::string("abcde"));

  g_object_unref(view);
}

// A placeholder on the peer that has no property for it. Drawn rather than set,
// so what a test can reach is the string the peer was given -- the drawing
// itself is in the screenshot that went with phase 57.
TEST(textinput_multiline_takes_a_placeholder) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10, folly::dynamic::object("text", "")("multiline", true)(
                                       "placeholder", "type here")));
  GtkWidget *peer = rn_view_get_editable(view);
  EXPECT(rn_peer_is_multiline(peer));
  const char *got = rn_peer_get_placeholder(peer);
  EXPECT_EQ(std::string(got != nullptr ? got : ""), std::string("type here"));

  // And the single-line peer still takes its own, which is a property rather
  // than something drawn.
  RnView *single = rn_view_new(11);
  g_object_ref_sink(single);
  manager.update(single,
                 makeTextInput(11, folly::dynamic::object("text", "")("placeholder", "type here")));
  const char *single_got = rn_peer_get_placeholder(rn_view_get_editable(single));
  EXPECT_EQ(std::string(single_got != nullptr ? single_got : ""), std::string("type here"));

  g_object_unref(single);
  g_object_unref(view);
}

TEST(textinput_drops_its_peer_when_the_field_is_removed) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "x")));
  manager.remove(10);

  // The signal handlers hold a pointer into the manager's own map, so the
  // widget has to go with the entry or a later `changed` writes through a
  // dangling pointer.
  EXPECT(rn_view_get_editable(view) == nullptr);
  EXPECT(!manager.dispatchCommand(10, "focus", folly::dynamic::array()));

  g_object_unref(view);
}

// --- What React actually hears ---------------------------------------------
//
// Nothing below the end-to-end suite could see an event until
// tests/EventRecorder.h, so every assertion above is about the widget and
// none is about the loop. These two are about the loop.

TEST(textinput_change_reaches_the_emitter) {
  basalt::testing::EventRecorder recorder;
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeRecordedManager(recorder);
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "")));

  // Mounting a field is not the user doing anything, so React hears nothing
  // yet. Worth pinning: a recorder wired up wrong would be just as quiet.
  EXPECT(recorder.seen().empty());

  typeInto(view, "a", 0);

  const auto seen = recorder.seen();
  EXPECT_EQ(seen.size(), 1U);
  EXPECT_EQ(seen.empty() ? std::string{} : seen[0], std::string("topChange"));

  g_object_unref(view);
}

// A value pushed down as a prop is React talking to the field, not the field
// talking back -- so it must not come back up as a change. That is the
// controlled-value loop the top of this file describes, and the half of it
// that could not be checked until an event could be observed at all: the
// tests above could see that the text was right, not that the field stayed
// quiet about it.
TEST(textinput_a_prop_does_not_report_itself_as_a_change) {
  basalt::testing::EventRecorder recorder;
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeRecordedManager(recorder);
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "")));

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "from React")));

  EXPECT_EQ(textOf(view), std::string("from React"));
  EXPECT(recorder.seen().empty());

  g_object_unref(view);
}
