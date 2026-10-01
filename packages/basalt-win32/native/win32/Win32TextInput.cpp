#include "Win32TextInput.h"

#include "DirectWriteLayout.h"
#include "Win32Strings.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>

#include <commctrl.h>

#include <algorithm>
#include <cwctype>
#include <cmath>

// Visual styles, which an EDIT needs for two things: the cue banner that backs
// `placeholder` is a comctl32 v6 message and silently does nothing without it,
// and a v5 EDIT looks like Windows 95 next to everything else on the screen.
// Declared here rather than in a .manifest file so that running the binary
// straight out of the build directory behaves the same as running an installed
// one -- the same argument SetProcessDpiAwarenessContext makes in main.
#pragma comment(linker,                                    \
                "\"/manifestdependency:type='win32' "      \
                "name='Microsoft.Windows.Common-Controls' " \
                "version='6.0.0.0' "                       \
                "processorArchitecture='*' "               \
                "publicKeyToken='6595b64144ccf1df' "       \
                "language='*'\"")

namespace basalt {

using facebook::react::AttributedString;
using facebook::react::ShadowView;
using facebook::react::Tag;
using facebook::react::TextInputEventEmitter;
using facebook::react::TextInputProps;
using win32::narrow;
using win32::RnTextAlign;
using win32::RnTextStyle;
using win32::RnWin32View;
using win32::widen;

namespace {

// One id for every peer. An EDIT's notifications arrive as WM_COMMAND with a
// control id, and the id is only used to tell "this is one of ours" from "this
// is a menu item"; which control it was comes from the HWND in lparam.
constexpr WORD kControlId = 0x4200;

// SetWindowSubclass takes an id of its own, distinct from the control id.
constexpr UINT_PTR kSubclassId = 1;

// What a password field shows instead of the character. Windows' own default is
// a bullet under visual styles; naming it means the two other desktops and this
// one agree about what secureTextEntry looks like.
constexpr wchar_t kPasswordCharacter = L'\x25CF';

COLORREF toColorRef(const float components[4]) {
  const auto channel = [](float value) {
    return static_cast<int>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
  };
  return RGB(channel(components[0]), channel(components[1]), channel(components[2]));
}

// A view's rectangle in the surface root's coordinates, and whether any of it is
// still visible.
//
// Translation only: an EDIT is a window and a window cannot be rotated, so a
// transformed field's peer sits where the untransformed one would. That is a
// real difference from the painted view behind it, and there is no cheap fix --
// see the header.
//
// Visibility is the other half. A child window is not clipped by anything in
// the React tree, so a field scrolled out of its list would otherwise go on
// being drawn over the list's neighbours. Any clipping ancestor that the
// rectangle has left entirely hides it.
bool peerRect(RnWin32View *view, RnWin32View *root, RECT &out) {
  if (view == nullptr || root == nullptr) {
    return false;
  }

  double x = 0.0;
  double y = 0.0;
  const double width = view->frame().width;
  const double height = view->frame().height;

  for (RnWin32View *current = view; current != nullptr; current = current->parent()) {
    if (current->hidden()) {
      return false;
    }
    if (current == root) {
      break;
    }
    x += current->frame().x;
    y += current->frame().y;

    RnWin32View *parent = current->parent();
    if (parent == nullptr) {
      // Detached: between a Remove and its Delete, or never inserted. Not on
      // screen either way.
      return false;
    }
    x -= parent->scrollX();
    y -= parent->scrollY();

    if (parent->clipsChildren()) {
      // In the parent's own coordinates the box is 0..width, 0..height, and
      // (x, y) is where this view now sits relative to it.
      if (x + width <= 0 || y + height <= 0 || x >= parent->frame().width ||
          y >= parent->frame().height) {
        return false;
      }
    }
  }

  out.left = static_cast<LONG>(std::lround(x));
  out.top = static_cast<LONG>(std::lround(y));
  out.right = out.left + static_cast<LONG>(std::lround(width));
  out.bottom = out.top + static_cast<LONG>(std::lround(height));
  return true;
}

} // namespace

Win32TextInputManager::Win32TextInputManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {}

Win32TextInputManager::~Win32TextInputManager() {
  for (auto &[tag, entry] : entries_) {
    destroyPeer(entry);
  }
}

void Win32TextInputManager::setHostWindow(HWND window) {
  host_ = window;
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

void Win32TextInputManager::update(RnWin32View *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;
  entry.owner = this;

  const auto props = std::dynamic_pointer_cast<const TextInputProps>(shadowView.props);

  if (entry.control == nullptr && host_ != nullptr) {
    // ES_AUTOHSCROLL so the caret can leave the visible box rather than the
    // control refusing further input; WS_CLIPSIBLINGS so two adjacent fields do
    // not paint into each other. No border and no ES_ styles for one: the
    // RnWin32View behind it draws the background, the border and the corner
    // radius, because those are React Native style props and the control knows
    // nothing about them.
    entry.control = CreateWindowEx(0,
                                   L"EDIT",
                                   L"",
                                   WS_CHILD | WS_CLIPSIBLINGS | ES_LEFT | ES_AUTOHSCROLL,
                                   0,
                                   0,
                                   0,
                                   0,
                                   host_,
                                   reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kControlId)),
                                   GetModuleHandle(nullptr),
                                   nullptr);
    if (entry.control != nullptr) {
      SetWindowSubclass(entry.control, editProc, kSubclassId, reinterpret_cast<DWORD_PTR>(&entry));
    }
    // So describeTree can report the field content. The view neither owns the
    // control nor draws it; see RnWin32View::setEditablePeer.
    if (view != nullptr) {
      view->setEditablePeer(entry.control);
    }
  }

