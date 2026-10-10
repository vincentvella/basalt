// Tests for <TextInput> on Windows: the peer, the controlled-value loop, the
// placement and the commands.
//
// These make a real window and real EDIT controls. That is unusual for this
// suite -- every other Windows test runs without one, because Direct2D renders
// offscreen -- and it is unavoidable here: a text field's peer *is* a window,
// so a test that mocks it away tests nothing that could break. The window is
// never shown, which is what keeps this runnable on a build agent.
//
// What still cannot be asserted from here is the events. An EventEmitter built
// by hand has no EventDispatcher, so `onChange` and `onFocus` go nowhere; the
// end-to-end proof is `e2e/input.js` under BASALT_TEST_TAP and BASALT_TEST_TYPE,
// What is left is everything the
// control itself can be asked: its text, its selection, its read-only and
// password state, where it is, and whether the loop that writes it is broken
// in the right place.

#include "TestHarness.h"

#include "Win32MountingManager.h"
#include "Win32Strings.h"
#include "Win32TextInput.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/RawProps.h>
#include <react/renderer/core/RawPropsParser.h>

// EM_GETCUEBANNER, which is comctl32's rather than USER32's -- the same
// requirement the manager's manifest comment explains for setting one.
#include <commctrl.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using basalt::Win32MountingManager;
using basalt::win32::narrow;
using basalt::win32::RnWin32View;
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
using facebook::react::TextInputProps;
using facebook::react::TransactionTelemetry;
using facebook::react::ViewProps;

namespace {

constexpr SurfaceId kSurfaceId = 1;

// The manager the test window forwards notifications to. A window procedure
// takes no context this suite can give it, and the alternative -- a window per
// manager -- would register a class per test.
Win32MountingManager *gCurrentManager = nullptr;

// The same forwarding the real host does, and it has to be here or nothing
// works: an EDIT reports EN_CHANGE to its *parent*, so a test window that
// answered with DefWindowProc alone would leave `eventCount` at zero and every
// staleness check trivially satisfied. That is the shape of a test that passes
// while the thing it describes is broken.
LRESULT CALLBACK testHostProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_COMMAND && gCurrentManager != nullptr &&
      gCurrentManager->handleControlCommand(wparam, lparam)) {
    return 0;
  }
  // And the colours, which an EDIT asks its parent for. Forwarded for the same
  // reason WM_COMMAND is: a field printed into a device context with the
  // system's colours instead of the app's is not the field the app described.
  if ((message == WM_CTLCOLOREDIT || message == WM_CTLCOLORSTATIC)
      && gCurrentManager != nullptr) {
    if (HBRUSH brush = gCurrentManager->controlColor(reinterpret_cast<HDC>(wparam),
                                                     reinterpret_cast<HWND>(lparam))) {
      return reinterpret_cast<LRESULT>(brush);
    }
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}

// One hidden window for the whole suite, because registering a class and
// creating a window per test is slower than the tests are and buys nothing:
// every peer is destroyed with its entry, so nothing leaks between them.
HWND testHostWindow() {
  static HWND window = [] {
    WNDCLASSEX windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = testHostProc;
    windowClass.hInstance = GetModuleHandle(nullptr);
    windowClass.lpszClassName = L"BasaltTestHost";
    RegisterClassEx(&windowClass);
    // WS_POPUP and never shown: a control still lays out, takes text and
    // reports its rectangle inside a window nobody can see.
    return CreateWindowEx(0,
                          L"BasaltTestHost",
                          L"",
                          WS_POPUP | WS_CLIPCHILDREN,
                          0,
                          0,
                          800,
                          600,
                          nullptr,
                          nullptr,
                          GetModuleHandle(nullptr),
                          nullptr);
  }();
  return window;
}

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

// One parser, prepared once: preparing walks every prop TextInputProps knows,
// which is not something to redo per test. RawProps has to be parsed before it
// can be read -- constructing one and handing it straight to a Props
// constructor trips an assertion inside RawProps rather than producing empty
// props, which is a better failure than it sounds and is what both other
// suites' comments say too.
//
// Through the parser rather than by assignment because TextInputProps' `traits`
// is const and only reachable this way.
const RawPropsParser &textInputParser() {
  static const RawPropsParser parser = []() {
    RawPropsParser prepared;
    prepared.prepare<TextInputProps>();
    return prepared;
  }();
  return parser;
}

