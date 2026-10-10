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

#include <cstdint>
#include <string>

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

// A colour the way React Native's own prop parser takes one: packed ARGB, which
// is what `processColor` hands down from JavaScript and the only shape that
// reaches a SharedColor without a JavaScript runtime. Signed, because an opaque
// colour has its top bit set and folly::dynamic holds a signed integer.
constexpr int32_t argb(uint32_t value) {
  return static_cast<int32_t>(value);
}

// The stylesheet the peer generated for its colour props, or the empty string
// when it generated none. Empty and "no rule for this colour" are different
// things and the tests below check both, so this never invents a rule.
std::string cssOf(RnView *view) {
  const char *css = rn_peer_get_colors_css(rn_view_get_editable(view));
  return css != nullptr ? css : "";
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

// --- placeholderTextColor, selectionColor and cursorColor --------------------
//
// These three were parsed and then dropped on this host for a long time, and
// why is worth keeping in view: GtkText takes none of them from the
// PangoAttrList that carries `color` and `fontSize`. The placeholder and the
// selection are CSS nodes of their own and the caret is the CSS `caret-color`
// property, so the only way in is a stylesheet.
//
// What is assertable here is that stylesheet rather than the pixels. A widget's
// resolved style cannot be read back without GtkStyleContext, which GTK 4.10
// deprecated and 4.22 has already moved under gtk/deprecated/, so the peer hands
// back the rules it generated and the class it hung them on and these pin both.
// Whether GTK then paints what the rules say is a screenshot's job, and GTK's
// own business.

TEST(textinput_puts_the_three_colour_props_into_css) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "")("placeholder", "type here")(
                                   "placeholderTextColor", argb(0xFFFF0000U))(
                                   "selectionColor", argb(0xFF0000FFU))(
                                   "cursorColor", argb(0xFF00FF00U))));

  // One rule per prop, each naming the node GTK actually draws: the
  // placeholder's own node, the selection's own node, and `caret-color` on the
  // field itself. Getting the node wrong is the failure mode that looks like
  // working code, because the stylesheet parses and then matches nothing.
  const std::string css = cssOf(view);
  EXPECT(css.find("placeholder { color: rgb(255,0,0); }") != std::string::npos);
  EXPECT(css.find("selection { background-color: rgb(0,0,255); }") != std::string::npos);
  EXPECT(css.find("{ caret-color: rgb(0,255,0);") != std::string::npos);

  // And the widget carries the class those rules are keyed on, without which the
  // stylesheet would be perfectly correct and entirely invisible.
  GtkWidget *peer = rn_view_get_editable(view);
  const char *className = rn_peer_get_colors_class(peer);
  EXPECT(className != nullptr);
  const std::string name = className != nullptr ? className : "";
  EXPECT(gtk_widget_has_css_class(peer, name.c_str()));
  EXPECT(css.find(name) != std::string::npos);

  g_object_unref(view);
}

// A prop that was never sent must leave GTK's own colour alone. This is the
// assertion that a careless conversion fails: an unset SharedColor is zero, so
// handing it on without asking whether it was set paints every field's caret
// black and takes the theme's placeholder and selection with it.
TEST(textinput_leaves_an_unset_colour_to_the_gtk_theme) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(
      view, makeTextInput(10, folly::dynamic::object("text", "")("placeholder", "type here")));

  GtkWidget *peer = rn_view_get_editable(view);
  EXPECT(rn_peer_get_colors_class(peer) == nullptr);
  EXPECT(rn_peer_get_colors_css(peer) == nullptr);

  // And one prop being set does not drag the other two in behind it.
  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "")("placeholder", "type here")(
                                   "selectionColor", argb(0xFF0000FFU))));
  const std::string css = cssOf(view);
  EXPECT(css.find("selection { background-color: rgb(0,0,255); }") != std::string::npos);
  EXPECT(css.find("placeholder") == std::string::npos);

  g_object_unref(view);
}

// The alpha survives. GDK writes `rgb(...)` for an opaque colour and `rgba(...)`
// for anything else, so a half-transparent placeholder is exactly where a
// formatter that quietly dropped the fourth channel shows up.
TEST(textinput_carries_a_colours_alpha_into_the_css) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "")("placeholder", "type here")(
                                   "placeholderTextColor", argb(0x330000FFU))));

  // Matched without the closing paren: 0x33 is 51/255, which is 0.2, and GDK
  // formats the alpha with %g, so six significant digits of it may legitimately
  // be written either "0.2" or "0.200000".
  const std::string css = cssOf(view);
  EXPECT(css.find("rgba(0,0,255,0.2") != std::string::npos);
  EXPECT(css.find("rgb(0,0,255)") == std::string::npos);

  g_object_unref(view);
}

