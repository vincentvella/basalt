// Tests for the accessibility mapping.
//
// GTK ships assertion helpers for exactly this (gtktestatcontext.h), so what a
// screen reader would be told can be read back rather than inferred. That is
// unusual and worth using: accessibility is otherwise the easiest thing in a UI
// to believe you have done.

#include "TestHarness.h"

#include "GtkMountingManager.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/RawProps.h>
#include <react/renderer/core/RawPropsParser.h>

#include <sstream>

using facebook::react::AccessibilityState;
using facebook::react::AccessibilityValue;
using facebook::react::ImportantForAccessibility;
using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TransactionTelemetry;
using facebook::react::ViewProps;

namespace {

constexpr SurfaceId kSurfaceId = 1;

// Builds a view with whatever accessibility props the caller wants to set.
ShadowView makeAccessibleView(Tag tag, const std::function<void(ViewProps &)> &configure) {
  auto props = std::make_shared<ViewProps>();
  configure(*props);

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0}, .size = {.width = 50, .height = 50}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  return view;
}

// The same, for a <TextInput>. Its props have to be real TextInputProps or the
// mounting manager builds no peer, and those are only reachable through React
// Native's own parser -- the fields that matter are const.
ShadowView makeTextInputView(Tag tag, const std::function<void(ViewProps &)> & /*configure*/,
                             const std::string &label) {
  static const facebook::react::RawPropsParser parser = []() {
    facebook::react::RawPropsParser prepared;
    prepared.prepare<facebook::react::TextInputProps>();
    return prepared;
  }();

  static const auto contextContainer =
      std::make_shared<const facebook::react::ContextContainer>();
  const facebook::react::PropsParserContext context{kSurfaceId, *contextContainer};

  folly::dynamic raw = folly::dynamic::object("text", "")("accessibilityLabel", label);
  facebook::react::RawProps rawProps{std::move(raw)};
  rawProps.parse(parser);

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0}, .size = {.width = 200, .height = 40}};

  ShadowView view;
  view.componentName = "TextInput";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = std::make_shared<const facebook::react::TextInputProps>(
      context, facebook::react::TextInputProps{}, rawProps);
  view.layoutMetrics = metrics;
  return view;
}

// Mounts one view and hands back the widget.
RnView *mountOne(basalt::GtkMountingManager &manager, const ShadowView &view) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(view));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, view, 0));
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));

  RnView *root = manager.getSurfaceRoot(kSurfaceId);
  return RN_VIEW(gtk_widget_get_first_child(GTK_WIDGET(root)));
}

// An Update mutation against a view already mounted, which nothing needed until
// a prop that can change after mount had to be tested.
void updateOne(basalt::GtkMountingManager &manager,
               Tag tag,
               const std::function<void(ViewProps &)> &configure) {
  const ShadowView before = makeAccessibleView(tag, [](ViewProps &) {});
  const ShadowView after = makeAccessibleView(tag, configure);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::UpdateMutation(before, after, kSurfaceId));
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 2, std::move(mutations), TransactionTelemetry{}));
}

} // namespace

TEST(accessibility_role_maps_to_a_gtk_role) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *button = mountOne(manager, makeAccessibleView(10, [](ViewProps &props) {
                              props.accessibilityRole = "button";
                            }));
  EXPECT_EQ(static_cast<int>(gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(button))),
            static_cast<int>(GTK_ACCESSIBLE_ROLE_BUTTON));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(unknown_accessibility_role_falls_back_to_generic) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  // A wrong role is worse than no role: it makes a widget announce itself as
  // something it is not.
  RnView *view = mountOne(manager, makeAccessibleView(11, [](ViewProps &props) {
                            props.accessibilityRole = "somethingReactNativeInvented";
                          }));
  EXPECT_EQ(static_cast<int>(gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(view))),
            static_cast<int>(GTK_ACCESSIBLE_ROLE_GENERIC));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(accessibility_label_and_hint_reach_the_accessible) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(12, [](ViewProps &props) {
                            props.accessibilityLabel = "Play";
                            props.accessibilityHint = "Starts the track";
                          }));

  char *mismatch = gtk_test_accessible_check_property(
      GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_LABEL, "Play");
  EXPECT(mismatch == nullptr);
  g_free(mismatch);

  mismatch = gtk_test_accessible_check_property(
      GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Starts the track");
  EXPECT(mismatch == nullptr);
  g_free(mismatch);

  manager.destroySurfaceRoot(kSurfaceId);
}