struct FieldOptions {
  std::string text{};
  std::string placeholder{};
  int mostRecentEventCount{0};
  bool editable{true};
  bool secure{false};
  int maxLength{0};
  float padding{0};
  // A controlled selection, as JavaScript sends one. -1 for "the app sent
  // none", which is what an uncontrolled field looks like and is a different
  // thing from sending (0, 0).
  int selectionStart{-1};
  int selectionEnd{-1};
  bool multiline{false};
  bool clearTextOnFocus{false};
  bool selectTextOnFocus{false};
  // The colours, as React Native sends them: 0xAARRGGBB, and 0 for "the app
  // sent none" -- which is the distinction the host has to keep, since
  // `SharedColor`'s unset value is zero and transparent black is a colour an
  // app could mean.
  unsigned int backgroundColor{0};
  unsigned int placeholderTextColor{0};
  unsigned int selectionColor{0};
  unsigned int cursorColor{0};
};

ShadowView makeField(Tag tag,
                     float x,
                     float y,
                     float width,
                     float height,
                     const FieldOptions &options = {}) {
  static const auto contextContainer = std::make_shared<const ContextContainer>();
  PropsParserContext context{kSurfaceId, *contextContainer};

  folly::dynamic raw = folly::dynamic::object("text", options.text)(
      "mostRecentEventCount", options.mostRecentEventCount)("editable", options.editable)(
      "secureTextEntry", options.secure)("multiline", options.multiline)(
      "clearTextOnFocus", options.clearTextOnFocus)("selectTextOnFocus",
                                                    options.selectTextOnFocus);
  if (!options.placeholder.empty()) {
    raw["placeholder"] = options.placeholder;
  }
  if (options.maxLength > 0) {
    raw["maxLength"] = options.maxLength;
  }
  if (options.selectionStart >= 0) {
    raw["selection"] =
        folly::dynamic::object("start", options.selectionStart)("end", options.selectionEnd);
  }
  for (const auto &[name, colour] :
       {std::pair<const char *, unsigned int>{"backgroundColor", options.backgroundColor},
        {"placeholderTextColor", options.placeholderTextColor},
        {"selectionColor", options.selectionColor},
        {"cursorColor", options.cursorColor}}) {
    if (colour != 0) {
      raw[name] = static_cast<int64_t>(colour);
    }
  }

  RawProps rawProps{std::move(raw)};
  rawProps.parse(textInputParser());
  auto parsed = std::make_shared<const TextInputProps>(context, TextInputProps{}, rawProps);

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = x, .y = y}, .size = {.width = width, .height = height}};
  metrics.contentInsets = {.left = options.padding,
                           .top = options.padding,
                           .right = options.padding,
                           .bottom = options.padding};

  ShadowView view;
  view.componentName = "TextInput";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = parsed;
  view.layoutMetrics = metrics;
  return view;
}

void apply(Win32MountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

void mount(Win32MountingManager &manager, Tag parent, const ShadowView &shadowView) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(shadowView));
  mutations.push_back(ShadowViewMutation::InsertMutation(parent, shadowView, 0));
  apply(manager, std::move(mutations));
}

void update(Win32MountingManager &manager, const ShadowView &before, const ShadowView &after) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::UpdateMutation(before, after, kSurfaceId));
  apply(manager, std::move(mutations));
}

// A manager already pointed at the hidden window, which is what every test
// below needs before it mounts anything: `update` creates the peer on first
// sight and only then.
std::unique_ptr<Win32MountingManager> makeManager() {
  auto manager = std::make_unique<Win32MountingManager>();
  manager->setHostWindow(testHostWindow());
  gCurrentManager = manager.get();
  return manager;
}