// The class is swapped rather than added to. Every rule lives in one
// display-wide provider, so a widget left carrying both classes would resolve to
// whichever rule the provider happened to parse last rather than to the colour
// React asked for.
TEST(textinput_swaps_its_colour_class_when_the_prop_changes) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "")("placeholder", "type here")(
                                   "placeholderTextColor", argb(0xFFFF0000U))));
  GtkWidget *peer = rn_view_get_editable(view);
  const char *before = rn_peer_get_colors_class(peer);
  EXPECT(before != nullptr);
  const std::string first = before != nullptr ? before : "";

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "")("placeholder", "type here")(
                                   "placeholderTextColor", argb(0xFF0000FFU))));
  const char *after = rn_peer_get_colors_class(peer);
  EXPECT(after != nullptr);
  const std::string second = after != nullptr ? after : "";

  EXPECT(second != first);
  EXPECT(!gtk_widget_has_css_class(peer, first.c_str()));
  EXPECT(gtk_widget_has_css_class(peer, second.c_str()));

  const std::string css = cssOf(view);
  EXPECT(css.find("rgb(0,0,255)") != std::string::npos);
  EXPECT(css.find("rgb(255,0,0)") == std::string::npos);

  g_object_unref(view);
}

// `cursorColor` overrides the caret and nothing else, and a field given only
// `selectionColor` still gets a caret in it. That is React Native's own
// contract: `selectionColor` is documented as the highlight, the selection
// handle *and* the cursor, and `cursorColor` exists to peel the cursor off.
TEST(textinput_colours_the_caret_from_the_selection_until_cursor_colour_says_otherwise) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(
      view,
      makeTextInput(10, folly::dynamic::object("text", "")("selectionColor", argb(0xFFFF0000U))));
  EXPECT(cssOf(view).find("{ caret-color: rgb(255,0,0);") != std::string::npos);

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "")(
                                   "selectionColor", argb(0xFFFF0000U))(
                                   "cursorColor", argb(0xFF0000FFU))));
  const std::string css = cssOf(view);
  EXPECT(css.find("{ caret-color: rgb(0,0,255);") != std::string::npos);
  EXPECT(css.find("selection { background-color: rgb(255,0,0); }") != std::string::npos);

  g_object_unref(view);
}

// The multiline peer's placeholder is the one colour of the three that cannot go
// through CSS, because a GtkTextView has no placeholder node: this platform
// draws that placeholder itself, so the colour has to arrive as a value.
TEST(textinput_multiline_takes_the_placeholder_colour_as_a_value) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "")("multiline", true)(
                                   "placeholder", "type here")(
                                   "placeholderTextColor", argb(0xFF0000FFU))));

  GtkWidget *peer = rn_view_get_editable(view);
  EXPECT(rn_peer_is_multiline(peer));

  GdkRGBA colour = {0.0F, 0.0F, 0.0F, 0.0F};
  EXPECT(rn_peer_get_placeholder_color(peer, &colour));
  EXPECT_NEAR(colour.red, 0.0, 0.01);
  EXPECT_NEAR(colour.green, 0.0, 0.01);
  EXPECT_NEAR(colour.blue, 1.0, 0.01);
  // The app's own alpha, not the 0.45 the drawing dims an *inherited* colour
  // by: a colour somebody chose is not a colour to second-guess.
  EXPECT_NEAR(colour.alpha, 1.0, 0.01);

  // The class still goes on, because the other two colours do reach a
  // GtkTextView through CSS.
  EXPECT(rn_peer_get_colors_class(peer) != nullptr);

  // Dropping the prop puts the dimmed text colour back rather than leaving the
  // last colour stuck on the widget.
  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "")("multiline", true)(
                                   "placeholder", "type here")));
  EXPECT(!rn_peer_get_placeholder_color(peer, nullptr));

  // And a single-line field takes no value at all: GTK owns that placeholder,
  // and the CSS rule is the whole mechanism there.
  RnView *single = rn_view_new(11);
  g_object_ref_sink(single);
  manager.update(single,
                 makeTextInput(11,
                               folly::dynamic::object("text", "")("placeholder", "type here")(
                                   "placeholderTextColor", argb(0xFF0000FFU))));
  EXPECT(!rn_peer_get_placeholder_color(rn_view_get_editable(single), nullptr));
  EXPECT(cssOf(single).find("placeholder { color: rgb(0,0,255); }") != std::string::npos);

  g_object_unref(single);
  g_object_unref(view);
}

