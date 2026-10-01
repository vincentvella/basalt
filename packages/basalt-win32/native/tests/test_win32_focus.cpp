// Keyboard focus: what Tab stops on, in what order, and what Enter does.
//
// The same questions the GTK and AppKit suites ask, in the same order, because
// the answers have to match -- but here they are testable with no window at
// all. That is the difference this platform makes: on the other two, focus
// belongs to the toolkit and a test has to show a window to get at it. Here a
// React Native view is a C++ object with no window behind it, so the whole
// chain is Win32Focus.cpp's and can be driven directly.
//
// No emitter is attached to these hand-built shadow views, so the focus and
// blur events have nowhere to go and the click dispatch does nothing. What is
// observable is the part that decides where focus is, which is the part this
// project can get wrong on its own.

#include "TestHarness.h"

#include "Win32Focus.h"
#include "Win32MountingManager.h"

#include <react/renderer/components/view/ViewProps.h>

#include <cstdint>
#include <memory>
#include <sstream>

using basalt::Win32FocusManager;
using basalt::Win32MountingManager;
using basalt::win32::RnWin32View;
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

ShadowView makeAccessibleView(Tag tag, float y, bool accessible) {
  auto props = std::make_shared<ViewProps>();
  props->accessible = accessible;

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = y}, .size = {.width = 200, .height = 40}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  return view;
}

void mountAll(Win32MountingManager &manager, std::initializer_list<ShadowView> views) {
  ShadowViewMutationList mutations;
  int index = 0;
  for (const ShadowView &view : views) {
    mutations.push_back(ShadowViewMutation::CreateMutation(view));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, view, index++));
  }
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

} // namespace

TEST(win32_focus_accessible_views_are_the_ones_tab_stops_on) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 400);
  // React Native's `focusable` prop never reaches this platform, so `accessible`
  // is the signal -- and it is what <Pressable> sets on everything it renders.
  mountAll(manager,
           {makeAccessibleView(10, 0, true),
            makeAccessibleView(11, 50, false),
            makeAccessibleView(12, 100, true)});

  EXPECT(manager.viewForTag(10)->focusable());
  EXPECT(!manager.viewForTag(11)->focusable());
  EXPECT(manager.viewForTag(12)->focusable());
  // And it shows up in the tree dump, so the cross-host diff can say the prop
  // arrived on all three.
  EXPECT(root->describeTree().find("focusable") != std::string::npos);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_focus_tab_visits_focusable_views_in_tree_order_and_wraps) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 400);
  Win32FocusManager focus(&manager, root);
  mountAll(manager,
           {makeAccessibleView(10, 0, true),
            makeAccessibleView(11, 50, false),
            makeAccessibleView(12, 100, true)});

  // Nothing is focused when a window opens, on any of the three.
  EXPECT_EQ(static_cast<long>(focus.focusedTag()), 0L);

  EXPECT(focus.moveFocus(true));
  EXPECT_EQ(static_cast<long>(focus.focusedTag()), 10L);
  // 11 is not accessible, so Tab goes past it.
  EXPECT(focus.moveFocus(true));
  EXPECT_EQ(static_cast<long>(focus.focusedTag()), 12L);
  // And round again: a window's Tab order wraps.
  EXPECT(focus.moveFocus(true));
  EXPECT_EQ(static_cast<long>(focus.focusedTag()), 10L);
  // Backwards is the same order in reverse.
  EXPECT(focus.moveFocus(false));
  EXPECT_EQ(static_cast<long>(focus.focusedTag()), 12L);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_focus_moves_the_ring_with_it) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 400);
  Win32FocusManager focus(&manager, root);
  mountAll(manager, {makeAccessibleView(10, 0, true), makeAccessibleView(11, 50, true)});

  // The ring is this project's own here: a view is not a window, so there is no
  // Win32 focus for the painting to ask about.
  focus.moveFocus(true);
  EXPECT(manager.viewForTag(10)->showsFocusRing());
  EXPECT(!manager.viewForTag(11)->showsFocusRing());

  focus.moveFocus(true);
  EXPECT(!manager.viewForTag(10)->showsFocusRing());
  EXPECT(manager.viewForTag(11)->showsFocusRing());

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_focus_a_hidden_view_is_not_a_tab_stop) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 400);
  Win32FocusManager focus(&manager, root);
  mountAll(manager, {makeAccessibleView(10, 0, true), makeAccessibleView(11, 50, true)});

  // Tab stopping on something nobody can see is worse than Tab skipping it.
  manager.viewForTag(10)->setHidden(true);
  focus.moveFocus(true);
  EXPECT_EQ(static_cast<long>(focus.focusedTag()), 11L);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_focus_activating_nothing_is_not_a_click) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 400);
  Win32FocusManager focus(&manager, root);
  mountAll(manager, {makeAccessibleView(10, 0, true)});

  // Nothing has focus yet, so there is nothing to activate -- and reporting a
  // press with no target is how a keystroke reaches the wrong view.
  EXPECT(!focus.activateFocused());

  focus.moveFocus(true);
  // No emitter is attached to these hand-built shadow views, so the click has
  // nowhere to deliver. It must not crash, and the key is still consumed: that
  // is exactly the shape of Enter arriving between a Remove and its Delete.
  EXPECT(focus.activateFocused());

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_focus_keys_that_are_not_tab_or_enter_are_left_alone) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 400);
  Win32FocusManager focus(&manager, root);
  mountAll(manager, {makeAccessibleView(10, 0, true)});

  // Reported as unhandled, so the window's own shortcuts still see them. VK_F1
  // and VK_ESCAPE stand for everything that is not this file's business.
  EXPECT(!focus.handleKeyDown(0x70)); // VK_F1
  EXPECT(!focus.handleKeyDown(0x1B)); // VK_ESCAPE
  // And Enter with nothing focused goes on rather than being swallowed.
  EXPECT(!focus.handleKeyDown(0x0D)); // VK_RETURN

  EXPECT(focus.handleKeyDown(0x09)); // VK_TAB
  EXPECT_EQ(static_cast<long>(focus.focusedTag()), 10L);
  EXPECT(focus.handleKeyDown(0x0D)); // VK_RETURN, now that something has focus

  manager.destroySurfaceRoot(kSurfaceId);
}
