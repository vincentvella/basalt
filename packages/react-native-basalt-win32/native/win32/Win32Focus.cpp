#include "Win32Focus.h"

#include "Win32KeyEvents.h"

#include <algorithm>
#include <optional>
#include <vector>

#include <folly/dynamic.h>

#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/core/EventEmitter.h>

#include <windows.h>

namespace basalt {

using facebook::react::RawEvent;
using facebook::react::Tag;
using win32::RnWin32View;

namespace {

// Everything Tab should stop on, in tree order.
//
// Tree order rather than paint order: zIndex changes what is on top and must
// not change what Tab reaches next, which is what both other hosts' chains do.
void collectInto(RnWin32View *view, std::vector<RnWin32View *> &stops) {
  if (view == nullptr || view->hidden()) {
    return;
  }
  if (view->editablePeer() != nullptr || view->focusable()) {
    stops.push_back(view);
  }
  for (RnWin32View *child : view->children()) {
    collectInto(child, stops);
  }
}

} // namespace

Win32FocusManager::Win32FocusManager(Win32MountingManager *mountingManager,
                                     RnWin32View *surfaceRoot)
    : mountingManager_(mountingManager), surfaceRoot_(surfaceRoot) {}

void Win32FocusManager::setSurfaceRoot(RnWin32View *surfaceRoot) {
  surfaceRoot_ = surfaceRoot;
  // Every tag in the old tree is about to stop existing, so the focus goes with
  // it -- without an event, because the emitter is going too.
  focusedTag_ = 0;
}

std::vector<RnWin32View *> Win32FocusManager::collectStops() const {
  std::vector<RnWin32View *> stops;
  collectInto(surfaceRoot_, stops);
  return stops;
}

void Win32FocusManager::setFocusedView(RnWin32View *view) {
  const Tag tag = view == nullptr ? 0 : static_cast<Tag>(view->tag());
  if (tag == focusedTag_) {
    return;
  }
  const Tag previous = focusedTag_;
  if (previous != 0) {
    if (RnWin32View *old = mountingManager_->viewForTag(previous)) {
      old->setShowsFocusRing(false);
    }
  }
  focusedTag_ = tag;
  if (previous != 0) {
    emitFocus(previous, false);
  }
  if (view != nullptr) {
    view->setShowsFocusRing(true);
    emitFocus(tag, true);
  }
}

void Win32FocusManager::emitFocus(Tag tag, bool focused) {
  const auto emitter = std::dynamic_pointer_cast<const facebook::react::ViewEventEmitter>(
      mountingManager_->eventEmitterForTag(tag));
  if (emitter == nullptr) {
    return;
  }
  // `topFocus` and `topBlur`, which React Native registers as bubbling events
  // with an empty payload. BaseViewEventEmitter has both, so nothing here has
  // to know the names.
  if (focused) {
    emitter->onFocus();
  } else {
    emitter->onBlur();
  }
}

void Win32FocusManager::textInputTookFocus() {
  // A field's peer is a real window and holds real Win32 focus; a view's ring
  // is this project's own idea of focus. Only one of the two can be true, and
  // the peer reports its own onFocus, so this just gives up the ring.
  setFocusedView(nullptr);
}

bool Win32FocusManager::moveFocus(bool forward) {
  const std::vector<RnWin32View *> stops = collectStops();
  if (stops.empty()) {
    return false;
  }

  // Where focus is now. A <TextInput> holds it through its peer and reports no
  // tag, so both kinds of stop have to be recognised.
  const HWND focusedWindow = GetFocus();
  std::size_t index = stops.size();
  for (std::size_t i = 0; i < stops.size(); i++) {
    const bool isPeer =
        stops[i]->editablePeer() != nullptr && stops[i]->editablePeer() == focusedWindow;
    const bool isView = focusedTag_ != 0 && static_cast<Tag>(stops[i]->tag()) == focusedTag_;
    if (isPeer || isView) {
      index = i;
      break;
    }
  }

  std::size_t next = 0;
  if (index == stops.size()) {
    next = forward ? 0 : stops.size() - 1;
  } else if (forward) {
    next = (index + 1) % stops.size();
  } else {
    next = (index + stops.size() - 1) % stops.size();
  }

  RnWin32View *target = stops[next];
  if (HWND peer = target->editablePeer()) {
    // Real Win32 focus for a real window. The ring goes out first: the peer
    // draws its own focus, and two of them at once would say focus is in two
    // places.
    setFocusedView(nullptr);
    SetFocus(peer);
    return true;
  }

  // Taking focus away from a field is the other half of the same rule, and
  // Windows will not do it on its own: the peer keeps focus until something
  // else asks for it, and nothing here is a window that can.
  if (focusedWindow != nullptr && focusedWindow != GetAncestor(focusedWindow, GA_ROOT)) {
    SetFocus(GetAncestor(focusedWindow, GA_ROOT));
  }
  setFocusedView(target);
  return true;
}

bool Win32FocusManager::activateFocused() {
  if (focusedTag_ == 0) {
    return false;
  }
  // True whether or not anything is listening: the key belongs to the focused
  // view either way, and letting it travel on because a view between a Remove
  // and its Delete has no emitter would deliver it somewhere else.
  // A <Switch> is toggled from here too. It listens for no click -- React
  // Native's is a controlled component driven by its own native control -- so
  // without this a switch that Tab reaches is one Enter cannot work. The same
  // call the touch path makes, and a no-op for every view that is not a switch.
  mountingManager_->pressedView(focusedTag_);

  const auto emitter = mountingManager_->eventEmitterForTag(focusedTag_);
  if (emitter == nullptr) {
    return true;
  }
  // `topClick` with an empty payload, which is exactly what React Native for
  // Android sends from a focusable view's OnClickListener. It cannot go through
  // TouchEventEmitter::onClick: that carries a PointerEvent, and Pressability
  // ignores a click with a `pointerType` on it so that a real click does not
  // fire onPress twice. See the header.
  emitter->dispatchEvent("click", folly::dynamic::object(), RawEvent::Category::Discrete);
  return true;
}

namespace {

// The ancestors of `target`, innermost first.
//
// Downwards from the root, because a RnWin32View has no parent pointer -- the
// same reason win32/Win32DropTarget.cpp walks down to find a drop target's
// ancestry. Two near-identical walks in two files is a smell; they stay apart
// because that one collects views from a view and this one collects tags from a
// tag, and merging them would mean a shared header for seventeen lines. A third
// caller should merge all three.
bool tagPathTo(win32::RnWin32View *view,
               facebook::react::Tag target,
               std::vector<facebook::react::Tag> &path) {
  if (view == nullptr) {
    return false;
  }
  path.push_back(static_cast<facebook::react::Tag>(view->tag()));
  if (static_cast<facebook::react::Tag>(view->tag()) == target) {
    return true;
  }
  for (win32::RnWin32View *child : view->children()) {
    if (tagPathTo(child, target, path)) {
      return true;
    }
  }
  path.pop_back();
  return false;
}

} // namespace

bool Win32FocusManager::handleDeclaredKey(unsigned int virtualKey) {
  // Almost every app claims nothing and presses plenty.
  if (handledKeyViewCount() == 0) {
    return false;
  }
  const std::optional<KeyCombination> pressed = keyCombinationFrom(virtualKey);
  if (!pressed.has_value()) {
    return false;
  }

  // Root-first from the walk, so reversed to put the focused view first and let
  // the innermost claim win -- the order core/KeyEvents.h expects.
  std::vector<facebook::react::Tag> path;
  if (focusedTag_ != 0) {
    tagPathTo(surfaceRoot_, focusedTag_, path);
    std::reverse(path.begin(), path.end());
  } else if (surfaceRoot_ != nullptr) {
    // Nothing focused. The root is still a candidate: a window-level shortcut
    // holder is the common case and would otherwise never be asked.
    path.push_back(static_cast<facebook::react::Tag>(surfaceRoot_->tag()));
  }

  std::optional<facebook::react::Tag> handler = handledBy(path, *pressed);
  if (!handler.has_value() && focusedTag_ == 0) {
    // See the AppKit host and core/KeyEvents.h.
    handler = handledByAny(*pressed);
  }
  if (!handler.has_value()) {
    return false;
  }
  reportKey(*handler, *pressed);
  return true;
}

bool Win32FocusManager::handleKeyDown(unsigned int virtualKey) {
  switch (virtualKey) {
    case VK_TAB:
      // Shift-Tab goes backwards. GetKeyState rather than a message parameter,
      // because WM_KEYDOWN carries no modifier state of its own.
      return moveFocus((GetKeyState(VK_SHIFT) & 0x8000) == 0);
    case VK_RETURN:
    case VK_SPACE:
      // Only when something is focused. Otherwise the key goes on, so a window
      // with no focus does not swallow it.
      return activateFocused();
    default:
      return false;
  }
}

} // namespace basalt