// `spellCheck`, which GTK has as an input *hint*: a suggestion to the input
// method rather than an instruction to a checker, and the nearest thing this
// toolkit has to the prop.
//
// `autoCorrect` has no hint at all -- `WORD_COMPLETION` offers completions
// rather than correcting what was typed -- so it is recorded rather than
// approximated, and the AppKit suite is where that half is asserted.
TEST(textinput_spell_check_becomes_an_input_hint) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("spellCheck", true)));
  GtkWidget *editable = rn_view_get_editable(view);
  EXPECT(editable != nullptr);
  EXPECT(editable != nullptr
         && (gtk_text_get_input_hints(GTK_TEXT(editable)) & GTK_INPUT_HINT_SPELLCHECK) != 0);

  // And the other way, which is the direction an app is likelier to ask for: a
  // search field that does not want its query underlined in red.
  manager.update(view, makeTextInput(10, folly::dynamic::object("spellCheck", false)));
  EXPECT((gtk_text_get_input_hints(GTK_TEXT(editable)) & GTK_INPUT_HINT_NO_SPELLCHECK) != 0);
  // The two are exclusive, and the hints are a bitmask: asking for one has to
  // take the other away rather than leaving both set.
  EXPECT((gtk_text_get_input_hints(GTK_TEXT(editable)) & GTK_INPUT_HINT_SPELLCHECK) == 0);

  g_object_unref(view);
}

// Unset is not false: a field that says nothing keeps whatever the input method
// was configured to do. See core/TextChecking.h.
TEST(textinput_an_unset_spell_check_sets_no_hint) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "plain")));
  GtkWidget *editable = rn_view_get_editable(view);
  EXPECT(editable != nullptr);
  const GtkInputHints hints =
      editable != nullptr ? gtk_text_get_input_hints(GTK_TEXT(editable)) : GTK_INPUT_HINT_NONE;
  EXPECT((hints & GTK_INPUT_HINT_SPELLCHECK) == 0);
  EXPECT((hints & GTK_INPUT_HINT_NO_SPELLCHECK) == 0);

  g_object_unref(view);
}

// The multiline peer is a GtkTextView, which has the same hints by a different
// call. Both are asserted because the two peers agree on almost nothing.
TEST(textinput_spell_check_reaches_a_multiline_peer) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(
      view,
      makeTextInput(10, folly::dynamic::object("multiline", true)("spellCheck", false)));
  GtkWidget *editable = rn_view_get_editable(view);
  EXPECT(editable != nullptr && GTK_IS_TEXT_VIEW(editable));
  EXPECT(editable != nullptr
         && (gtk_text_view_get_input_hints(GTK_TEXT_VIEW(editable))
             & GTK_INPUT_HINT_NO_SPELLCHECK)
             != 0);

  g_object_unref(view);
}

// `autoCapitalize`, which GTK has as three more input hints.
//
// React Native's default is `sentences`, as on iOS, and the prop is a plain
// enum with no "did not say" -- so a field that mentions nothing still asks for
// sentence capitalisation, which is faithful rather than surprising. The test
// pins that, because it is the kind of behaviour a later reader would take for
// a bug.
TEST(textinput_auto_capitalize_becomes_input_hints) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  const auto hintsOf = [&](const char *value) {
    manager.update(view,
                   makeTextInput(10, folly::dynamic::object("autoCapitalize", value)));
    return gtk_text_get_input_hints(GTK_TEXT(rn_view_get_editable(view)));
  };

  EXPECT((hintsOf("characters") & GTK_INPUT_HINT_UPPERCASE_CHARS) != 0);
  EXPECT((hintsOf("words") & GTK_INPUT_HINT_UPPERCASE_WORDS) != 0);
  EXPECT((hintsOf("sentences") & GTK_INPUT_HINT_UPPERCASE_SENTENCES) != 0);
  // Exclusive: asking for one takes the others off, the three sharing a mask.
  EXPECT((hintsOf("sentences") & GTK_INPUT_HINT_UPPERCASE_CHARS) == 0);
  EXPECT((hintsOf("sentences") & GTK_INPUT_HINT_UPPERCASE_WORDS) == 0);

  // `none` is the absence of all three rather than GTK_INPUT_HINT_LOWERCASE,
  // which would ask the input method to lowercase what was typed.
  const GtkInputHints none = hintsOf("none");
  EXPECT((none & GTK_INPUT_HINT_UPPERCASE_CHARS) == 0);
  EXPECT((none & GTK_INPUT_HINT_UPPERCASE_WORDS) == 0);
  EXPECT((none & GTK_INPUT_HINT_UPPERCASE_SENTENCES) == 0);
  EXPECT((none & GTK_INPUT_HINT_LOWERCASE) == 0);

  g_object_unref(view);
}

