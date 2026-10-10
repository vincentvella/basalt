// <TextInput> on Win32.
//
// The native half is React Native's own *iOS* TextInput: its shadow node,
// props, state and event emitter are pure C++ and measure through a
// TextLayoutManager, which on this platform is the DirectWrite one. Android's
// variant includes fbjni and calls into a Java FabricUIManager, so it is
// unusable here; see docs/DECISIONS.md. The component name is therefore
// "TextInput", which is what packages/basalt-core's
// src/overrides/TextInput.js asks for -- the same file both other desktops use,
// because that override was never about Linux.
//
// The editing itself is a real EDIT control, not a caret drawn on a DirectWrite
// layout. That brings the IME, selection, the clipboard, undo, double-click
// word selection, Ctrl+arrow, and every other binding a Windows user expects --
// none of it worth reimplementing and all of it easy to get subtly wrong. GTK
// embeds a GtkText and AppKit an NSTextField for exactly the same reason.
//
// ## The peer is a window, and that is the whole difficulty
//
// On the other two desktops a React Native view *is* a widget, so a text field
// is a widget inside a widget and the toolkit handles the rest. Here every view
// is a plain C++ object painted with Direct2D into one HWND, and an EDIT is a
// real child window. Three consequences follow, and none of them has an
// equivalent on GTK or AppKit:
//
//   - **It has to be positioned by hand.** The manager walks the view up to the
//     surface root to find its rectangle in client coordinates, and has to redo
//     that whenever anything moves it -- which includes a scroll, where no
//     mutation arrives at all. `syncBounds` is that, and the host calls it after
//     every transaction and every wheel.
//
//   - **It is always on top.** Child windows paint above whatever Direct2D drew,
//     in an order the React tree has no say in. A field cannot be covered by a
//     later sibling, and it does not clip to a scrolled ancestor -- it is hidden
//     when it leaves the ancestor's box instead, which is close enough to look
//     right and is not the same thing.
//
//   - **Its events arrive at the parent.** EN_CHANGE, EN_SETFOCUS and
//     EN_KILLFOCUS are WM_COMMAND notifications to the host window, and the
//     text and background colours are answered from WM_CTLCOLOREDIT. So the
//     host forwards both, which is why this class has methods a mounting
//     manager would not otherwise need.
//
// ## The controlled-value loop, which is not about a toolkit
//
// React Native's <TextInput> is controlled: JavaScript owns the value, the
// control reports every change, JavaScript re-renders and pushes the value back
// down as a prop. If applying that prop looks like the user typing, the two
// chase each other forever. If a prop that arrives late is applied anyway, a
// fast typist watches characters reorder themselves.
//
// `applying` breaks the first and `eventCount` the second, exactly as on both
// other desktops, because neither is a Windows problem.

#pragma once

#include "RnWin32View.h"

