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
#include "Win32Cursors.h"
#include "Win32MountingManager.h"
#include "Win32TouchDispatcher.h"

#include <react/renderer/components/view/ViewProps.h>

#include <cstdint>
#include <cstring>
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

// ---------------------------------------------------------------------------
// The whole of `cursor`, as a keyword list rather than as a hit test.
// See win32/Win32Cursors.h.
//
// The cursor tests in tests/test_win32_hittest.cpp ask which view is under the
// pointer and whether the keywords Windows has a stock cursor for land on the
// right one. This one asks the question those cannot: whether the list React
// Native accepts is covered at all. It sits beside the touch dispatcher because
// the list is a React Native fact -- `core/CursorNames.h` prints it from
// `facebook::react::Cursor` -- and only the shards that have React Native can
// be held to it.
//
// What breaks without this test is already in the tree and passing:
// `cursor_a_keyword_windows_has_nothing_for_leaves_the_pointer_alone` asserts
// the gap rather than closing it, so the suite stays green while six keywords
// do nothing. An app that puts `cursor: 'zoom-in'` on an image gets the arrow
// it already had, with no error and no warning, and a drag source that asked
// for `copy` or `alias` to say what a drop will do gets no feedback at all --
// which for those two keywords is the entire feature, the pointer being the
// only thing a drag can speak through.
//
// So this claims the stronger thing the support page's `partial` is measured
// against: every keyword React Native accepts resolves to some cursor, no
// keyword falls back to the default arrow unless it *is* `default`, and two
// keywords share a cursor only inside the families below, where the host
// documents the sharing. It fails today on the six the backlog entry names and
// on nothing else: `alias`, `cell`, `context-menu`, `copy`, `zoom-in` and
// `zoom-out`, each of which answers no cursor whatever.
//
// `none` is deliberately not in the list. It is an absence rather than an
// image, `SetCursor(nullptr)`, and the `hidden` flag is what says so; the
// hit-test suite covers it. `auto` and `url` reach a host as no keyword at all.
// ---------------------------------------------------------------------------

// A cursor as pixels, for comparing two of them.
//
// DrawIconEx at a size we choose rather than the cursor's own, so that a
// 32-pixel drawn cursor and whatever size the system hands back are the same
// shape of answer. False when the cursor cannot be drawn at all, which the
// caller treats as "nothing to compare" rather than as a failure.
bool cursorPixels(HCURSOR cursor, std::vector<std::uint8_t> &out) {
  constexpr int kSide = 32;
  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = kSide;
  info.bmiHeader.biHeight = -kSide;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void *bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  bool ok = false;
  if (bitmap != nullptr && bits != nullptr) {
    HGDIOBJ previous = SelectObject(dc, bitmap);
    // Cleared first: a DIB section is not promised zeroed, and comparing two
    // cursors over two different piles of leftover memory would never match.
    std::memset(bits, 0, static_cast<std::size_t>(kSide) * kSide * 4);
    ok = DrawIconEx(dc, 0, 0, cursor, kSide, kSide, 0, nullptr, DI_NORMAL) != FALSE;
    if (ok) {
      out.assign(static_cast<std::uint8_t *>(bits),
                 static_cast<std::uint8_t *>(bits) + static_cast<std::size_t>(kSide) * kSide * 4);
    }
    SelectObject(dc, previous);
  }
  if (bitmap != nullptr) {
    DeleteObject(bitmap);
  }
  DeleteDC(dc);
  ReleaseDC(nullptr, screen);
  return ok;
}