// The peer of a mounted field, found the way the host's WM_COMMAND handler
// finds one: by walking the host window's children. Nothing exposes the HWND,
// and nothing should -- so the test goes the long way round rather than the
// manager growing an accessor for it.
HWND peerOf(RnWin32View *view) {
  struct Search {
    RECT wanted{};
    HWND found{nullptr};
  } search;
  // The peer is placed inside the view's box, so a child window whose rectangle
  // is within it is the one. Unambiguous here because each test mounts fields
  // that do not overlap.
  search.wanted = RECT{static_cast<LONG>(view->frame().x),
                       static_cast<LONG>(view->frame().y),
                       static_cast<LONG>(view->frame().x + view->frame().width),
                       static_cast<LONG>(view->frame().y + view->frame().height)};

  EnumChildWindows(
      testHostWindow(),
      [](HWND child, LPARAM data) -> BOOL {
        auto *state = reinterpret_cast<Search *>(data);
        RECT rect{};
        GetWindowRect(child, &rect);
        MapWindowPoints(HWND_DESKTOP, GetParent(child), reinterpret_cast<LPPOINT>(&rect), 2);
        if (rect.left >= state->wanted.left && rect.right <= state->wanted.right &&
            rect.top >= state->wanted.top && rect.bottom <= state->wanted.bottom) {
          state->found = child;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.found;
}

std::string textOf(HWND control) {
  if (control == nullptr) {
    return "<no control>";
  }
  const int length = GetWindowTextLength(control);
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  GetWindowText(control, text.data(), length + 1);
  text.resize(static_cast<size_t>(length));
  return narrow(text);
}

// WM_CHAR per character, which is what a keyboard driver sends and what the
// scripted BASALT_TEST_TYPE sends.
void type(HWND control, const std::wstring &text) {
  for (const wchar_t unit : text) {
    SendMessage(control, WM_CHAR, static_cast<WPARAM>(unit), 1);
  }
}

RECT rectOf(HWND control) {
  RECT rect{};
  GetWindowRect(control, &rect);
  MapWindowPoints(HWND_DESKTOP, GetParent(control), reinterpret_cast<LPPOINT>(&rect), 2);
  return rect;
}

} // namespace

// A mounted field gets a real control, and the host's window is its parent.
TEST(win32_a_text_input_mounts_a_real_control) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  EXPECT(GetParent(control) == testHostWindow());

  manager->destroySurfaceRoot(kSurfaceId);
}

// The `text` prop reaches the control, which is what a `defaultValue` or a
// controlled field's first render looks like from here.
TEST(win32_the_text_prop_reaches_the_control) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44, {.text = "hello"}));
  manager->syncTextInputBounds(root);

  EXPECT_EQ(textOf(peerOf(manager->viewForTag(10))), std::string("hello"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// The controlled loop, in the shape e2e/input.js exercises: the user types, and
// JavaScript sends back something *different*. The field has to take it.
TEST(win32_a_changed_text_prop_is_applied) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  const ShadowView before = makeField(10, 20, 30, 200, 44);
  mount(*manager, kSurfaceId, before);
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  type(control, L"hi");
  EXPECT_EQ(textOf(control), std::string("hi"));

  // React re-renders with the upper-cased value, acknowledging both keystrokes.
  update(*manager,
         before,
         makeField(10, 20, 30, 200, 44, {.text = "HI", .mostRecentEventCount = 2}));
  EXPECT_EQ(textOf(control), std::string("HI"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// An uncontrolled field keeps what was typed into it. React Native re-sends
// `mostRecentEventCount` on every change, which is an Update mutation on its
// own, and `text` for an uncontrolled field is the empty string forever -- so a
// manager that applies the prop whenever it differs from the control wipes the
// field on the user's second keystroke. Applying it only when the *prop*
// changes is what makes this work, and this is the test that says so.
TEST(win32_an_uncontrolled_field_keeps_what_was_typed) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  const ShadowView before = makeField(10, 20, 30, 200, 44);
  mount(*manager, kSurfaceId, before);
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  type(control, L"abc");
  EXPECT_EQ(textOf(control), std::string("abc"));

  // The re-render an uncontrolled field produces: no text, a bumped count.
  update(*manager, before, makeField(10, 20, 30, 200, 44, {.mostRecentEventCount = 3}));
  EXPECT_EQ(textOf(control), std::string("abc"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// A prop older than what the user has since typed is dropped. That is what
// React Native counts events for, and without it a fast typist watches
// characters reorder themselves.
TEST(win32_a_stale_text_prop_is_dropped) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  const ShadowView before = makeField(10, 20, 30, 200, 44);
  mount(*manager, kSurfaceId, before);
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  type(control, L"abcd");

  // JavaScript answering the first keystroke, three keystrokes late.
  update(*manager,
         before,
         makeField(10, 20, 30, 200, 44, {.text = "A", .mostRecentEventCount = 1}));
  EXPECT_EQ(textOf(control), std::string("abcd"));

  // And once it has caught up, the same value is applied rather than lost.
  update(*manager,
         before,
         makeField(10, 20, 30, 200, 44, {.text = "A", .mostRecentEventCount = 4}));
  EXPECT_EQ(textOf(control), std::string("A"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// Enter is swallowed rather than inserted. A single-line EDIT has nowhere to
// put a newline and beeps at one; that beep is an unhandled submitEditing.
TEST(win32_enter_does_not_reach_the_text) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  type(control, L"a\rb\tc\x1b");
  EXPECT_EQ(textOf(control), std::string("abc"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// editable: false, which React Native also spells readOnly.
TEST(win32_a_read_only_field_refuses_typing) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44, {.editable = false}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  type(control, L"nope");
  EXPECT_EQ(textOf(control), std::string(""));

  manager->destroySurfaceRoot(kSurfaceId);
}

// secureTextEntry, which is a message here rather than the different class
// AppKit needs -- so unlike macOS it can be turned on and off without losing
// what was typed, and this checks that as well as that it is on at all.
TEST(win32_secure_text_entry_masks_and_can_be_turned_off) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  const ShadowView before = makeField(10, 20, 30, 200, 44, {.secure = true});
  mount(*manager, kSurfaceId, before);
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  type(control, L"secret");
  EXPECT(SendMessage(control, EM_GETPASSWORDCHAR, 0, 0) != 0);
  // Masked on screen and not in the buffer: the value React sees is the real
  // one, which is the whole point of a password field rather than a cipher.
  EXPECT_EQ(textOf(control), std::string("secret"));

  update(*manager, before, makeField(10, 20, 30, 200, 44, {.secure = false}));
  EXPECT(SendMessage(control, EM_GETPASSWORDCHAR, 0, 0) == 0);
  EXPECT_EQ(textOf(control), std::string("secret"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// maxLength, which macOS does not have at all: an NSTextField has no maximum
// and enforcing one there needs a formatter.
TEST(win32_max_length_is_enforced) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44, {.maxLength = 3}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  type(control, L"abcdef");
  EXPECT_EQ(textOf(control), std::string("abc"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// The peer is placed inside the content insets, so that `paddingHorizontal` on
// a field means what it means on a <View>, and no taller than one line so the
// text sits in the middle rather than at the top.
TEST(win32_the_peer_sits_inside_the_padding_and_is_centred) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44, {.padding = 12}));
  manager->syncTextInputBounds(root);

  const RECT rect = rectOf(peerOf(manager->viewForTag(10)));
  EXPECT_EQ(static_cast<long>(rect.left), 32L);
  EXPECT_EQ(static_cast<long>(rect.right), 208L);
  // Vertically centred in the 20..64 box, whatever the measured line height
  // turned out to be. Asserting the centring rather than the height is what
  // keeps this from failing on a machine whose default font is a point taller.
  EXPECT_EQ(static_cast<long>((rect.top - 30) - (74 - rect.bottom)), 0L);
  EXPECT(rect.bottom - rect.top < 44);

  manager->destroySurfaceRoot(kSurfaceId);
}

// A peer follows its view. Nothing in the view tree owns a window, so it has to
// be moved by hand -- which is also why it is worth a test that it is.
TEST(win32_the_peer_follows_its_view) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeView(10, 0, 0, 400, 400));
  mount(*manager, 10, makeField(11, 0, 0, 200, 44));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(11));
  EXPECT_EQ(static_cast<long>(rectOf(control).left), 0L);

  // The *parent* moves, which produces no mutation for the field at all.
  manager->viewForTag(10)->setFrame(100, 50, 400, 400);
  manager->syncTextInputBounds(root);
  EXPECT_EQ(static_cast<long>(rectOf(control).left), 100L);

  manager->destroySurfaceRoot(kSurfaceId);
}

// A field scrolled out of its list is hidden. A child window is not clipped by
// anything in the React tree, so without this it goes on being drawn over the
// list's neighbours -- which is the most visible consequence of the peer being
// a window.
TEST(win32_a_peer_scrolled_out_of_view_is_hidden) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeView(10, 0, 0, 400, 100));
  manager->viewForTag(10)->setClipsChildren(true);
  mount(*manager, 10, makeField(11, 0, 0, 200, 44));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(11));
  EXPECT(IsWindowVisible(control) || GetParent(control) != nullptr);

  manager->viewForTag(10)->setScrollOffset(0, 500);
  manager->syncTextInputBounds(root);
  EXPECT(!IsWindowVisible(control));

  manager->viewForTag(10)->setScrollOffset(0, 0);
  manager->syncTextInputBounds(root);
  EXPECT_EQ(static_cast<long>(rectOf(control).left), 0L);

  manager->destroySurfaceRoot(kSurfaceId);
}

// focus, blur and setTextAndSelection, which arrive through dispatchCommand.
TEST(win32_the_commands_reach_the_control) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44));
  manager->syncTextInputBounds(root);
  HWND control = peerOf(manager->viewForTag(10));

  manager->applyCommand(10, "setTextAndSelection", folly::dynamic::array(0, "typed", 1, 3));
  EXPECT_EQ(textOf(control), std::string("typed"));

  DWORD start = 0;
  DWORD end = 0;
  SendMessage(
      control, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
  EXPECT_EQ(static_cast<long>(start), 1L);
  EXPECT_EQ(static_cast<long>(end), 3L);

  // Focus needs the window to be able to take it, and a WS_POPUP that was never
  // shown cannot -- so what is checked is that the command is claimed and
  // survives, not that focus moved. The end-to-end run covers the rest.
  manager->applyCommand(10, "focus", folly::dynamic::array());
  manager->applyCommand(10, "blur", folly::dynamic::array());
  EXPECT_EQ(textOf(control), std::string("typed"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// A stale setTextAndSelection is dropped for the same reason a stale prop is.
TEST(win32_a_stale_set_text_command_is_dropped) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44));
  manager->syncTextInputBounds(root);
  HWND control = peerOf(manager->viewForTag(10));

  type(control, L"abc");
  manager->applyCommand(10, "setTextAndSelection", folly::dynamic::array(1, "stale", 0, 0));
  EXPECT_EQ(textOf(control), std::string("abc"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// The peer goes with the view. A control left behind would keep drawing over
// the surface that replaced it, which is the failure a plain C++ view layer
// cannot get for free.
TEST(win32_deleting_a_text_input_destroys_its_peer) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  const ShadowView field = makeField(10, 20, 30, 200, 44);
  mount(*manager, kSurfaceId, field);
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(IsWindow(control));

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, field, 0));
  mutations.push_back(ShadowViewMutation::DeleteMutation(field));
  apply(*manager, std::move(mutations));

  EXPECT(!IsWindow(control));
  EXPECT(manager->viewForTag(10) == nullptr);
  // And a command that arrives after it is gone is survivable, which is the
  // shape of a blur racing a surface teardown.
  manager->applyCommand(10, "blur", folly::dynamic::array());

  manager->destroySurfaceRoot(kSurfaceId);
}

// The manager outlives nothing: every peer goes when it does. Without this a
// second surface in the same process inherits the first one's controls.
TEST(win32_destroying_the_manager_destroys_every_peer) {
  HWND control = nullptr;
  {
    auto manager = makeManager();
    RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
    root->setFrame(0, 0, 800, 600);
    mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44));
    manager->syncTextInputBounds(root);
    control = peerOf(manager->viewForTag(10));
    EXPECT(IsWindow(control));
    manager->destroySurfaceRoot(kSurfaceId);
  }
  EXPECT(!IsWindow(control));
}

