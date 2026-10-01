// Tests for the touch dispatcher: does a mouse message become the right touch,
// against the right view, and does a gesture recogniser see the same geometry?
//
// Hit testing itself is not here -- it is a pure function of the view tree and
// needs no React Native, so it lives in tests/test_win32_hittest.cpp with the
// rest of the view layer. What is here is everything above it: the state
// machine that decides a move is a drag rather than hover, the tag a touch is
// reported against, and the hit chain a gesture handler is offered.
//
// Nothing below synthesises a real mouse event. Doing that on Windows means
// SendInput, which moves the actual cursor and so cannot run beside anything
// else on the machine; the same objection CGEvent raises on macOS. So these
// enter at `dispatchTouch*`, exactly where the window procedure does, and what
// goes untested is that Windows routes WM_LBUTTONDOWN here at all.

#include "TestHarness.h"

#include "PointerButtons.h"

#include "Gestures.h"
#include "Win32MountingManager.h"
#include "Win32TouchDispatcher.h"

#include <react/renderer/components/view/ViewProps.h>

#include <cstdint>
#include <string>
#include <vector>

using basalt::Win32MountingManager;
using basalt::Win32TouchDispatcher;
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

ShadowView makeView(Tag tag, float x, float y, float width, float height) {
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = x, .y = y}, .size = {.width = width, .height = height}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = std::make_shared<ViewProps>();
  view.layoutMetrics = metrics;
  return view;
}

void apply(Win32MountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

// Creates and inserts one view under `parent` in a single transaction, which is
// how Fabric would send it.
void mount(Win32MountingManager &manager,
           Tag parent,
           Tag tag,
           float x,
           float y,
           float width,
           float height) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(tag, x, y, width, height)));
  mutations.push_back(
      ShadowViewMutation::InsertMutation(parent, makeView(tag, x, y, width, height), 0));
  apply(manager, std::move(mutations));
}

// The gesture registry is a process-wide singleton -- there is one pointer --
// so a test that attaches anything has to start by dropping what the last one
// left behind, and end by dropping its own.
struct GestureScope {
  std::vector<std::string> states;

  GestureScope() {
    clear();
    basalt::gestures().setEmitter([this](const std::string &event, folly::dynamic payload) {
      if (event != "onGestureHandlerStateChange") {
        return;
      }
      states.push_back(std::to_string(static_cast<int>(payload["state"].asDouble())));
    });
  }

  ~GestureScope() {
    basalt::gestures().setEmitter(nullptr);
    clear();
  }

  GestureScope(const GestureScope &) = delete;
  GestureScope &operator=(const GestureScope &) = delete;

  static void clear() {
    for (int tag = 1; tag <= 32; tag++) {
      basalt::gestures().drop(tag);
    }
  }

  // "2,4,5" -- readable in a failure message, which a vector is not.
  std::string joined() const {
    std::string out;
    for (const std::string &state : states) {
      if (!out.empty()) {
        out += ",";
      }
      out += state;
    }
    return out;
  }
};

} // namespace

// The dispatcher against a real mounted tree: a tap has to find the tag the
// mounting manager registered, not just some view.
TEST(win32_a_synthesised_tap_finds_the_mounted_view) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, 10, 40, 50, 120, 60);

  EXPECT_EQ(static_cast<long>(basalt::hitTestTag(root, 50, 60)), 10L);
  // Inside the surface but over nothing: the root is the honest answer, because
  // the responder system needs a target for every press inside the surface.
  EXPECT_EQ(static_cast<long>(basalt::hitTestTag(root, 5, 5)),
            static_cast<long>(kSurfaceId));
  EXPECT_EQ(static_cast<long>(basalt::hitTestTag(root, 5000, 5000)), 0L);

  // No emitter is attached to these hand-built shadow views, so the tap has
  // nowhere to deliver. It must not crash: that is exactly the shape of a press
  // arriving between a Remove and its Delete.
  Win32TouchDispatcher dispatcher(&manager, root);
  dispatcher.synthesiseTap(50, 60);
  dispatcher.synthesiseTap(5000, 5000);

  manager.destroySurfaceRoot(kSurfaceId);
}

