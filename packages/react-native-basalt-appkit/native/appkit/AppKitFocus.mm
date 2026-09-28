#import "AppKitFocus.h"

#import "AppKitTextPeer.h"

#import "AppKitKeyEvents.h"

#include <optional>
#include <vector>

#include <folly/dynamic.h>

#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/core/EventEmitter.h>

// The bridge between AppKit's protocol and the C++ manager. A C++ object cannot
// conform to an Objective-C protocol, and the root holds its handler weakly, so
// this exists and the manager owns it -- the same arrangement
// AppKitTouchDispatcher has.
@interface RnAppKitFocusTarget : NSObject <RnAppKitFocusHandler>
@property(nonatomic, assign) basalt::AppKitFocusManager *manager;
@end

@implementation RnAppKitFocusTarget

- (void)rnView:(RnAppKitView *)view didChangeFocus:(BOOL)focused {
  if (_manager != nullptr) {
    _manager->viewDidChangeFocus(view, focused);
  }
}

- (BOOL)rnActivateView:(RnAppKitView *)view {
  return _manager != nullptr && _manager->activate(view);
}

- (BOOL)rnMoveFocusForward:(BOOL)forward {
  return _manager != nullptr && _manager->moveFocus(forward) ? YES : NO;
}

- (BOOL)rnView:(RnAppKitView *)view handlesKey:(NSEvent *)event {
  return _manager != nullptr && _manager->handleKey(view, event) ? YES : NO;
}

@end