// A field's label has to be on the field, not on the view wrapping it. The
// peer is a real GtkText and is what AT-SPI presents as a text box, so a label
// left on the wrapper is a labelled field that announces no name.
TEST(a_text_input_label_lands_on_the_peer_not_the_wrapper) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeTextInputView(13, nullptr, "Your name"));
  GtkWidget *peer = rn_view_get_editable(view);
  EXPECT(peer != nullptr);

  char *mismatch = gtk_test_accessible_check_property(
      GTK_ACCESSIBLE(peer), GTK_ACCESSIBLE_PROPERTY_LABEL, "Your name");
  EXPECT(mismatch == nullptr);
  g_free(mismatch);

  // And not announced twice: the wrapper does not also carry the name. Checked
  // by asking for a mismatch rather than for an empty string, because an unset
  // GTK property is not the empty one and does not compare equal to it.
  mismatch = gtk_test_accessible_check_property(
      GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_LABEL, "Your name");
  EXPECT(mismatch != nullptr);
  g_free(mismatch);

  // The wrapper is scaffolding rather than a control, so it is presentational
  // and a screen reader walks past it to the peer. Decided at construction
  // because a GtkAccessible role cannot be changed later.
  EXPECT_EQ(static_cast<int>(gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(view))),
            static_cast<int>(GTK_ACCESSIBLE_ROLE_PRESENTATION));

  // And the tree dump says nothing about it. React Native has no role here to
  // report, AppKit prints none either, and a name invented for GTK's benefit
  // would make the two hosts disagree on this view alone.
  std::ostringstream dump;
  char *text = rn_view_describe_tree(view);
  dump << text;
  g_free(text);
  EXPECT(dump.str().find("role=") == std::string::npos);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(accessibility_state_reaches_the_accessible) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(13, [](ViewProps &props) {
                            AccessibilityState state;
                            state.disabled = true;
                            state.selected = true;
                            state.checked = AccessibilityState::Checked;
                            props.accessibilityState = state;
                          }));

  EXPECT(gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_DISABLED));
  EXPECT(gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_SELECTED));
  EXPECT(gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_CHECKED));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(no_accessibility_state_leaves_checked_unset) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  // A view that never says anything about "checked" is not an unchecked
  // checkbox, and a screen reader should not announce it as one.
  RnView *view = mountOne(manager, makeAccessibleView(14, [](ViewProps &props) { (void)props; }));

  EXPECT(!gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_CHECKED));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(elements_hidden_marks_the_view_hidden) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(15, [](ViewProps &props) {
                            props.accessibilityElementsHidden = true;
                          }));
  EXPECT(gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_HIDDEN));

  RnView *shown = mountOne(manager, makeAccessibleView(16, [](ViewProps &props) { (void)props; }));
  char *mismatch = gtk_test_accessible_check_state(
      GTK_ACCESSIBLE(shown), GTK_ACCESSIBLE_STATE_HIDDEN, FALSE);
  EXPECT(mismatch == nullptr);
  g_free(mismatch);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(important_for_accessibility_no_hide_descendants_hides_too) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(17, [](ViewProps &props) {
                            props.importantForAccessibility =
                                ImportantForAccessibility::NoHideDescendants;
                          }));
  EXPECT(gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_HIDDEN));

  manager.destroySurfaceRoot(kSurfaceId);
}

// accessibilityValue: a range, a position in it, and a text form a screen reader
// prefers over the number. React Native's four parts are independent optionals,
// which is the whole difficulty: an absent part must be absent rather than zero.