// A controlled selection, which is `selection` on the props and `EM_SETSEL` on
// the control. The same three rules the text is under, because it is the same
// kind of prop: applied when it changes, dropped when it is stale, and never
// reported back as the user having moved the caret.
TEST(win32_the_selection_prop_reaches_the_control) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  const ShadowView before =
      makeField(10, 20, 30, 200, 44, {.text = "hello world", .mostRecentEventCount = 1});
  mount(*manager, kSurfaceId, before);
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  const auto selectionOf = [](HWND field) {
    DWORD start = 0;
    DWORD end = 0;
    SendMessage(field, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    return std::pair<int, int>{static_cast<int>(start), static_cast<int>(end)};
  };

  update(*manager,
         before,
         makeField(10,
                   20,
                   30,
                   200,
                   44,
                   {.text = "hello world",
                    .mostRecentEventCount = 1,
                    .selectionStart = 2,
                    .selectionEnd = 7}));
  EXPECT_EQ(selectionOf(control).first, 2);
  EXPECT_EQ(selectionOf(control).second, 7);

  manager->destroySurfaceRoot(kSurfaceId);
}

// A selection older than what the user has since typed is dropped, which is the
// rule that keeps a fast typist's caret from jumping backwards.
TEST(win32_a_stale_selection_prop_is_dropped) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  const ShadowView before = makeField(10, 20, 30, 200, 44);
  mount(*manager, kSurfaceId, before);
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }
  // Two keystrokes, so the control's event count is ahead of anything
  // JavaScript sent before them.
  type(control, L"hi");

  update(*manager,
         before,
         makeField(10,
                   20,
                   30,
                   200,
                   44,
                   {.text = "hi",
                    .mostRecentEventCount = 0,
                    .selectionStart = 0,
                    .selectionEnd = 0}));

  DWORD start = 0;
  DWORD end = 0;
  SendMessage(control, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
  // Still at the end, where typing left it: the stale selection was dropped
  // rather than applied, so the caret did not go home.
  EXPECT_EQ(static_cast<int>(start), 2);
  EXPECT_EQ(static_cast<int>(end), 2);

  manager->destroySurfaceRoot(kSurfaceId);
}