  if (props == nullptr) {
    return;
  }
  applyProps(entry, *props);
  // After the font: the line height is measured against it.
  measureShape(entry, shadowView.layoutMetrics);

  if (inserted && props->autoFocus && entry.control != nullptr) {
    SetFocus(entry.control);
  }
}

void Win32TextInputManager::measureShape(Entry &entry,
                                         const facebook::react::LayoutMetrics &metrics) {
  const auto &insets = metrics.contentInsets;
  entry.insets = RECT{static_cast<LONG>(std::lround(insets.left)),
                      static_cast<LONG>(std::lround(insets.top)),
                      static_cast<LONG>(std::lround(insets.right)),
                      static_cast<LONG>(std::lround(insets.bottom))};

  if (entry.control == nullptr) {
    return;
  }
  // From the font the control is actually using rather than from the size that
  // was asked for, because a family substitution changes it.
  if (HDC deviceContext = GetDC(entry.control)) {
    HGDIOBJ previous = SelectObject(deviceContext, entry.font);
    TEXTMETRIC textMetrics{};
    if (GetTextMetrics(deviceContext, &textMetrics) != 0) {
      // Two points of slack, which is where the caret's overhang goes.
      entry.lineHeight = textMetrics.tmHeight + 2;
    }
    SelectObject(deviceContext, previous);
    ReleaseDC(entry.control, deviceContext);
  }
}