namespace basalt {

using facebook::react::RawEvent;
using facebook::react::Tag;

AppKitFocusManager::AppKitFocusManager(AppKitMountingManager *mountingManager,
                                       RnAppKitView *surfaceRoot)
    : mountingManager_(mountingManager), surfaceRoot_(surfaceRoot) {
  RnAppKitFocusTarget *target = [[RnAppKitFocusTarget alloc] init];
  target.manager = this;
  focusTarget_ = target;
  surfaceRoot_.rnFocusHandler = target;
}

AppKitFocusManager::~AppKitFocusManager() {
  // The root's reference is weak, so it goes nil on its own; clearing the back
  // pointer first means a focus change already in flight cannot reach a dead
  // object.
  ((RnAppKitFocusTarget *)focusTarget_).manager = nullptr;
  focusTarget_ = nil;
}

void AppKitFocusManager::viewDidChangeFocus(RnAppKitView *view, bool focused) {
  const auto tag = static_cast<Tag>(view.rnTag);
  if (focused) {
    // A blur for the view that had it, before the focus for the one that took
    // it. AppKit resigns before it makes anyone else first responder, so this
    // is usually already true; it is written out because an app reading
    // onFocus/onBlur as a pair must not see two focuses in a row.
    if (focusedTag_ != 0 && focusedTag_ != tag) {
      emitFocus(focusedTag_, false);
    }
    focusedTag_ = tag;
    emitFocus(tag, true);
    return;
  }
  if (focusedTag_ != tag) {
    return;
  }
  focusedTag_ = 0;
  emitFocus(tag, false);
}

void AppKitFocusManager::emitFocus(Tag tag, bool focused) {
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

bool AppKitFocusManager::activate(RnAppKitView *view) {
  return dispatchClick(static_cast<Tag>(view.rnTag));
}

bool AppKitFocusManager::handleKey(RnAppKitView *view, NSEvent *event) {
  // Cheap first: almost every app declares no keys at all, and every one of them
  // presses some. Without this the walk below runs on every keystroke in every
  // text field in every app that never asked for a shortcut.
  if (handledKeyViewCount() == 0) {
    return false;
  }

  const std::optional<KeyCombination> pressed = keyCombinationFrom(event);
  if (!pressed.has_value()) {
    // A bare modifier or a dead key. Nothing to match and nothing to consume.
    return false;
  }
  return deliverKeyFrom(view, *pressed);
}

bool AppKitFocusManager::deliverKeyFrom(RnAppKitView *view, const KeyCombination &pressed) {
  // From the first responder outwards, so the innermost claim wins -- the order
  // core/KeyEvents.h's `handledBy` expects. Only RnAppKitViews, because only they
  // have a React tag; a scroll view's clip view in between is not a step in the
  // React tree.
  std::vector<Tag> path;
  for (NSView *current = view; current != nil; current = current.superview) {
    if ([current isKindOfClass:[RnAppKitView class]]) {
      path.push_back(static_cast<Tag>(((RnAppKitView *)current).rnTag));
    }
  }

  std::optional<Tag> handler = handledBy(path, pressed);
  if (!handler.has_value() && focusedTag_ == 0) {
    // Nothing focused, so there is no path to be outside of and no text field
    // taking keys. A window-level shortcut holder is a child of the root rather
    // than the root itself, so the walk above cannot reach it -- and that is
    // every app that declares its shortcuts once at the top. See
    // core/KeyEvents.h on why this is only safe unfocused.
    handler = handledByAny(pressed);
  }
  if (!handler.has_value()) {
    return false;
  }
  // Decided. Reporting is asynchronous from here -- it crosses to the JavaScript
  // thread -- which is why the answer could not have come from there.
  reportKey(*handler, pressed);
  return true;
}

bool AppKitFocusManager::deliverTestKey(const KeyCombination &pressed) {
  // From whatever has focus, or from the root when nothing does -- which is the
  // ordinary case for a window-level shortcut holder and the one a scenario
  // exercises. A real press reaches the first responder, and with nothing
  // focusable focused that is the root too.
  RnAppKitView *from = nil;
  if (focusedTag_ != 0 && mountingManager_ != nullptr) {
    from = mountingManager_->viewForTag(focusedTag_);
  }
  if (from == nil) {
    from = surfaceRoot_;
  }
  return deliverKeyFrom(from, pressed);
}

bool AppKitFocusManager::activateFocused() {
  if (focusedTag_ == 0) {
    return false;
  }
  // A <Switch> is toggled from here too. It listens for no click -- React
  // Native's is a controlled component driven by its own native control -- so
  // without this a switch that Tab reaches is one Enter cannot work. The same
  // call the touch path makes, and a no-op for every view that is not a switch.
  mountingManager_->pressedView(focusedTag_);
  return dispatchClick(focusedTag_);
}

bool AppKitFocusManager::dispatchClick(Tag tag) {
  // True whether or not anything is listening: the key belongs to the focused
  // view either way, and letting it travel on because a view between a Remove
  // and its Delete has no emitter would deliver it somewhere else.
  const auto emitter = mountingManager_->eventEmitterForTag(tag);
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

// Everything Tab should stop on, in tree order.
//
// Tree order rather than paint order: zIndex changes what is on top and must
// not change what Tab reaches next, which is also what the GTK side's chain
// does. A <TextInput> contributes its peer rather than its view, because the
// peer is the thing AppKit will make first responder -- so a field sits in the
// same order as the buttons around it without this file knowing what a text
// input is beyond that.
void collectFocusStops(RnAppKitView *view, NSMutableArray<RnAppKitView *> *stops) {
  if (view == nil || view.hidden) {
    return;
  }
  if (view.rnEditable != nil || view.rnFocusable) {
    [stops addObject:view];
  }
  for (NSView *child in view.subviews) {
    if ([child isKindOfClass:[RnAppKitView class]]) {
      collectFocusStops((RnAppKitView *)child, stops);
    }
  }
}

// What AppKit should be asked to make first responder for a stop.
NSResponder *responderForStop(RnAppKitView *view) {
  return view.rnEditable != nil ? (NSResponder *)view.rnEditable : (NSResponder *)view;
}

// Whether this stop is the one that holds focus now. Not just a pointer
// comparison: while a single-line field is being edited the first responder is
// its *field editor* rather than the field.
bool stopHoldsFocus(RnAppKitView *view, NSResponder *current) {
  if (current == nil) {
    return false;
  }
  if (responderForStop(view) == current) {
    return true;
  }
  return view.rnEditable != nil && RnPeerEditor(view.rnEditable) == current;
}

} // namespace

bool AppKitFocusManager::moveFocus(bool forward) {
  NSWindow *window = surfaceRoot_.window;
  if (window == nil) {
    return false;
  }

  NSMutableArray<RnAppKitView *> *stops = [NSMutableArray array];
  collectFocusStops(surfaceRoot_, stops);
  if (stops.count == 0) {
    return false;
  }

  // Where focus is now. Not focusedTag_: a <TextInput> holds focus through its
  // peer and never reports a tag here, so the responder is the question with an
  // answer for both kinds of stop.
  NSResponder *current = window.firstResponder;
  NSUInteger index = [stops indexOfObjectPassingTest:^BOOL(RnAppKitView *stop, NSUInteger, BOOL *) {
    return stopHoldsFocus(stop, current) ? YES : NO;
  }];

  NSUInteger next = 0;
  if (index == NSNotFound) {
    next = forward ? 0 : stops.count - 1;
  } else if (forward) {
    next = (index + 1) % stops.count;
  } else {
    next = (index + stops.count - 1) % stops.count;
  }
  // Wrapping, unlike GTK's `gtk_widget_child_focus` on a container, which stops
  // at the end. A window's Tab order wraps on both desktops, and this is the
  // window's order.
  return [window makeFirstResponder:responderForStop(stops[next])] == YES;
}

} // namespace basalt