TEST(accessibility_value_reaches_the_accessible) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(30, [](ViewProps &props) {
                            AccessibilityValue value;
                            value.min = 0;
                            value.max = 10;
                            value.now = 7;
                            props.accessibilityValue = value;
                          }));

  EXPECT(gtk_test_accessible_has_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_VALUE_MIN));
  EXPECT(gtk_test_accessible_has_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_VALUE_MAX));
  EXPECT(gtk_test_accessible_has_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_VALUE_NOW));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(an_accessibility_value_without_a_range_does_not_invent_one) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  // `now` alone. A view that never said what its range is must not be announced
  // as sitting at the bottom of a range from zero: absent is not zero, which is
  // why the seam carries a sentinel rather than a number.
  RnView *view = mountOne(manager, makeAccessibleView(31, [](ViewProps &props) {
                            AccessibilityValue value;
                            value.now = 3;
                            props.accessibilityValue = value;
                          }));

  EXPECT(gtk_test_accessible_has_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_VALUE_NOW));
  EXPECT(!gtk_test_accessible_has_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_VALUE_MIN));
  EXPECT(!gtk_test_accessible_has_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_VALUE_MAX));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(an_accessibility_value_can_be_text_rather_than_a_number) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  // "Tuesday" reads better than "3", which is what the text form is for.
  RnView *view = mountOne(manager, makeAccessibleView(32, [](ViewProps &props) {
                            AccessibilityValue value;
                            value.text = "Tuesday";
                            props.accessibilityValue = value;
                          }));

  EXPECT(gtk_test_accessible_has_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT));
  EXPECT(!gtk_test_accessible_has_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_VALUE_NOW));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(a_view_with_no_accessibility_value_says_nothing_about_one) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(33, [](ViewProps &props) { (void)props; }));

  EXPECT(!gtk_test_accessible_has_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_VALUE_NOW));
  EXPECT(!gtk_test_accessible_has_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT));

  manager.destroySurfaceRoot(kSurfaceId);
}

// A role that changes after mount. GTK's `accessible-role` is construct-only, so
// the role cannot follow; the description a screen reader reads out can, and
// that is what these pin. The limitation is real and recorded, so what is
// asserted is the mitigation and not a claim that the role changed.

TEST(a_role_that_changes_after_mount_at_least_changes_what_is_announced) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  // Mounted as a button, then told it is a checkbox. GTK keeps the button role.
  RnView *view = mountOne(manager, makeAccessibleView(40, [](ViewProps &props) {
                            props.accessibilityRole = "button";
                          }));
  EXPECT(!gtk_test_accessible_has_property(GTK_ACCESSIBLE(view),
                                           GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION));

  updateOne(manager, 40, [](ViewProps &props) { props.accessibilityRole = "checkbox"; });

  // The role is still button, which is the limitation, and the description now
  // says checkbox, which is the part that could be done.
  EXPECT(gtk_test_accessible_has_property(GTK_ACCESSIBLE(view),
                                          GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(a_role_that_does_not_change_describes_itself_as_nothing) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  // A description identical to the view's own role would be noise read out on
  // every visit, so an unchanged role must leave it unset.
  RnView *view = mountOne(manager, makeAccessibleView(41, [](ViewProps &props) {
                            props.accessibilityRole = "button";
                          }));

  updateOne(manager, 41, [](ViewProps &props) { props.accessibilityRole = "button"; });

  EXPECT(!gtk_test_accessible_has_property(GTK_ACCESSIBLE(view),
                                           GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION));

  manager.destroySurfaceRoot(kSurfaceId);
}

// `accessibilityLabelledBy`: another view, named by its `nativeID`, whose text
// names this one. GTK models it as a relation, which is what it is, and ships an
// assertion helper for relations.
//
// Two views per test, mounted in both orders, because the order is the whole
// difficulty: Fabric mounts in tree order, so a field labelled by the text after
// it is mounted before its label exists. See core/LabelRegistry.h.
namespace {

// Two views in one transaction: the first is the field, the second its label.
// `mountOne` mounts one and hands back a widget; this hands back both.
void mountPair(basalt::GtkMountingManager &manager,
               const ShadowView &first,
               const ShadowView &second,
               int transaction) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(first));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, first, 0));
  mutations.push_back(ShadowViewMutation::CreateMutation(second));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, second, 1));
  manager.applyTransaction(
      kSurfaceId,
      MountingTransaction(kSurfaceId, transaction, std::move(mutations), TransactionTelemetry{}));
}

} // namespace