void Win32TextInputManager::applyProps(Entry &entry, const TextInputProps &props) {
  // The style, through the same translation a <Text> goes through, so a font
  // family or size means the same thing in a field as it does in a label.
  // fontSizeMultiplier is 1: nothing on this platform scales text for
  // accessibility settings yet, and passing 0 would multiply the size away.
  const RnTextStyle style = win32::buildTextStyle(props.getEffectiveTextAttributes(1.0F));

  entry.textColor = toColorRef(style.color);

  if (props.backgroundColor) {
    const auto components = facebook::react::colorComponentsFromColor(props.backgroundColor);
    const float rgba[4] = {components.red, components.green, components.blue, components.alpha};
    const COLORREF wanted = toColorRef(rgba);
    if (!entry.hasBackground || wanted != entry.backgroundColor ||
        entry.backgroundBrush == nullptr) {
      if (entry.backgroundBrush != nullptr) {
        DeleteObject(entry.backgroundBrush);
      }
      entry.backgroundColor = wanted;
      entry.backgroundBrush = CreateSolidBrush(wanted);
    }
    entry.hasBackground = true;
  } else {
    entry.hasBackground = false;
  }

  if (entry.control == nullptr) {
    return;
  }

  // A negative height is character height rather than cell height, which is
  // what a CSS font-size means and what DirectWrite was given for the same
  // number in a <Text>.
  const int height = -static_cast<int>(std::lround(style.fontSize));
  const std::wstring family = widen(style.fontFamily);
  HFONT font = CreateFont(height,
                          0,
                          0,
                          0,
                          style.bold ? FW_BOLD : FW_NORMAL,
                          style.italic ? TRUE : FALSE,
                          FALSE,
                          FALSE,
                          DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE,
                          family.c_str());
  if (font != nullptr) {
    SendMessage(entry.control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    // After the control has taken the new one: an HFONT in use must not be
    // deleted, and WM_SETFONT does not copy it.
    if (entry.font != nullptr) {
      DeleteObject(entry.font);
    }
    entry.font = font;
  }

  // textAlign, which on an EDIT is a style bit rather than a message.
  LONG_PTR windowStyle = GetWindowLongPtr(entry.control, GWL_STYLE);
  const LONG_PTR alignmentBits = static_cast<LONG_PTR>(ES_LEFT | ES_CENTER | ES_RIGHT);
  LONG_PTR alignment = ES_LEFT;
  if (style.align == RnTextAlign::Center) {
    alignment = ES_CENTER;
  } else if (style.align == RnTextAlign::Right) {
    alignment = ES_RIGHT;
  }
  const LONG_PTR wanted = (windowStyle & ~alignmentBits) | alignment;
  if (wanted != windowStyle) {
    SetWindowLongPtr(entry.control, GWL_STYLE, wanted);
    InvalidateRect(entry.control, nullptr, TRUE);
  }

  // Controlled component: JavaScript owns the value. Two things have to be
  // true at once, and each fails differently.
  //
  // Setting it must not look like typing, or the change reported provokes a
  // re-render that sets it again and the two chase each other -- `applying`.
  //
  // And a prop older than what the user has since typed must not be applied at
  // all, or a fast typist watches characters reorder themselves. That is what
  // `mostRecentEventCount` is for, and dropping the value without recording it
  // is deliberate: the next render, once JavaScript has caught up, applies it.
  const bool stale = props.mostRecentEventCount < entry.eventCount;
  const bool changed = !entry.sawProps || props.text != entry.lastPropText;
  if (changed && !stale) {
    entry.lastPropText = props.text;
    entry.sawProps = true;

    const int length = GetWindowTextLength(entry.control);
    std::wstring current(static_cast<size_t>(length) + 1, L'\0');
    GetWindowText(entry.control, current.data(), length + 1);
    current.resize(static_cast<size_t>(length));

    if (props.text != narrow(current)) {
      entry.applying = true;
      // Preserve the caret: setting the text sends it to the start, which would
      // send it home on every keystroke of a controlled field.
      DWORD selectionStart = 0;
      DWORD selectionEnd = 0;
      SendMessage(entry.control,
                  EM_GETSEL,
                  reinterpret_cast<WPARAM>(&selectionStart),
                  reinterpret_cast<LPARAM>(&selectionEnd));
      const std::wstring wide = widen(props.text);
      SetWindowText(entry.control, wide.c_str());
      const auto caret = static_cast<DWORD>(std::min<size_t>(selectionStart, wide.size()));
      SendMessage(entry.control, EM_SETSEL, caret, caret);
      entry.applying = false;
      entry.lastReportedText = props.text;
    }
  }

  // The cue banner, which is Windows' placeholder and needs visual styles --
  // see the manifest at the top of this file. TRUE keeps it visible while the
  // field has focus and no text, which is what both other desktops do.
  const std::wstring placeholder = widen(props.placeholder);
  SendMessage(entry.control,
              EM_SETCUEBANNER,
              TRUE,
              reinterpret_cast<LPARAM>(placeholder.c_str()));

  // `editable` is the prop; `readOnly` is the newer spelling of its inverse,
  // and React Native honours both.
  const bool writable = props.traits.editable && !props.readOnly;
  SendMessage(entry.control, EM_SETREADONLY, writable ? FALSE : TRUE, 0);

  // secureTextEntry is a message here rather than the different *class* AppKit
  // needs, so it can be turned on and off without rebuilding the control and
  // without losing focus -- the one place this platform has the easier job.
  if (props.traits.secureTextEntry != entry.secure) {
    entry.secure = props.traits.secureTextEntry;
    SendMessage(entry.control, EM_SETPASSWORDCHAR, entry.secure ? kPasswordCharacter : 0, 0);
    InvalidateRect(entry.control, nullptr, TRUE);
  }

  // 0 means no limit, and React Native spells "no limit" as an absent prop --
  // which arrives here as 0 too, so the two agree without a special case.
  SendMessage(entry.control, EM_SETLIMITTEXT, static_cast<WPARAM>(std::max(0, props.maxLength)), 0);
}

void Win32TextInputManager::destroyPeer(Entry &entry) {
  // Deliberately does not clear the view's back pointer, and must not: by the
  // time this runs the view is already gone. MountingWalk's Delete calls
  // `destroyView` *before* `forgetTag`, and `releaseAllViews` runs before this
  // object's own destructor -- so both paths free the view first and touching
  // it here is a use-after-free. It was one, until the end-to-end suite caught
  // the process exiting 0xC0000005.
  //
  // Nothing needs clearing anyway: the view never outlives its peer. The only
  // moment a live view holds a dead HWND is between the host window being
  // destroyed and the views being freed, and reading a dead HWND's text
  // reports nothing rather than crashing -- which is why `captureBeforeTeardown`
  // takes the dump before the window goes.
  entry.view = nullptr;
  if (entry.control != nullptr) {
    RemoveWindowSubclass(entry.control, editProc, kSubclassId);
    DestroyWindow(entry.control);
    entry.control = nullptr;
  }
  if (entry.font != nullptr) {
    DeleteObject(entry.font);
    entry.font = nullptr;
  }
  if (entry.backgroundBrush != nullptr) {
    DeleteObject(entry.backgroundBrush);
    entry.backgroundBrush = nullptr;
  }
}

void Win32TextInputManager::remove(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  // The subclass procedure holds a pointer to the Entry, so the window has to
  // go before the map entry does.
  destroyPeer(it->second);
  entries_.erase(it);
}

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------

void Win32TextInputManager::syncBounds(RnWin32View *root) {
  for (auto &[tag, entry] : entries_) {
    if (entry.control == nullptr) {
      continue;
    }
    RECT rect{};
    if (!peerRect(entry.view, root, rect)) {
      ShowWindow(entry.control, SW_HIDE);
      continue;
    }

    // Inside the content insets, so that `paddingHorizontal` on a field means
    // what it means on a <View>. GTK allocates its GtkText inside the same
    // numbers; here there is no allocation, so the window is placed there.
    rect.left += entry.insets.left;
    rect.top += entry.insets.top;
    rect.right -= entry.insets.right;
    rect.bottom -= entry.insets.bottom;

    // And no taller than one line, centred in what is left. A single-line EDIT
    // draws its text at the top of its client area -- Windows' own fields are
    // sized to their font, so it never shows -- and in a 44-point field with
    // 16-point text that puts the caret near the top and looks broken. Making
    // the control a strip the height of one line and centring it is what every
    // Win32 application does about this.
    //
    // It also keeps the control away from the corners. The RnWin32View behind
    // it paints the rounded background; a full-height rectangular child window
    // would paint square corners straight over them, and a strip at the middle
    // never reaches the part a radius cuts away.
    const LONG available = rect.bottom - rect.top;
    if (entry.lineHeight > 0 && entry.lineHeight < available) {
      rect.top += (available - entry.lineHeight) / 2;
      rect.bottom = rect.top + entry.lineHeight;
    }

    if (rect.right <= rect.left || rect.bottom <= rect.top) {
      ShowWindow(entry.control, SW_HIDE);
      continue;
    }

    SetWindowPos(entry.control,
                 nullptr,
                 rect.left,
                 rect.top,
                 rect.right - rect.left,
                 rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

Win32TextInputManager::Entry *Win32TextInputManager::entryForControl(HWND control) {
  if (control == nullptr) {
    return nullptr;
  }
  for (auto &[tag, entry] : entries_) {
    if (entry.control == control) {
      return &entry;
    }
  }
  return nullptr;
}

bool Win32TextInputManager::ownsControl(HWND control) const {
  for (const auto &[tag, entry] : entries_) {
    if (entry.control == control) {
      return true;
    }
  }
  return false;
}

bool Win32TextInputManager::focusAt(RnWin32View *root, double x, double y) {
  if (root == nullptr || entries_.empty()) {
    return false;
  }
  RnWin32View *hit = win32::hitTest(root, static_cast<float>(x), static_cast<float>(y));
  for (RnWin32View *view = hit; view != nullptr; view = view->parent()) {
    const auto it = entries_.find(static_cast<Tag>(view->tag()));
    if (it != entries_.end() && it->second.control != nullptr) {
      SetFocus(it->second.control);
      return true;
    }
  }
  return false;
}

bool Win32TextInputManager::handleControlCommand(WPARAM wparam, LPARAM lparam) {
  Entry *entry = entryForControl(reinterpret_cast<HWND>(lparam));
  if (entry == nullptr) {
    return false;
  }

  switch (HIWORD(wparam)) {
    case EN_CHANGE:
      reportChange(*entry);
      return true;

    case EN_SETFOCUS:
      if (const auto emitter = emitterFor(entry->tag)) {
        emitter->onFocus(metricsFor(*entry));
      }
      return true;

    case EN_KILLFOCUS:
      if (const auto emitter = emitterFor(entry->tag)) {
        emitter->onBlur(metricsFor(*entry));
        emitter->onEndEditing(metricsFor(*entry));
      }
      return true;

    default:
      break;
  }
  // A notification from one of this manager's controls that it does not act on
  // -- EN_UPDATE, EN_MAXTEXT. Still handled, in the sense that the host must
  // not treat it as a menu command.
  return true;
}

HBRUSH Win32TextInputManager::controlColor(HDC deviceContext, HWND control) {
  Entry *entry = entryForControl(control);
  if (entry == nullptr) {
    return nullptr;
  }
  SetTextColor(deviceContext, entry->textColor);
  if (entry->hasBackground && entry->backgroundBrush != nullptr) {
    SetBkColor(deviceContext, entry->backgroundColor);
    return entry->backgroundBrush;
  }
  // No background prop. The control cannot see through itself to what Direct2D
  // painted behind it, so the window's own ground is the closest honest answer;
  // a field with no backgroundColor over a coloured parent is the case this
  // gets visibly wrong, and the fix is a background prop.
  SetBkColor(deviceContext, GetSysColor(COLOR_WINDOW));
  return GetSysColorBrush(COLOR_WINDOW);
}

void Win32TextInputManager::reportChange(Entry &entry) {
  if (entry.applying) {
    // A prop being applied, not the user typing.
    return;
  }
  const auto metrics = metricsFor(entry);
  if (metrics.text == entry.lastReportedText) {
    return;
  }
  entry.lastReportedText = metrics.text;
  entry.eventCount++;

  if (const auto emitter = emitterFor(entry.tag)) {
    // Rebuilt so it carries the incremented count: React Native drops a prop
    // update whose eventCount is older than the last one it was told about, so
    // an event that under-reports its own count makes the field ignore the
    // value coming back down.
    emitter->onChange(metricsFor(entry));
  }
}

std::shared_ptr<const TextInputEventEmitter> Win32TextInputManager::emitterFor(Tag tag) const {
  return std::dynamic_pointer_cast<const TextInputEventEmitter>(lookup_(tag));
}

TextInputEventEmitter::Metrics Win32TextInputManager::metricsFor(const Entry &entry) const {
  TextInputEventEmitter::Metrics metrics{};
  metrics.eventCount = entry.eventCount;
  metrics.target = entry.tag;
  metrics.zoomScale = 1.0F;

  if (entry.control != nullptr) {
    const int length = GetWindowTextLength(entry.control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowText(entry.control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    metrics.text = narrow(text);

    DWORD start = 0;
    DWORD end = 0;
    SendMessage(entry.control,
                EM_GETSEL,
                reinterpret_cast<WPARAM>(&start),
                reinterpret_cast<LPARAM>(&end));
    metrics.selectionRange =
        AttributedString::Range{static_cast<int>(start), static_cast<int>(end - start)};
  }

  // The scroll-shaped fields exist because iOS's text view is a scroll view.
  // Nothing here scrolls yet, so they describe a viewport the size of the
  // field, which is true and keeps JavaScript's arithmetic sane.
  const auto width = static_cast<facebook::react::Float>(
      entry.view != nullptr ? entry.view->frame().width : 0.0F);
  const auto height = static_cast<facebook::react::Float>(
      entry.view != nullptr ? entry.view->frame().height : 0.0F);
  metrics.containerSize = {.width = width, .height = height};
  metrics.contentSize = metrics.containerSize;
  metrics.layoutMeasurement = metrics.containerSize;

  return metrics;
}

// ---------------------------------------------------------------------------
// The subclass
// ---------------------------------------------------------------------------

void Win32TextInputManager::reportSelectionIfChanged(Entry &entry) {
  if (entry.control == nullptr) {
    return;
  }
  // Not while a prop is being pushed in. Applying `text` moves the caret, and
  // reporting that as the person selecting something would make a controlled
  // field fight its own render -- the same guard both other hosts have.
  if (entry.applying) {
    return;
  }

  // The caret first, on its own. `metricsFor` copies the field's whole text,
  // and this runs after every mouse move -- so the cheap question is asked
  // first and the expensive one only when the answer has changed.
  DWORD start = 0;
  DWORD end = 0;
  SendMessage(entry.control,
              EM_GETSEL,
              reinterpret_cast<WPARAM>(&start),
              reinterpret_cast<LPARAM>(&end));
  const auto location = static_cast<int>(start);
  const auto length = static_cast<int>(end - start);
  if (location == entry.lastReportedSelection.location &&
      length == entry.lastReportedSelection.length) {
    return;
  }
  entry.lastReportedSelection = AttributedString::Range{location, length};

  if (const auto emitter = emitterFor(entry.tag)) {
    emitter->onSelectionChange(metricsFor(entry));
  }
}

LRESULT CALLBACK Win32TextInputManager::editProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
  auto *entry = reinterpret_cast<Entry *>(data);

  if (message == WM_CHAR && entry != nullptr && entry->owner != nullptr) {
    // React Native's contract, the same one both other hosts follow: 'Enter'
    // and 'Backspace' by name, the typed character otherwise -- including ' '
    // for space -- and nothing at all for a key that produces no character.
    // WM_CHAR is already the character, so Escape and Tab fall out as
    // unprintable rather than needing to be named.
    const auto typed = static_cast<wchar_t>(wparam);
    std::string key;
    if (typed == L'\r' || typed == L'\n') {
      key = "Enter";
    } else if (typed == L'\b') {
      key = "Backspace";
    } else if (std::iswprint(static_cast<wint_t>(typed))) {
      key = narrow(std::wstring(1, typed));
    }
    if (!key.empty()) {
      if (const auto emitter = entry->owner->emitterFor(entry->tag)) {
        TextInputEventEmitter::KeyPressMetrics metrics{};
        metrics.text = key;
        metrics.eventCount = entry->eventCount;
        emitter->onKeyPress(metrics);
      }
    }

    // A single-line EDIT has nowhere to put a newline, so it beeps at one. That
    // beep is the sound of an unhandled Enter, and Enter is what React Native
    // calls submitEditing -- so it is caught here and swallowed. Escape and Tab
    // beep for the same reason: neither has a meaning inside a field, and
    // nothing on this platform implements a tab order for the second to reach.
    const auto character = typed;
    if (character == L'\r' || character == L'\n') {
      if (const auto emitter = entry->owner->emitterFor(entry->tag)) {
        emitter->onSubmitEditing(entry->owner->metricsFor(*entry));
      }
      return 0;
    }
    if (character == L'\x1b' || character == L'\t') {
      return 0;
    }
  }

  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, editProc, id);
  }

  const LRESULT result = DefSubclassProc(hwnd, message, wparam, lparam);

  // Windows does not report the caret moving: EN_SELCHANGE is RichEdit's, and a
  // plain EDIT says nothing. So the selection is read after anything that could
  // have moved it -- which is why this runs *after* DefSubclassProc, once the
  // control has done the moving.
  if (entry != nullptr && entry->owner != nullptr) {
    switch (message) {
      // Its own case rather than part of the list below: `wparam` here is a
      // button mask, and everywhere else in this switch it is something else
      // entirely -- a character for WM_CHAR, a virtual key for WM_KEYDOWN. A
      // fallthrough would test a character against MK_LBUTTON, which passes for
      // odd code points and fails for even ones.
      case WM_MOUSEMOVE:
        // A hover cannot move the caret, and this runs on every mouse move over
        // the field.
        if ((wparam & MK_LBUTTON) != 0) {
          entry->owner->reportSelectionIfChanged(*entry);
        }
        break;
      case WM_CHAR:
      case WM_KEYDOWN:
      case WM_KEYUP:
      case WM_LBUTTONDOWN:
      case WM_LBUTTONUP:
      case WM_SETFOCUS:
      case WM_CUT:
      case WM_PASTE:
      case WM_CLEAR:
      case WM_UNDO:
      case EM_SETSEL:
        entry->owner->reportSelectionIfChanged(*entry);
        break;
      default:
        break;
    }
  }

  return result;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool Win32TextInputManager::dispatchCommand(Tag tag,
                                            const std::string &name,
                                            const folly::dynamic &args) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;

  if (name == "focus") {
    if (entry.control != nullptr) {
      SetFocus(entry.control);
    }
    return true;
  }

  if (name == "blur") {
    // Focus moves to the host window rather than being dropped: Win32 has no
    // "nothing has focus" within an active window, and SetFocus(nullptr) takes
    // keyboard input away from the process entirely, which is not what blur
    // means. The control still sees EN_KILLFOCUS, so JavaScript sees onBlur.
    if (entry.control != nullptr && GetFocus() == entry.control) {
      SetFocus(host_);
    }
    return true;
  }

  if (name == "setTextAndSelection") {
    // [eventCount, text, start, end]. An eventCount older than what the user
    // has since typed means this command is stale and must be dropped, which is
    // the whole reason React Native counts them.
    if (args.isArray() && args.size() >= 2 && entry.control != nullptr) {
      const int eventCount = static_cast<int>(args[0].asInt());
      if (eventCount < entry.eventCount) {
        return true;
      }
      entry.applying = true;
      const auto text = args[1].isString() ? args[1].asString() : std::string{};
      const std::wstring wide = widen(text);
      SetWindowText(entry.control, wide.c_str());
      if (args.size() >= 4 && args[2].isInt() && args[3].isInt()) {
        SendMessage(entry.control,
                    EM_SETSEL,
                    static_cast<WPARAM>(args[2].asInt()),
                    static_cast<LPARAM>(args[3].asInt()));
      }
      entry.applying = false;
      entry.lastReportedText = text;
    }
    return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// Scripted typing
// ---------------------------------------------------------------------------

bool Win32TextInputManager::typeIntoFocused(const std::string &text) {
  HWND focused = GetFocus();
  if (!ownsControl(focused)) {
    return false;
  }
  // WM_CHAR per UTF-16 code unit, which is what a keyboard driver would send
  // and what an IME sends for a composed character. So this skips the driver
  // and nothing above it -- the EDIT's own handling, EN_CHANGE, the emitter,
  // the event beat and React all run exactly as they would.
  for (const wchar_t unit : widen(text)) {
    SendMessage(focused, WM_CHAR, static_cast<WPARAM>(unit), 1);
  }
  return true;
}

} // namespace basalt