TEST(win32_every_cursor_keyword_resolves_to_a_cursor_of_its_own) {
  // CSS's list, grouped into the families that may legitimately share one
  // cursor. A family is the exception the claim allows; everything else has to
  // differ. The first family is the arrow, and nothing outside it may be.
  const std::vector<std::vector<std::string>> families = {
      {"default"},
      {"pointer"},
      // No vertical I-beam in the stock set, so `vertical-text` wears the
      // horizontal one. A narrower complaint than the six below, since it is a
      // cursor that means roughly the right thing rather than no cursor.
      {"text", "vertical-text"},
      {"crosshair"},
      {"wait"},
      // Busy but still usable, which is the arrow with the hourglass beside it.
      {"progress"},
      {"help"},
      // CSS's own pair: both mean "this drop will not happen".
      {"not-allowed", "no-drop"},
      // Windows draws no difference between taking hold of something and
      // moving it, and the shell uses one cursor for all four.
      {"move", "all-scroll", "grab", "grabbing"},
      // The eight directional resizes collapse onto the four `IDC_SIZE*`
      // arrows the way a window's own edges do.
      {"ew-resize", "col-resize", "e-resize", "w-resize"},
      {"ns-resize", "row-resize", "n-resize", "s-resize"},
      {"nesw-resize", "ne-resize", "sw-resize"},
      {"nwse-resize", "nw-resize", "se-resize"},
      // And the six with nothing. Each is its own family because each means
      // its own thing: a shortcut, a spreadsheet cell, a menu about to open, a
      // copy rather than a move, and magnifying in rather than out.
      {"alias"},
      {"cell"},
      {"context-menu"},
      {"copy"},
      {"zoom-in"},
      {"zoom-out"}};

  struct Resolved {
    std::string keyword;
    int family;
    basalt::win32::Win32Cursor answer;
  };

  std::vector<Resolved> resolved;
  int family = 0;
  for (const std::vector<std::string> &keywords : families) {
    for (const std::string &keyword : keywords) {
      resolved.push_back(Resolved{keyword, family, basalt::win32::win32CursorFor(keyword)});
    }
    family++;
  }

  // Every keyword answers a cursor. Collected into one string rather than
  // asserted in the loop so that a failure names all of them at once, the way
  // GestureScope::joined does for states.
  std::string withoutOne;
  for (const Resolved &entry : resolved) {
    // `hidden` as well as null: a keyword answered by hiding the pointer has
    // no cursor of its own either, and hiding the pointer over a cell or a
    // shortcut would be worse than the arrow it replaces.
    if (entry.answer.cursor != nullptr && !entry.answer.hidden) {
      continue;
    }
    if (!withoutOne.empty()) {
      withoutOne += ",";
    }
    withoutOne += entry.keyword;
  }
  EXPECT_EQ(withoutOne, std::string(""));

  // No two keywords from different families wear the same cursor. Null answers
  // are skipped: every null equals every other null, and the check above has
  // already named them, so this reports only genuine collisions.
  std::string collided;
  for (const Resolved &left : resolved) {
    for (const Resolved &right : resolved) {
      // Each cross-family pair once, which is also how same-family pairs are
      // left alone.
      if (left.family >= right.family) {
        continue;
      }
      if (left.answer.cursor == nullptr || right.answer.cursor == nullptr) {
        continue;
      }
      if (left.answer.cursor != right.answer.cursor) {
        continue;
      }
      if (!collided.empty()) {
        collided += ",";
      }
      collided += left.keyword + "=" + right.keyword;
    }
  }
  EXPECT_EQ(collided, std::string(""));

  // And again by what they look like, which is not the same question.
  //
  // The check above compares handles, and two handles are different the
  // moment they come from different calls -- `LoadCursorFromFileW` on a .cur
  // and `LoadCursorW(IDC_HAND)` are two handles to the same drawing. That is
  // exactly how `alias` and `pointer` came to be the same cursor without this
  // test noticing: `alias` was loaded from %SystemRoot%\Cursors\aero_link.cur,
  // which on Windows 11 is "Link Select" -- the hand IDC_HAND already gives
  // `pointer`. Distinct handles, one picture, and the user cannot tell the two
  // keywords apart.
  //
  // So this draws each one and compares the pixels. Thirty-two by thirty-two
  // because that is the size DrawIconEx is being asked for rather than the
  // size the cursor happens to be, which makes a system cursor and a drawn
  // one comparable at all.
  std::string looksTheSame;
  for (const Resolved &left : resolved) {
    for (const Resolved &right : resolved) {
      if (left.family >= right.family) {
        continue;
      }
      if (left.answer.cursor == nullptr || right.answer.cursor == nullptr) {
        continue;
      }
      std::vector<std::uint8_t> leftBits;
      std::vector<std::uint8_t> rightBits;
      if (!cursorPixels(left.answer.cursor, leftBits) ||
          !cursorPixels(right.answer.cursor, rightBits)) {
        continue;
      }
      if (leftBits != rightBits) {
        continue;
      }
      if (!looksTheSame.empty()) {
        looksTheSame += ",";
      }
      looksTheSame += left.keyword + "=" + right.keyword;
    }
  }
  EXPECT_EQ(looksTheSame, std::string(""));

  // And the failure this is really about, named directly rather than left to
  // the loop above: the arrow belongs to `default`. A keyword mapped onto
  // `IDC_ARROW` to stop it answering nothing is the same silence with a handle
  // in it, because the arrow is what the window class already shows.
  const HCURSOR arrow = LoadCursorW(nullptr, IDC_ARROW);
  EXPECT(basalt::win32::win32CursorFor("default").cursor == arrow);

  std::string wearingTheArrow;
  for (const Resolved &entry : resolved) {
    if (entry.keyword == "default" || entry.answer.cursor != arrow) {
      continue;
    }
    if (!wearingTheArrow.empty()) {
      wearingTheArrow += ",";
    }
    wearingTheArrow += entry.keyword;
  }
  EXPECT_EQ(wearingTheArrow, std::string(""));
}