// The two hint groups share one mask, so one must not take the other off.
TEST(textinput_spell_check_and_capitalisation_do_not_clear_each_other) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("spellCheck", false)(
                                   "autoCapitalize", "characters")));
  const GtkInputHints hints = gtk_text_get_input_hints(GTK_TEXT(rn_view_get_editable(view)));
  EXPECT((hints & GTK_INPUT_HINT_NO_SPELLCHECK) != 0);
  EXPECT((hints & GTK_INPUT_HINT_UPPERCASE_CHARS) != 0);

  g_object_unref(view);
}

// `keyboardType`, which GTK has as an input purpose: what the text is for.
TEST(textinput_keyboard_type_becomes_an_input_purpose) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  const auto purposeOf = [&](const char *value) {
    manager.update(view, makeTextInput(10, folly::dynamic::object("keyboardType", value)));
    return gtk_text_get_input_purpose(GTK_TEXT(rn_view_get_editable(view)));
  };

  EXPECT(purposeOf("email-address") == GTK_INPUT_PURPOSE_EMAIL);
  EXPECT(purposeOf("url") == GTK_INPUT_PURPOSE_URL);
  EXPECT(purposeOf("phone-pad") == GTK_INPUT_PURPOSE_PHONE);
  // Digits only against a number: a number pad types digits, where `numeric`
  // and `decimal-pad` allow a separator and a sign.
  EXPECT(purposeOf("number-pad") == GTK_INPUT_PURPOSE_DIGITS);
  EXPECT(purposeOf("numeric") == GTK_INPUT_PURPOSE_NUMBER);
  EXPECT(purposeOf("decimal-pad") == GTK_INPUT_PURPOSE_NUMBER);
  // And the ones with no purpose to map to, which are ordinary text rather than
  // a refusal.
  EXPECT(purposeOf("default") == GTK_INPUT_PURPOSE_FREE_FORM);
  EXPECT(purposeOf("twitter") == GTK_INPUT_PURPOSE_FREE_FORM);
  EXPECT(purposeOf("visible-password") == GTK_INPUT_PURPOSE_FREE_FORM);

  g_object_unref(view);
}

// --- clearTextOnFocus and selectTextOnFocus ---------------------------------
//
// What happens when a field takes focus, which no host read until 2026-10-10.
//
// Through `handleFocus` rather than by focusing a widget, which this suite
// cannot do: a GtkText needs a realised window to take focus, and focusing an
// unmapped one is the stall `flushAutoFocus` documents. The signal handler calls
// exactly this, so what is exercised here is the whole of what the prop does --
// and the AppKit suite, which *can* focus, asserts the same two props through
// its own `handleFocus` from the other side.

TEST(textinput_clear_text_on_focus_empties_the_field) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(
      view,
      makeTextInput(10, folly::dynamic::object("text", "keep me")("clearTextOnFocus", true)));
  GtkWidget *editable = rn_view_get_editable(view);

  char *before = rn_peer_get_text(editable);
  EXPECT(before != nullptr && g_strcmp0(before, "keep me") == 0);
  g_free(before);

  manager.handleFocus(10);

  char *after = rn_peer_get_text(editable);
  EXPECT(after != nullptr && *after == '\0');
  g_free(after);

  g_object_unref(view);
}

// The control: a field that did not ask keeps its text, so a host that cleared
// unconditionally would fail here rather than pass the test above twice.
TEST(textinput_a_field_that_did_not_ask_keeps_its_text_on_focus) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "keep me")));
  manager.handleFocus(10);

  char *text = rn_peer_get_text(rn_view_get_editable(view));
  EXPECT(text != nullptr && g_strcmp0(text, "keep me") == 0);
  g_free(text);

  g_object_unref(view);
}

TEST(textinput_select_text_on_focus_selects_all_of_it) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(
      view,
      makeTextInput(10, folly::dynamic::object("text", "select me")("selectTextOnFocus", true)));
  GtkWidget *editable = rn_view_get_editable(view);

  manager.handleFocus(10);

  int start = -1;
  int end = -1;
  EXPECT(rn_peer_get_selection_bounds(editable, &start, &end));
  EXPECT_EQ(start, 0);
  EXPECT_EQ(end, 9);

  g_object_unref(view);
}