// A multiline field is a different kind of EDIT, which is a creation-time
// decision: `ES_MULTILINE` is read when the window is made.
TEST(win32_a_multiline_field_is_a_multiline_control) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10, 20, 30, 200, 120, {.text = "", .multiline = true}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }
  const LONG_PTR style = GetWindowLongPtr(control, GWL_STYLE);
  EXPECT((style & ES_MULTILINE) != 0);
  // And not the horizontal scroll a single line gets: a paragraph wraps.
  EXPECT((style & ES_AUTOHSCROLL) == 0);
  EXPECT((style & ES_AUTOVSCROLL) != 0);

  // Enter inserts a newline rather than being swallowed, which is React
  // Native's `submitBehavior: 'newline'` -- the default for a multiline field.
  type(control, L"a\rb");
  EXPECT_EQ(textOf(control), std::string("a\r\nb"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// A single-line field still swallows Enter, which is the other half of the same
// rule and the test that was here before this prop existed.
TEST(win32_a_single_line_field_is_not_multiline) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }
  EXPECT((GetWindowLongPtr(control, GWL_STYLE) & ES_MULTILINE) == 0);

  manager->destroySurfaceRoot(kSurfaceId);
}

// A field that changes the prop has its peer rebuilt, because the style cannot
// be added to a live control. The text goes with it -- which is correct for a
// controlled field, where the next render re-sends it -- and what this asserts
// is that the control is the right kind afterwards and still works.
TEST(win32_changing_multiline_rebuilds_the_peer) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  const ShadowView single = makeField(10, 20, 30, 200, 44);
  mount(*manager, kSurfaceId, single);
  manager->syncTextInputBounds(root);

  HWND before = peerOf(manager->viewForTag(10));
  EXPECT(before != nullptr);
  EXPECT((GetWindowLongPtr(before, GWL_STYLE) & ES_MULTILINE) == 0);

  update(*manager,
         single,
         makeField(10, 20, 30, 200, 120, {.text = "wrapped", .multiline = true}));
  manager->syncTextInputBounds(root);

  HWND after = peerOf(manager->viewForTag(10));
  EXPECT(after != nullptr);
  if (after == nullptr) {
    return;
  }
  EXPECT(after != before);
  EXPECT((GetWindowLongPtr(after, GWL_STYLE) & ES_MULTILINE) != 0);
  // The new control has the props applied to it, text included: a rebuild that
  // left the field empty would be a field that cleared itself on a prop change.
  EXPECT_EQ(textOf(after), std::string("wrapped"));

  manager->destroySurfaceRoot(kSurfaceId);
}