TEST(accessibility_labelled_by_resolves_a_native_id_to_a_relation) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  const ShadowView field = makeAccessibleView(10, [](ViewProps &props) {
    props.accessibilityLabelledBy.value = {"name-label"};
  });
  const ShadowView label =
      makeAccessibleView(20, [](ViewProps &props) { props.nativeId = "name-label"; });
  mountPair(manager, field, label, 1);

  RnView *view = manager.viewForTag(10);
  EXPECT(view != nullptr);
  EXPECT(gtk_test_accessible_has_relation(GTK_ACCESSIBLE(view),
                                          GTK_ACCESSIBLE_RELATION_LABELLED_BY));
  // And it points at the right view, which the helper compares by reference.
  char *mismatch = gtk_test_accessible_check_relation(
      GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_RELATION_LABELLED_BY, manager.viewForTag(20), nullptr);
  EXPECT(mismatch == nullptr);
  g_free(mismatch);

  // The dump says so too, by tag, which is what the end-to-end run reads.
  char *tree = rn_view_describe_tree(manager.getSurfaceRoot(kSurfaceId));
  const std::string dumped(tree);
  g_free(tree);
  EXPECT(dumped.find("labelled-by=20") != std::string::npos);
}

// The label mounted in the same transaction but *after* the field, which is the
// ordinary case: a field followed by its caption. A resolution done as the props
// arrived would find nothing here.
TEST(accessibility_labelled_by_waits_for_a_label_that_mounts_later) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  // The field alone in the first transaction, so the id does not exist yet.
  const ShadowView field = makeAccessibleView(10, [](ViewProps &props) {
    props.accessibilityLabelledBy.value = {"name-label"};
  });
  mountOne(manager, field);
  RnView *view = manager.viewForTag(10);
  EXPECT(!gtk_test_accessible_has_relation(GTK_ACCESSIBLE(view),
                                           GTK_ACCESSIBLE_RELATION_LABELLED_BY));

  // The label in a second one, which is what resolves it.
  const ShadowView label =
      makeAccessibleView(20, [](ViewProps &props) { props.nativeId = "name-label"; });
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(label));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, label, 1));
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 2, std::move(mutations), TransactionTelemetry{}));

  EXPECT(gtk_test_accessible_has_relation(GTK_ACCESSIBLE(view),
                                          GTK_ACCESSIBLE_RELATION_LABELLED_BY));
}

// Several labels, in the order the app wrote them: a field named by a heading
// and a hint is read in that order, and sorting them would change what is said.
TEST(accessibility_labelled_by_keeps_every_label_in_order) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  const ShadowView field = makeAccessibleView(10, [](ViewProps &props) {
    props.accessibilityLabelledBy.value = {"heading", "hint"};
  });
  const ShadowView heading =
      makeAccessibleView(20, [](ViewProps &props) { props.nativeId = "heading"; });
  mountPair(manager, field, heading, 1);

  const ShadowView hint =
      makeAccessibleView(30, [](ViewProps &props) { props.nativeId = "hint"; });
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(hint));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, hint, 2));
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 2, std::move(mutations), TransactionTelemetry{}));

  char *tree = rn_view_describe_tree(manager.getSurfaceRoot(kSurfaceId));
  const std::string dumped(tree);
  g_free(tree);
  EXPECT(dumped.find("labelled-by=20,30") != std::string::npos);
}

// A label that is unmounted takes the relation with it. A reference left behind
// would have a screen reader read a view that is no longer on screen.
TEST(accessibility_labelled_by_comes_apart_when_the_label_goes_away) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  const ShadowView field = makeAccessibleView(10, [](ViewProps &props) {
    props.accessibilityLabelledBy.value = {"name-label"};
  });
  const ShadowView label =
      makeAccessibleView(20, [](ViewProps &props) { props.nativeId = "name-label"; });
  mountPair(manager, field, label, 1);
  RnView *view = manager.viewForTag(10);
  EXPECT(gtk_test_accessible_has_relation(GTK_ACCESSIBLE(view),
                                          GTK_ACCESSIBLE_RELATION_LABELLED_BY));

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, label, 1));
  mutations.push_back(ShadowViewMutation::DeleteMutation(label));
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 2, std::move(mutations), TransactionTelemetry{}));

  EXPECT(!gtk_test_accessible_has_relation(GTK_ACCESSIBLE(view),
                                           GTK_ACCESSIBLE_RELATION_LABELLED_BY));
  char *tree = rn_view_describe_tree(manager.getSurfaceRoot(kSurfaceId));
  const std::string dumped(tree);
  g_free(tree);
  EXPECT(dumped.find("labelled-by=") == std::string::npos);
}