// And a field that did not ask has no selection, which is what makes the prop a
// difference here rather than a no-op: `gtk_text_grab_focus_without_selecting`
// is what this host focuses with, for the reason `flushAutoFocus` gives.
TEST(textinput_a_field_that_did_not_ask_is_not_selected_on_focus) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "select me")));
  manager.handleFocus(10);

  int start = -1;
  int end = -1;
  rn_peer_get_selection_bounds(rn_view_get_editable(view), &start, &end);
  EXPECT_EQ(start, end);

  g_object_unref(view);
}

// Both at once, in the order that means something: cleared first, so there is
// nothing left to select. A host that selected first would leave a selection
// over text it then threw away.
TEST(textinput_clearing_and_selecting_at_once_leaves_an_empty_field) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "both")("clearTextOnFocus", true)(
                                   "selectTextOnFocus", true)));
  GtkWidget *editable = rn_view_get_editable(view);

  manager.handleFocus(10);

  char *text = rn_peer_get_text(editable);
  EXPECT(text != nullptr && *text == '\0');
  g_free(text);
  int start = -1;
  int end = -1;
  rn_peer_get_selection_bounds(editable, &start, &end);
  EXPECT_EQ(start, end);

  g_object_unref(view);
}

// --- caretHidden and contextMenuHidden ---------------------------------------
//
// Two props a field can set that GTK has no property for, and the two go in by
// different routes: the caret through the same stylesheet the colours use, the
// menu by claiming the gesture that opens it.

TEST(textinput_caret_hidden_makes_the_caret_transparent) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("caretHidden", true)));

  // Transparent rather than absent: an absent rule is the theme's caret, which
  // is a caret you can see.
  const std::string css = cssOf(view);
  EXPECT(css.find("caret-color: rgba(0,0,0,0)") != std::string::npos);
  EXPECT(css.find("-gtk-secondary-caret-color: rgba(0,0,0,0)") != std::string::npos);

  g_object_unref(view);
}

TEST(textinput_caret_hidden_wins_over_a_cursor_colour) {
  // A field that asked for both means the hidden one: it cannot be both red and
  // invisible, and "hide it" is the more specific instruction.
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("caretHidden", true)(
                                   "cursorColor", argb(0xffff0000))));

  const std::string css = cssOf(view);
  EXPECT(css.find("caret-color: rgba(0,0,0,0)") != std::string::npos);
  EXPECT(css.find("rgb(255,0,0)") == std::string::npos);

  g_object_unref(view);
}

TEST(textinput_a_field_that_did_not_ask_keeps_its_caret) {
  // The control: no rule at all, which is the theme's caret.
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "visible")));

  EXPECT(cssOf(view).find("caret-color") == std::string::npos);

  g_object_unref(view);
}

TEST(textinput_context_menu_hidden_installs_the_gesture_that_swallows_it) {
  // What is observable without a display is the gesture: a capture-phase click
  // controller on the peer, which is what stops GtkText opening its menu. That
  // the menu then does not open needs a pointer and a window, which is the
  // end-to-end suite's half.
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("contextMenuHidden", true)));
  EXPECT(rn_peer_context_menu_hidden(rn_view_get_editable(view)) == TRUE);

  // And it goes away again when the prop does, rather than being a one-way
  // door: a field that stops asking gets its menu back.
  manager.update(view, makeTextInput(10, folly::dynamic::object("contextMenuHidden", false)));
  EXPECT(rn_peer_context_menu_hidden(rn_view_get_editable(view)) == FALSE);

  g_object_unref(view);
}

// And both are in the tree dump, which is where the cross-host diff reads them:
// each is the absence of something, so there is nothing else to compare.
TEST(textinput_the_hiding_props_are_reported_in_the_tree) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("caretHidden", true)(
                                   "contextMenuHidden", true)));
  gchar *hiding = rn_view_describe_tree(view);
  const std::string hidden(hiding);
  g_free(hiding);
  EXPECT(hidden.find("caret=hidden") != std::string::npos);
  EXPECT(hidden.find("context-menu=hidden") != std::string::npos);

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "visible")));
  gchar *plain = rn_view_describe_tree(view);
  const std::string shown(plain);
  g_free(plain);
  EXPECT(shown.find("caret=hidden") == std::string::npos);
  EXPECT(shown.find("context-menu=hidden") == std::string::npos);

  g_object_unref(view);
}

TEST(textinput_a_field_that_did_not_ask_keeps_its_context_menu) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "menu")));
  EXPECT(rn_peer_context_menu_hidden(rn_view_get_editable(view)) == FALSE);

  g_object_unref(view);
}