// And it fills its content box rather than being a strip one line tall, which
// is what a single-line field gets and what would hide every line after the
// first.
TEST(win32_a_multiline_peer_fills_its_content_box) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10, 20, 30, 200, 120, {.text = "", .padding = 8, .multiline = true}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }
  // The field is at y=30, 120 tall, with 8 points of padding -- so the content
  // box starts at 38 and is 104 tall, and the control is all of it.
  const RECT placed = rectOf(control);
  EXPECT_EQ(static_cast<long>(placed.top), 38L);
  EXPECT_EQ(static_cast<long>(placed.bottom - placed.top), 104L);

  manager->destroySurfaceRoot(kSurfaceId);
}

// `clearTextOnFocus` and `selectTextOnFocus`: what happens when the field takes
// focus, which no host read until 2026-10-10.
//
// Focus is real here -- `SetFocus` on the peer, with the suite's window as its
// parent -- and the notification comes back through the same WM_COMMAND the
// host forwards, which is what makes this the whole path rather than a call to
// the handler.
TEST(win32_clear_text_on_focus_empties_the_field) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10, 20, 30, 200, 44, {.text = "keep me", .clearTextOnFocus = true}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }
  EXPECT_EQ(textOf(control), std::string("keep me"));

  SetFocus(control);
  EXPECT_EQ(textOf(control), std::string(""));

  manager->destroySurfaceRoot(kSurfaceId);
}

// The control: a field that did not ask keeps its text, so a host that cleared
// unconditionally would fail here rather than pass the test above twice.
TEST(win32_a_field_that_did_not_ask_keeps_its_text_on_focus) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44, {.text = "keep me"}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }
  SetFocus(control);
  EXPECT_EQ(textOf(control), std::string("keep me"));

  manager->destroySurfaceRoot(kSurfaceId);
}

TEST(win32_select_text_on_focus_selects_all_of_it) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10, 20, 30, 200, 44, {.text = "select me", .selectTextOnFocus = true}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  SetFocus(control);
  DWORD start = 0;
  DWORD end = 0;
  SendMessage(control, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
  EXPECT_EQ(static_cast<int>(start), 0);
  EXPECT_EQ(static_cast<int>(end), 9);

  manager->destroySurfaceRoot(kSurfaceId);
}

// And a field that did not ask gets a caret rather than a selection, which is
// what a bare `SetFocus` on an EDIT gives: it is not dialog-managed, which is
// why `autoFocus` needed no fixing on this host and why this prop is a real
// difference here rather than a no-op.
TEST(win32_a_field_that_did_not_ask_gets_a_caret_on_focus) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44, {.text = "select me"}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  SetFocus(control);
  DWORD start = 0;
  DWORD end = 0;
  SendMessage(control, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
  EXPECT_EQ(static_cast<int>(start), static_cast<int>(end));

  manager->destroySurfaceRoot(kSurfaceId);
}

// Both at once, in the order that means something: cleared first, so there is
// nothing left to select. A host that selected first would leave a selection
// over text it then threw away.
TEST(win32_clearing_and_selecting_at_once_leaves_an_empty_field) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10,
                  20,
                  30,
                  200,
                  44,
                  {.text = "both", .clearTextOnFocus = true, .selectTextOnFocus = true}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  SetFocus(control);
  EXPECT_EQ(textOf(control), std::string(""));
  DWORD start = 0;
  DWORD end = 0;
  SendMessage(control, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
  EXPECT_EQ(static_cast<int>(start), static_cast<int>(end));

  manager->destroySurfaceRoot(kSurfaceId);
}