// A move with no button down is hover, and the touch model has no place for it.
// Reporting it would look to the responder system like a finger dragging across
// the screen at all times, which cancels every press before it can fire.
TEST(win32_a_move_before_a_press_is_ignored) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, 10, 0, 0, 100, 100);

  Win32TouchDispatcher dispatcher(&manager, root);
  dispatcher.dispatchTouchMove(10, 10);
  EXPECT(!dispatcher.isDown());
  // An end with no start is a no-op too, which is the case the host hits when
  // a WM_LBUTTONUP arrives after the surface was torn down.
  dispatcher.dispatchTouchEnd(10, 10, basalt::PointerButton::Primary);
  dispatcher.dispatchTouchCancel();
  EXPECT(!dispatcher.isDown());

  manager.destroySurfaceRoot(kSurfaceId);
}

// The press/release pair, which is the state the host reads to decide whether
// to hold capture. A press that hits nothing must not arm it: releasing capture
// that was never taken is how a window ends up ignoring the mouse entirely.
TEST(win32_a_press_is_only_outstanding_between_down_and_up) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, 10, 40, 50, 120, 60);

  Win32TouchDispatcher dispatcher(&manager, root);
  EXPECT(!dispatcher.isDown());
  dispatcher.dispatchTouchStart(50, 60, basalt::PointerButton::Primary);
  EXPECT(dispatcher.isDown());
  dispatcher.dispatchTouchMove(60, 70);
  EXPECT(dispatcher.isDown());
  dispatcher.dispatchTouchEnd(60, 70, basalt::PointerButton::Primary);
  EXPECT(!dispatcher.isDown());

  // Outside the surface entirely: nothing was hit, so nothing is down.
  dispatcher.dispatchTouchStart(5000, 5000, basalt::PointerButton::Primary);
  EXPECT(!dispatcher.isDown());

  manager.destroySurfaceRoot(kSurfaceId);
}

// A press with no surface root at all -- which is what the host has between
// creating the window and starting the surface.
TEST(win32_a_press_with_no_surface_is_a_no_op) {
  Win32TouchDispatcher dispatcher(nullptr, nullptr);
  dispatcher.dispatchTouchStart(10, 10, basalt::PointerButton::Primary);
  dispatcher.dispatchTouchMove(20, 20);
  dispatcher.dispatchTouchEnd(20, 20, basalt::PointerButton::Primary);
  EXPECT(!dispatcher.isDown());
}

// The dispatcher's other half: a pointer offered to react-native-gesture-handler
// with the views under it. A pan attached to a mounted view has to see the
// press, the movement and the release, which is BEGAN, ACTIVE, END.
TEST(win32_a_drag_drives_a_pan_recogniser) {
  GestureScope scope;
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, 10, 40, 50, 120, 60);

  basalt::gestures().create("PanGestureHandler", 1, folly::dynamic::object());
  basalt::gestures().attach(1, 10);

  Win32TouchDispatcher dispatcher(&manager, root);
  dispatcher.synthesiseDrag(50, 60, 150, 60, 20);

  EXPECT_EQ(scope.joined(), std::string("2,4,5"));

  manager.destroySurfaceRoot(kSurfaceId);
}

// A handler attached to an *ancestor* of the pressed view still sees the
// gesture, which is the whole reason the dispatcher builds a chain rather than
// reporting the one tag a touch is reported against. Getting this wrong means a
// <GestureDetector> wrapping a screen never fires, because the press always
// lands on something inside it.
TEST(win32_a_gesture_on_an_ancestor_sees_the_press) {
  GestureScope scope;
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, 10, 40, 50, 200, 200);
  mount(manager, 10, 11, 10, 10, 50, 50);

  // The press lands on 11; the handler is on its parent.
  EXPECT_EQ(static_cast<long>(basalt::hitTestTag(root, 60, 70)), 11L);

  basalt::gestures().create("PanGestureHandler", 1, folly::dynamic::object());
  basalt::gestures().attach(1, 10);

  Win32TouchDispatcher dispatcher(&manager, root);
  dispatcher.synthesiseDrag(60, 70, 160, 70, 20);

  EXPECT_EQ(scope.joined(), std::string("2,4,5"));

  manager.destroySurfaceRoot(kSurfaceId);
}