#include <react/renderer/components/iostextinput/TextInputShadowNode.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowView.h>

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace basalt {

class Win32TextInputManager {
 public:
  using EmitterLookup = std::function<facebook::react::EventEmitter::Shared(facebook::react::Tag)>;

  explicit Win32TextInputManager(EmitterLookup lookup);
  ~Win32TextInputManager();

  Win32TextInputManager(const Win32TextInputManager &) = delete;
  Win32TextInputManager &operator=(const Win32TextInputManager &) = delete;
  Win32TextInputManager(Win32TextInputManager &&) = delete;
  Win32TextInputManager &operator=(Win32TextInputManager &&) = delete;

  // The window every peer is a child of. Nothing can be created before this is
  // set, which is why `update` tolerates being called without it: the mounting
  // manager exists before the host's window does, and the tests never have one.
  void setHostWindow(HWND window);

  // Called for every mutation touching a TextInput. Creates the control the
  // first time, then applies props.
  void update(win32::RnWin32View *view, const facebook::react::ShadowView &shadowView);

  void remove(facebook::react::Tag tag);

  // Focuses whatever `autoFocus` asked for, once the transaction is on screen.
  //
  // Where the prop is read is too early on the other two hosts, and may be here:
  // MountingWalk::create applies props straight after `createView`, before any
  // Insert. An EDIT is a child of the real top-level window from the moment it is
  // created, so Windows may get away with it, but the control is made without
  // WS_VISIBLE and is only shown by syncBounds afterwards. Deferred here too, so
  // all three hosts answer `autoFocus` the same way and the one that differs is
  // not the one nobody tested.
  void flushAutoFocus();

  // focus, blur, and setTextAndSelection. Returns false for anything else.
  bool dispatchCommand(facebook::react::Tag tag,
                       const std::string &name,
                       const folly::dynamic &args);

  // Moves every peer to where its view now is, in client coordinates, and hides
  // the ones that have gone out of sight. Called after each transaction and
  // each scroll: a child window does not follow a view that has no widget.
  void syncBounds(win32::RnWin32View *root);

  // WM_COMMAND from the host: EN_CHANGE, EN_SETFOCUS, EN_KILLFOCUS. True when
  // the notification was one of this manager's controls.
  bool handleControlCommand(WPARAM wparam, LPARAM lparam);

  // WM_CTLCOLOREDIT / WM_CTLCOLORSTATIC from the host. Returns the brush to
  // paint the control's background with, or null when the control is not one of
  // this manager's. An EDIT otherwise draws in the system's colours, which have
  // nothing to do with the `style` the component was given -- on a dark field
  // that is dark text on dark, the same trap GtkText has.
  HBRUSH controlColor(HDC deviceContext, HWND control);

  // The bitmap currently installed as the caret for `tag`, or null.
  //
  // Only the test suite asks, and it asks because Windows offers no way to read
  // the caret back: `CreateCaret` hands a bitmap to the system and there is no
  // `GetCaret`. So the only thing that can be measured is the bitmap this
  // installed, which is why this is here rather than private -- see
  // `win32_a_cursor_colour_installs_a_caret_of_that_colour`.
  HBITMAP caretBitmapFor(facebook::react::Tag tag) const;

  // True when the control is one of this manager's peers, which is how the host
  // tells a key meant for a field from one meant for the window.
  bool ownsControl(HWND control) const;

  // Focuses the field under a point in the surface root's coordinates, if there
  // is one. True when there was.
  //
  // Only BASALT_TEST_TAP needs this, and the reason is the one thing a
  // synthesised tap cannot reproduce. A real click at these coordinates never
  // reaches the host's window procedure at all: the peer is a child window, so
  // USER32 routes the click to it and the control focuses itself. A tap
  // injected at the touch dispatcher skips all of that, because the dispatcher
  // is above the point where the two paths diverge.
  bool focusAt(win32::RnWin32View *root, double x, double y);

  // Types into whichever field has focus, for BASALT_TEST_TYPE. Real WM_CHAR
  // messages to the real control, so what it skips is the keyboard driver and
  // nothing above it.
  bool typeIntoFocused(const std::string &text);

 private:
  // Set when a control is created with `autoFocus`, cleared by flushAutoFocus.
  std::vector<facebook::react::Tag> pendingAutoFocus_;

  struct Entry {
    win32::RnWin32View *view{nullptr};
    HWND control{nullptr};
    HFONT font{nullptr};
    facebook::react::Tag tag{0};
    Win32TextInputManager *owner{nullptr};
    // The subclass has to be able to find its way back, and SetWindowSubclass's
    // reference data is the only channel; this is what is put there.

    // React Native counts events so it can ignore a prop update older than what
    // the user has since typed. Every emitted metric carries it.
    int eventCount{0};

    // True while a prop is being pushed into the control, so the EN_CHANGE it
    // provokes is not reported back as the user typing.
    bool applying{false};
    // The selection last reported to JavaScript. A plain EDIT has no
    // notification for the caret moving -- EN_SELCHANGE belongs to RichEdit --
    // so the selection is read after anything that could have moved it and
    // compared with this. See reportSelectionIfChanged.
    facebook::react::AttributedString::Range lastReportedSelection{};

    std::string lastReportedText;

    // The last `text` prop actually seen, and whether one has been seen at all.
    //
    // This is what tells a *controlled* field from an uncontrolled one, which
    // props alone cannot: React Native's TextInput.js sends
    // `text={value ?? defaultValue}`, and an uncontrolled field with no default
    // sends undefined, which arrives here as the empty string -- exactly what a
    // controlled field that JavaScript has cleared sends. Applying it on every
    // render therefore wipes an uncontrolled field the moment anything else in
    // the tree re-renders, because RN re-sends `mostRecentEventCount` on every
    // change and that alone produces an Update mutation.
    //
    // So the prop is applied when it *changes*, not when it differs from the
    // control. A controlled field's value changes as the user types; an
    // uncontrolled one's never does.
    std::string lastPropText;
    bool sawProps{false};

    // The last `selection` prop seen, for the same reason and under the same
    // rule: a controlled selection is applied when it changes, not whenever it
    // differs from the control. Absent means the field has never sent one,
    // which is what an uncontrolled selection looks like.
    std::optional<facebook::react::Selection> lastPropSelection{};

    COLORREF textColor{RGB(0, 0, 0)};
    COLORREF backgroundColor{RGB(255, 255, 255)};
    HBRUSH backgroundBrush{nullptr};
    // Whether the view behind the control has a background of its own. With
    // none, the control is painted over whatever Direct2D drew there -- which
    // it cannot see, so the closest honest answer is the window's own ground.
    bool hasBackground{false};

    bool secure{false};

    // The three colours React Native has for a field, none of which a plain
    // EDIT has a property for. See `applyProps`, `drawPlaceholder` and
    // `installCaret`.
    //
    // Each is held with a flag rather than a sentinel colour, because
    // `SharedColor`'s unset value is zero: a field that asked for nothing would
    // otherwise get a black caret and a transparent placeholder, which is the
    // bug both other hosts had to be careful of for the same reason.
    bool hasPlaceholderColour{false};
    COLORREF placeholderColour{RGB(0, 0, 0)};
    // The placeholder itself, held because with a colour this host draws it
    // rather than handing it to `EM_SETCUEBANNER`, which has no colour.
    std::wstring placeholder;
    bool hasCaretColour{false};
    COLORREF caretColour{RGB(0, 0, 0)};
    // The caret's shape, which is how its colour is set: a bitmap, XOR-ed onto
    // the field by the system. Created when the field takes focus, because the
    // colour it has to carry depends on the background it will be XOR-ed
    // against, and destroyed when it loses it -- the documented contract for a
    // caret is that the window owning it has the focus.
    HBITMAP caret{nullptr};

    // `clearTextOnFocus` and `selectTextOnFocus`: what happens when the field
    // takes focus, which arrives long after the props do. See EN_SETFOCUS.
    bool clearTextOnFocus{false};
    bool selectTextOnFocus{false};

    // Which kind of EDIT this is. `ES_MULTILINE` is read when the window is
    // created and cannot be added afterwards, so this is what a change in the
    // prop is compared against: a field that switches has its peer rebuilt.
    bool multiline{false};

    // Yoga resolves border and padding into contentInsets, so the same twelve
    // points mean the same thing in a field as in a <View>. Applied by placing
    // the control inside them rather than by any EDIT message: EM_SETRECT is
    // documented as multiline-only and silently does nothing here, which is a
    // afternoon nobody should spend twice.
    RECT insets{0, 0, 0, 0};

    // What the control's font actually occupies, measured rather than assumed:
    // a family substitution changes it, and this is what the control's height
    // is set to so that the text sits in the middle of the field.
    LONG lineHeight{0};

    // The input scope last installed for `keyboardType`, and whether one has
    // been. `SetInputScope` replaces the window's whole scope list, so it is
    // called on a change rather than on every props update -- a controlled
    // field re-sends identical props on every keystroke.
    int inputScope{0};
    bool sawInputScope{false};
  };

  Entry *entryForControl(HWND control);
  void applyProps(Entry &entry, const facebook::react::TextInputProps &props);
  // `spellCheck`, `autoCorrect`, `autoCapitalize` and `keyboardType`: the four
  // the three hosts honour different subsets of. Separate from `applyProps`
  // because what this host can act on is one and a half of them and the comment
  // explaining which is longer than the code.
  void applyTextChecking(Entry &entry, const facebook::react::TextInputProps &props);

  // Remembers the content box and the line height, which is what `syncBounds`
  // needs to place the control and cannot work out from the view alone.
  void measureShape(Entry &entry, const facebook::react::LayoutMetrics &metrics);

  void destroyPeer(Entry &entry);

  // The placeholder, drawn by this host rather than by the control.
  //
  // `EM_SETCUEBANNER` is Windows' placeholder and takes no colour, so a field
  // that asked for one is drawn here instead: the control's own font, the
  // asked-for colour, inside the formatting rectangle the control reports. Used
  // from both WM_PAINT and WM_PRINTCLIENT, which is what lets a test render a
  // field into a memory DC and read the pixels back.
  void drawPlaceholder(const Entry &entry, HDC deviceContext) const;

  // The caret, whose colour is a bitmap XOR-ed onto the field.
  //
  // Called after the control has handled WM_SETFOCUS, because an EDIT creates
  // its own caret there and `CreateCaret` replaces whatever shape came before.
  void installCaret(Entry &entry);
  void releaseCaret(Entry &entry);

  // Enter, and the keys an EDIT would otherwise beep at. Installed with
  // SetWindowSubclass rather than SetWindowLongPtr so that a later subclass by
  // anything else still chains correctly.
  static LRESULT CALLBACK
  editProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);

  void reportChange(Entry &entry);
  facebook::react::TextInputEventEmitter::Metrics metricsFor(const Entry &entry) const;
  // Reads the caret and emits onSelectionChange if it moved. Called after any
  // message that could have moved it, because Windows will not say.
  void reportSelectionIfChanged(Entry &entry);
  std::shared_ptr<const facebook::react::TextInputEventEmitter> emitterFor(
      facebook::react::Tag tag) const;

  EmitterLookup lookup_;
  HWND host_{nullptr};
  // Stable addresses: the subclass procedure holds an Entry pointer, so the map
  // must not move its values. std::unordered_map does not, which is the reason
  // it is this container and not a vector.
  std::unordered_map<facebook::react::Tag, Entry> entries_;
};

} // namespace basalt