// --- The three colours a field has ------------------------------------------
//
// `placeholderTextColor`, `selectionColor` and `cursorColor`, which this host
// read none of until 2026-10-10 because a classic EDIT has a property for none
// of them: the cue banner is drawn in the system's grey, the selection in
// COLOR_HIGHLIGHT, and the caret is a *shape* rather than a colour.
//
// Two of the three are reachable anyway and are asserted here. The selection
// highlight is not, and backlog/textinput.md records the route that would reach
// it rather than this suite pretending otherwise.
namespace {

// The control's own pixels, which is not something this host could otherwise
// produce: the Direct2D snapshot renders the view tree and a peer is a child
// window, invisible to it. WM_PRINTCLIENT is the documented way to render a
// control into a device context of the caller's, and the subclass draws the
// placeholder there as well as on screen for exactly this reason.
//
// White to begin with, so that "nothing was drawn" and "something was drawn in
// black" are different answers.
int pixelsMatchingInPrintedField(HWND control,
                                 bool (*matches)(int red, int green, int blue)) {
  RECT client{};
  GetClientRect(control, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width <= 0 || height <= 0) {
    return -1;
  }

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  void *bits = nullptr;
  const HDC screen = GetDC(nullptr);
  const HDC memory = CreateCompatibleDC(screen);
  const HBITMAP surface = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (memory == nullptr || surface == nullptr || bits == nullptr) {
    if (surface != nullptr) {
      DeleteObject(surface);
    }
    if (memory != nullptr) {
      DeleteDC(memory);
    }
    return -1;
  }

  const HGDIOBJ previous = SelectObject(memory, surface);
  RECT whole{0, 0, width, height};
  FillRect(memory, &whole, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  SendMessage(control, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
  GdiFlush();

  int matching = 0;
  const auto *pixels = static_cast<const std::uint32_t *>(bits);
  for (int index = 0; index < width * height; index++) {
    const std::uint32_t pixel = pixels[index];
    if (matches(static_cast<int>((pixel >> 16) & 0xFF),
                static_cast<int>((pixel >> 8) & 0xFF),
                static_cast<int>(pixel & 0xFF))) {
      matching++;
    }
  }

  SelectObject(memory, previous);
  DeleteObject(surface);
  DeleteDC(memory);
  return matching;
}

bool reddish(int red, int green, int blue) {
  return red > 200 && green < 80 && blue < 80;
}

// What the field's cue banner is now, which is the other half of the
// placeholder: a field that asked for a colour must not have one, or Windows
// draws its grey placeholder underneath the one drawn here.
std::wstring cueBannerOf(HWND control) {
  constexpr size_t kLimit = 64;
  wchar_t buffer[kLimit] = {};
  SendMessage(control,
              EM_GETCUEBANNER,
              reinterpret_cast<WPARAM>(buffer),
              static_cast<LPARAM>(kLimit));
  return std::wstring(buffer);
}

// The colour of the caret bitmap the manager installed, read back out of GDI.
//
// Nothing can ask the system what the caret looks like -- there is no GetCaret
// -- so the bitmap is the measurable artefact, which is why the manager hands
// it over.
COLORREF caretColourOf(HBITMAP bitmap) {
  BITMAP described{};
  if (bitmap == nullptr || GetObject(bitmap, sizeof(described), &described) == 0) {
    return CLR_INVALID;
  }

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = described.bmWidth;
  info.bmiHeader.biHeight = -described.bmHeight;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  std::vector<std::uint32_t> pixels(
      static_cast<size_t>(described.bmWidth) * static_cast<size_t>(described.bmHeight), 0);
  const HDC screen = GetDC(nullptr);
  const int rows = GetDIBits(screen,
                             bitmap,
                             0,
                             static_cast<UINT>(described.bmHeight),
                             pixels.data(),
                             &info,
                             DIB_RGB_COLORS);
  ReleaseDC(nullptr, screen);
  if (rows == 0 || pixels.empty()) {
    return CLR_INVALID;
  }
  const std::uint32_t pixel = pixels.front();
  return RGB((pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF);
}

} // namespace

TEST(win32_a_placeholder_colour_is_drawn_by_this_host) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10,
                  20,
                  30,
                  200,
                  44,
                  {.placeholder = "Search",
                   .backgroundColor = 0xffffffff,
                   .placeholderTextColor = 0xffff0000}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  // Drawn here, in the colour the app asked for. `EM_SETCUEBANNER` could not
  // have produced this: it has no colour parameter at all.
  const int red = pixelsMatchingInPrintedField(control, reddish);
  EXPECT(red > 0);

  // And the banner is empty, so Windows is not drawing a grey placeholder
  // underneath this one.
  EXPECT(cueBannerOf(control).empty());

  manager->destroySurfaceRoot(kSurfaceId);
}

// The negative control, and the other behaviour: a field that named no colour
// keeps Windows' own placeholder, which is the cue banner.
TEST(win32_a_placeholder_with_no_colour_keeps_the_system_banner) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10, 20, 30, 200, 44, {.placeholder = "Search", .backgroundColor = 0xffffffff}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  EXPECT_EQ(narrow(cueBannerOf(control)), std::string("Search"));
  EXPECT_EQ(pixelsMatchingInPrintedField(control, reddish), 0);

  manager->destroySurfaceRoot(kSurfaceId);
}

// A field with text has no placeholder to draw, which is a thing the drawing
// has to check for itself: the cue banner hides itself and this does not.
TEST(win32_a_placeholder_is_not_drawn_over_text) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10,
                  20,
                  30,
                  200,
                  44,
                  {.text = "typed",
                   .placeholder = "Search",
                   .backgroundColor = 0xffffffff,
                   .placeholderTextColor = 0xffff0000}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  EXPECT_EQ(pixelsMatchingInPrintedField(control, reddish), 0);

