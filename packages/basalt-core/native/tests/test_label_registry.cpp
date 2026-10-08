// Resolving `accessibilityLabelledBy`, which is a lookup this project did not
// have: a view names another by its `nativeID` and the registry is keyed by tag.
//
// What is asserted here is the ordering, because that is the whole difficulty.
// Fabric mounts in tree order, so a field labelled by the text after it is
// mounted before its label exists; a relation resolved once at mount time would
// find nothing and stay wrong for the life of the screen. See core/LabelRegistry.h.

#include "TestHarness.h"

#include "LabelRegistry.h"

#include <sstream>

using basalt::LabelRegistry;

namespace {

// The resolution for one tag out of a batch of changes, and whether it was in
// there at all.
bool labelsFor(const std::vector<LabelRegistry::Resolved> &changes,
               LabelRegistry::Tag tag,
               std::vector<LabelRegistry::Tag> *out) {
  for (const auto &change : changes) {
    if (change.tag == tag) {
      *out = change.labels;
      return true;
    }
  }
  return false;
}

} // namespace

TEST(labels_resolve_an_id_to_the_view_that_has_it) {
  LabelRegistry registry;
  registry.setNativeId(20, "name-label");
  registry.setLabelledBy(10, {"name-label"});

  std::vector<LabelRegistry::Tag> labels;
  EXPECT(labelsFor(registry.changes(), 10, &labels));
  EXPECT_EQ((long)labels.size(), 1L);
  EXPECT_EQ((long)labels[0], 20L);
}

// The case that makes this a registry rather than a lookup: the label mounts
// second, which is what happens whenever the label comes after the field in the
// tree.
TEST(labels_a_relation_waits_for_an_id_that_has_not_arrived) {
  LabelRegistry registry;
  registry.setLabelledBy(10, {"name-label"});

  std::vector<LabelRegistry::Tag> labels;
  // Reported, and resolving to nothing: the host has a relation to reset rather
  // than a reference to a view that does not exist.
  EXPECT(labelsFor(registry.changes(), 10, &labels));
  EXPECT_EQ((long)labels.size(), 0L);

  registry.setNativeId(20, "name-label");
  EXPECT(labelsFor(registry.changes(), 10, &labels));
  EXPECT_EQ((long)labels.size(), 1L);
  EXPECT_EQ((long)labels[0], 20L);
}

// Order is the app's. A field labelled by a heading and a hint is read in that
// order, and sorting them would change what is said.
TEST(labels_keep_the_order_the_app_wrote) {
  LabelRegistry registry;
  registry.setNativeId(30, "hint");
  registry.setNativeId(20, "heading");
  registry.setLabelledBy(10, {"heading", "hint"});

  std::vector<LabelRegistry::Tag> labels;
  EXPECT(labelsFor(registry.changes(), 10, &labels));
  EXPECT_EQ((long)labels.size(), 2L);
  EXPECT_EQ((long)labels[0], 20L);
  EXPECT_EQ((long)labels[1], 30L);
}

// Nothing is reported twice. Applying an unchanged relation again tells
// assistive technology that something happened when nothing did.
TEST(labels_an_unchanged_relation_is_not_reported_again) {
  LabelRegistry registry;
  registry.setNativeId(20, "name-label");
  registry.setLabelledBy(10, {"name-label"});
  EXPECT_EQ((long)registry.changes().size(), 1L);
  EXPECT_EQ((long)registry.changes().size(), 0L);

  // Re-sending the same props, which every mutation does, changes nothing.
  registry.setNativeId(20, "name-label");
  registry.setLabelledBy(10, {"name-label"});
  EXPECT_EQ((long)registry.changes().size(), 0L);
}

// A label that goes away leaves a dangling reference, and a screen reader
// following it would read a view that is no longer on screen.
TEST(labels_a_label_that_is_unmounted_empties_the_relation) {
  LabelRegistry registry;
  registry.setNativeId(20, "name-label");
  registry.setLabelledBy(10, {"name-label"});
  registry.changes();

  registry.forget(20);
  std::vector<LabelRegistry::Tag> labels;
  EXPECT(labelsFor(registry.changes(), 10, &labels));
  EXPECT_EQ((long)labels.size(), 0L);
}

// And a view that stops asking is reported once, so the host can reset the
// relation, and then not again.
TEST(labels_a_view_that_stops_asking_is_reported_once) {
  LabelRegistry registry;
  registry.setNativeId(20, "name-label");
  registry.setLabelledBy(10, {"name-label"});
  registry.changes();

  registry.setLabelledBy(10, {});
  std::vector<LabelRegistry::Tag> labels;
  EXPECT(labelsFor(registry.changes(), 10, &labels));
  EXPECT_EQ((long)labels.size(), 0L);
  EXPECT_EQ((long)registry.changes().size(), 0L);
}

// A view cannot label itself, which an app can ask for by giving one view both
// props. Honouring it would be a cycle for anything walking the relation.
TEST(labels_a_view_does_not_label_itself) {
  LabelRegistry registry;
  registry.setNativeId(10, "mine");
  registry.setLabelledBy(10, {"mine"});

  std::vector<LabelRegistry::Tag> labels;
  EXPECT(labelsFor(registry.changes(), 10, &labels));
  EXPECT_EQ((long)labels.size(), 0L);
}

// An id taken over by another view follows the new one. Two views with the same
// nativeID is an app bug; what matters is that the old one letting go does not
// take the id with it.
TEST(labels_an_id_that_moves_follows_the_view_that_has_it) {
  LabelRegistry registry;
  registry.setNativeId(20, "name-label");
  registry.setLabelledBy(10, {"name-label"});
  registry.changes();

  registry.setNativeId(30, "name-label");
  std::vector<LabelRegistry::Tag> labels;
  EXPECT(labelsFor(registry.changes(), 10, &labels));
  EXPECT_EQ((long)labels[0], 30L);

  // The view that used to own it goes away without disturbing the new owner.
  registry.forget(20);
  EXPECT_EQ((long)registry.changes().size(), 0L);
}

// A view with no relation costs nothing, which is what `empty()` is for: the
// hosts ask it before doing any work per transaction.
TEST(labels_a_registry_with_no_relations_is_empty) {
  LabelRegistry registry;
  EXPECT(registry.empty());
  registry.setNativeId(20, "name-label");
  EXPECT(registry.empty());
  registry.setLabelledBy(10, {"name-label"});
  EXPECT(!registry.empty());
}

// But not while a relation it applied is still on a widget, which is the half
// that is easy to get wrong: a host checking `empty()` first would skip the one
// transaction that had to take the relation off, and it would stay there for the
// rest of the screen's life. Caught by writing the host side, not by this test.
TEST(labels_a_registry_is_not_empty_while_a_relation_is_still_applied) {
  LabelRegistry registry;
  registry.setNativeId(20, "name-label");
  registry.setLabelledBy(10, {"name-label"});
  registry.changes();

  registry.setLabelledBy(10, {});
  EXPECT(!registry.empty());
  // Reported once, so the host can reset it, and then it really is empty.
  EXPECT_EQ((long)registry.changes().size(), 1L);
  EXPECT(registry.empty());
}