// An id that names nothing is not a relation. A view labelled by a typo should
// announce itself as it would have anyway, not point at nothing.
TEST(accessibility_labelled_by_an_id_that_names_nothing_is_no_relation) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(10, [](ViewProps &props) {
                            props.accessibilityLabelledBy.value = {"no-such-view"};
                          }));
  EXPECT(!gtk_test_accessible_has_relation(GTK_ACCESSIBLE(view),
                                           GTK_ACCESSIBLE_RELATION_LABELLED_BY));
}

// `accessibilityLiveRegion`: a status message read out when it changes.
//
// Nothing in an automated run is connected to AT-SPI, so what is asserted is the
// decision rather than the speech: `rn_view_announce` records the text it passed
// to `gtk_accessible_announce`, and these read that back. The rules themselves --
// what counts as a change -- are core/LiveRegions.h's and are tested there.
//
// The text here comes from the accessibility label, which is what an icon-only
// status has; a <Text> inside the region is the other source and is asserted
// against `rn_view_collect_text` in test_viewprops.cpp.

TEST(accessibility_a_live_region_says_nothing_when_it_appears) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *status = mountOne(manager, makeAccessibleView(10, [](ViewProps &props) {
                              props.accessibilityLiveRegion =
                                  facebook::react::AccessibilityLiveRegion::Polite;
                              props.accessibilityLabel = "Saving";
                            }));
  // Mounting a status line is not news: the screen appeared, nothing changed.
  EXPECT(rn_view_get_last_announcement(status) == nullptr);
}

TEST(accessibility_a_live_region_announces_a_change) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *status = mountOne(manager, makeAccessibleView(10, [](ViewProps &props) {
                              props.accessibilityLiveRegion =
                                  facebook::react::AccessibilityLiveRegion::Polite;
                              props.accessibilityLabel = "Saving";
                            }));

  updateOne(manager, 10, [](ViewProps &props) {
    props.accessibilityLiveRegion = facebook::react::AccessibilityLiveRegion::Polite;
    props.accessibilityLabel = "Saved";
  });
  EXPECT(rn_view_get_last_announcement(status) != nullptr);
  EXPECT_EQ(std::string(rn_view_get_last_announcement(status)), std::string("Saved"));
}

// A view without the prop never announces, whatever its label does. The whole
// point of the prop is that most views are not live regions.
TEST(accessibility_a_view_that_is_not_a_live_region_stays_quiet) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(10, [](ViewProps &props) {
                            props.accessibilityLabel = "Saving";
                          }));
  updateOne(manager, 10, [](ViewProps &props) { props.accessibilityLabel = "Saved"; });
  EXPECT(rn_view_get_last_announcement(view) == nullptr);
}

// And a region that stops being one goes quiet, which is what a screen that
// turns its status line into ordinary text means.
TEST(accessibility_a_live_region_that_stops_being_one_goes_quiet) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *status = mountOne(manager, makeAccessibleView(10, [](ViewProps &props) {
                              props.accessibilityLiveRegion =
                                  facebook::react::AccessibilityLiveRegion::Assertive;
                              props.accessibilityLabel = "Saving";
                            }));
  updateOne(manager, 10, [](ViewProps &props) {
    props.accessibilityLiveRegion = facebook::react::AccessibilityLiveRegion::Assertive;
    props.accessibilityLabel = "Saved";
  });
  EXPECT_EQ(std::string(rn_view_get_last_announcement(status)), std::string("Saved"));

  updateOne(manager, 10, [](ViewProps &props) { props.accessibilityLabel = "Saved again"; });
  // Still the old announcement: nothing new was said.
  EXPECT_EQ(std::string(rn_view_get_last_announcement(status)), std::string("Saved"));
}