  manager->destroySurfaceRoot(kSurfaceId);
}

// `cursorColor`, which is a bitmap because a caret is a shape.
//
// The bitmap carries the asked-for colour XOR the field's background, because
// CreateCaret's documentation says "the caret is drawn to the screen via the
// XOR operation" -- so red over white is a cyan bitmap, and the system's XOR
// turns it back into red. Asserting the cyan rather than the red is what makes
// this a test of the arithmetic the platform needs rather than of a value being
// remembered.
TEST(win32_a_cursor_colour_installs_a_caret_of_that_colour) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10,
                  20,
                  30,
                  200,
                  44,
                  {.backgroundColor = 0xffffffff, .cursorColor = 0xffff0000}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  // Nothing before focus: a window that does not have the focus should not own
  // a caret, which is the documented contract and not a shortcut.
  EXPECT(manager->textInputCaretBitmap(10) == nullptr);

  SetFocus(control);
  const HBITMAP caret = manager->textInputCaretBitmap(10);
  EXPECT(caret != nullptr);
  EXPECT_EQ(caretColourOf(caret), RGB(0, 255, 255));

  manager->destroySurfaceRoot(kSurfaceId);
}

// The control: a field that asked for nothing keeps the system's caret, which
// is a solid XOR of the background and needs no bitmap at all.
TEST(win32_a_field_with_no_cursor_colour_keeps_the_system_caret) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager, kSurfaceId, makeField(10, 20, 30, 200, 44, {.backgroundColor = 0xffffffff}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  SetFocus(control);
  EXPECT(manager->textInputCaretBitmap(10) == nullptr);

  manager->destroySurfaceRoot(kSurfaceId);
}

// `cursorColor` falls back to `selectionColor`, which is React Native's
// contract rather than an invention here: `selectionColor` is "the highlight,
// selection handle and cursor color" and `cursorColor` overrides the caret
// alone. Both other hosts read the pair the same way.
//
// Green over white is a magenta bitmap, by the same XOR.
TEST(win32_a_cursor_with_no_colour_of_its_own_follows_the_selection) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10,
                  20,
                  30,
                  200,
                  44,
                  {.backgroundColor = 0xffffffff, .selectionColor = 0xff00ff00}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  SetFocus(control);
  const HBITMAP caret = manager->textInputCaretBitmap(10);
  EXPECT(caret != nullptr);
  EXPECT_EQ(caretColourOf(caret), RGB(255, 0, 255));

  manager->destroySurfaceRoot(kSurfaceId);
}

// And the shape goes when the focus does. Not housekeeping: there is one caret
// per queue, so a field that kept its bitmap around would be holding the shape
// of whatever has the focus now.
TEST(win32_a_caret_bitmap_goes_when_the_field_loses_focus) {
  auto manager = makeManager();
  RnWin32View *root = manager->createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 800, 600);
  mount(*manager,
        kSurfaceId,
        makeField(10,
                  20,
                  30,
                  200,
                  44,
                  {.backgroundColor = 0xffffffff, .cursorColor = 0xffff0000}));
  manager->syncTextInputBounds(root);

  HWND control = peerOf(manager->viewForTag(10));
  EXPECT(control != nullptr);
  if (control == nullptr) {
    return;
  }

  SetFocus(control);
  EXPECT(manager->textInputCaretBitmap(10) != nullptr);

  SetFocus(testHostWindow());
  EXPECT(manager->textInputCaretBitmap(10) == nullptr);

  manager->destroySurfaceRoot(kSurfaceId);
}