// The chain carries each view's origin in root coordinates, and a scrolled
// ancestor moves it. The recogniser is what reads that origin, so a scroll that
// the chain does not account for puts a gesture's x/y in the wrong place -- and
// since hit testing already follows the scroll, the two would disagree about
// the same press.
TEST(win32_the_hit_chain_follows_a_scroll_offset) {
  GestureScope scope;
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, 10, 0, 0, 200, 200);
  mount(manager, 10, 11, 0, 100, 50, 50);

  // Scrolled down by 100, so the child that was at y=100 is now at the top.
  manager.viewForTag(10)->setScrollOffset(0, 100);
  EXPECT_EQ(static_cast<long>(basalt::hitTestTag(root, 10, 10)), 11L);

  basalt::gestures().create("PanGestureHandler", 1, folly::dynamic::object());
  basalt::gestures().attach(1, 11);

  Win32TouchDispatcher dispatcher(&manager, root);
  dispatcher.synthesiseDrag(10, 10, 110, 10, 20);

  EXPECT_EQ(scope.joined(), std::string("2,4,5"));

  manager.destroySurfaceRoot(kSurfaceId);
}

// A native handler that activates owns the pointer, and React Native's touch
// has to be cancelled or a pan over a <Pressable> both pans and presses it.
// The observable half here is that the dispatcher stops considering a press
// outstanding the moment the handler activates.
TEST(win32_an_activated_gesture_takes_the_pointer) {
  GestureScope scope;
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, 10, 0, 0, 200, 200);

  // A native handler activates on contact rather than waiting for movement,
  // which makes the handover observable from a single press.
  basalt::gestures().create("NativeViewGestureHandler", 1, folly::dynamic::object());
  basalt::gestures().attach(1, 10);

  Win32TouchDispatcher dispatcher(&manager, root);
  dispatcher.dispatchTouchStart(50, 50, basalt::PointerButton::Primary);
  EXPECT(!dispatcher.isDown());

  dispatcher.dispatchTouchEnd(50, 50, basalt::PointerButton::Primary);

  manager.destroySurfaceRoot(kSurfaceId);
}

// Hover runs only for views that asked for it, and the ask arrives as props on
// every Create and Update. Recorded by the mutation walk, so a cursor crossing
// an app that uses no hover costs a hit test and nothing else.
TEST(win32_a_hover_listener_is_remembered_from_the_props) {
  Win32MountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  ShadowView listening = makeView(10, 40, 50, 120, 60);
  auto props = std::make_shared<ViewProps>();
  props->events[facebook::react::ViewEvents::Offset::PointerEnter] = true;
  listening.props = props;

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(listening));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, listening, 0));
  apply(manager, std::move(mutations));

  EXPECT_EQ(manager.hoverListenersForTag(10),
            static_cast<std::uint16_t>(basalt::HoverListenerEnter));
  // A view that never asked is not stored at all.
  EXPECT_EQ(manager.hoverListenersForTag(kSurfaceId),
            static_cast<std::uint16_t>(basalt::HoverListenerNone));

  // An update can take the listener away again, and leaving the old mask behind
  // would mean dispatching to a view that stopped listening.
  ShadowViewMutationList updates;
  updates.push_back(ShadowViewMutation::UpdateMutation(
      listening, makeView(10, 40, 50, 120, 60), kSurfaceId));
  apply(manager, std::move(updates));
  EXPECT_EQ(manager.hoverListenersForTag(10),
            static_cast<std::uint16_t>(basalt::HoverListenerNone));

  manager.destroySurfaceRoot(kSurfaceId);
}

// The same shape as the synthesised tap: no emitter is attached to these
// hand-built shadow views, so every hover has nowhere to deliver and must not
// crash -- which is also what a cursor moving during a surface teardown looks
// like from here.
TEST(win32_a_synthesised_hover_walks_the_tree_without_an_emitter) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, 10, 40, 50, 120, 60);

  Win32TouchDispatcher dispatcher(&manager, root);
  dispatcher.synthesiseHover(50, 60);
  dispatcher.synthesiseHover(5, 5);
  // Outside the surface entirely, and then the cursor leaving the window.
  dispatcher.synthesiseHover(5000, 5000);
  dispatcher.synthesiseHover(-1, -1);

  manager.destroySurfaceRoot(kSurfaceId);
}
