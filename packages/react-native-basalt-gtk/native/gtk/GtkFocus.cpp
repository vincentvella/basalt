#include "GtkFocus.h"

#include "GtkKeyEvents.h"

#include <optional>
#include <vector>

#include <folly/dynamic.h>

#include <react/renderer/core/EventEmitter.h>

namespace basalt {

using facebook::react::EventEmitter;
using facebook::react::RawEvent;
using facebook::react::Tag;

namespace {

// The React Native view a widget belongs to, or null. A <TextInput>'s peer is a
// GtkText inside an RnView, so focus landing on the peer is focus landing on
// the wrapper as far as React Native is concerned.
RnView *owningView(GtkWidget *widget) {
  while (widget != nullptr && !RN_IS_VIEW(widget)) {
    widget = gtk_widget_get_parent(widget);
  }
  return widget == nullptr ? nullptr : RN_VIEW(widget);
}

} // namespace

GtkFocusManager::GtkFocusManager(GtkMountingManager *mountingManager, RnView *surfaceRoot)
    : mountingManager_(mountingManager), surfaceRoot_(surfaceRoot) {
  keyController_ = gtk_event_controller_key_new();
  // Bubble rather than capture: a <TextInput> with focus must see space and
  // Enter first, or typing a space in a field would activate it instead.
  gtk_event_controller_set_propagation_phase(keyController_, GTK_PHASE_BUBBLE);
  g_signal_connect(keyController_, "key-pressed", G_CALLBACK(onKeyPressed), this);
  gtk_widget_add_controller(GTK_WIDGET(surfaceRoot_), keyController_);
  // Weak, so it becomes NULL when the widget takes it down rather than
  // dangling. See the destructor.
  g_object_add_weak_pointer(G_OBJECT(keyController_),
                            reinterpret_cast<gpointer *>(&keyController_));

  // The root may or may not be in a window yet, depending on the order the host
  // built things in, so both are handled rather than one being assumed.
  attachToWindow();
  g_signal_connect(surfaceRoot_, "notify::root", G_CALLBACK(onRootChanged), this);
}

GtkFocusManager::~GtkFocusManager() {
  if (window_ != nullptr) {
    g_signal_handlers_disconnect_by_data(window_, this);
    window_ = nullptr;
  }
  if (surfaceRoot_ != nullptr && RN_IS_VIEW(surfaceRoot_)) {
    g_signal_handlers_disconnect_by_data(surfaceRoot_, this);
  }
  // The key controller belongs to the widget, and that is exactly why it has to
  // be disconnected: it outlives *this*, because both teardown paths reset the
  // focus manager before destroying the window. A key press delivered in
  // between would call `onKeyPressed` with a freed pointer.
  //
  // The dispatcher next door had the same hazard and it was not theoretical --
  // a leave crossing between the two crashed in `dispatchHoverLeave`. Weak
  // pointer rather than a flag, because the widget may have gone first and
  // taken the controller with it.
  if (keyController_ != nullptr) {
    g_signal_handlers_disconnect_by_data(keyController_, this);
    g_object_remove_weak_pointer(G_OBJECT(keyController_),
                                 reinterpret_cast<gpointer *>(&keyController_));
  }
}

void GtkFocusManager::onRootChanged(GObject * /*widget*/, GParamSpec * /*spec*/, gpointer userData) {
  static_cast<GtkFocusManager *>(userData)->attachToWindow();
}

void GtkFocusManager::attachToWindow() {
  GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(surfaceRoot_));
  GtkWindow *window = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : nullptr;
  if (window == window_) {
    return;
  }
  if (window_ != nullptr) {
    g_signal_handlers_disconnect_by_data(window_, this);
  }
  window_ = window;
  if (window_ == nullptr) {
    return;
  }
  // One signal for the whole tree. GtkWindow reports which widget holds focus,
  // so there is nothing to connect per view -- which matters, because views
  // arrive and leave with every mutation and the window does not.
  g_signal_connect(window_, "notify::focus-widget", G_CALLBACK(onFocusChanged), this);
  refreshFocus();
}

void GtkFocusManager::onFocusChanged(GObject * /*window*/, GParamSpec * /*spec*/, gpointer userData) {
  static_cast<GtkFocusManager *>(userData)->refreshFocus();
}

void GtkFocusManager::refreshFocus() {
  Tag tag = 0;
  if (window_ != nullptr) {
    if (RnView *view = owningView(gtk_window_get_focus(window_))) {
      // A <TextInput> reports its own focus and blur, through the peer that
      // actually has them. Reporting them here as well would fire onFocus twice
      // for every field in the app.
      if (rn_view_get_editable(view) == nullptr) {
        tag = static_cast<Tag>(rn_view_get_tag(view));
      }
    }
  }
  if (tag == focusedTag_) {
    return;
  }
  const Tag previous = focusedTag_;
  focusedTag_ = tag;
  if (previous != 0) {
    emitFocus(previous, false);
  }
  if (tag != 0) {
    emitFocus(tag, true);
  }
}

void GtkFocusManager::emitFocus(Tag tag, bool focused) {
  const auto emitter = mountingManager_->eventEmitterForTag(tag);
  if (emitter == nullptr) {
    return;
  }
  // `topFocus` and `topBlur`, which React Native registers as bubbling events
  // with an empty payload. BaseViewEventEmitter has both, so nothing here has
  // to know the names.
  const auto view = std::dynamic_pointer_cast<const facebook::react::ViewEventEmitter>(emitter);
  if (view == nullptr) {
    return;
  }
  if (focused) {
    view->onFocus();
  } else {
    view->onBlur();
  }
}

Tag GtkFocusManager::focusedTag() const {
  return focusedTag_;
}

gboolean GtkFocusManager::onKeyPressed(GtkEventControllerKey * /*controller*/,
                                       guint keyval,
                                       guint /*keycode*/,
                                       GdkModifierType state,
                                       gpointer userData) {
  auto *self = static_cast<GtkFocusManager *>(userData);

  // An app's own declared shortcuts first, before Escape and before activation,
  // for the reason the AppKit host gives: an app that binds Escape means it, and
  // a platform default that beat an explicit declaration could not be overridden.
  if (self->handleKey(keyval, state)) {
    return GDK_EVENT_STOP;
  }

  // Escape closes the topmost <Modal> -- or rather, asks the app to. React
  // Native's `onRequestClose` is documented as the hardware back button on
  // Android and the swipe-down on iOS; on a desktop it is Escape, and a modal
  // the app does not close in response stays up, which is deliberate.
  //
  // Handled here because this is the only key controller on the window, and
  // because a modal is a surface-wide thing rather than a focused view's.
  if (keyval == GDK_KEY_Escape) {
    return self->mountingManager_->requestCloseTopModal() ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
  }

  if (keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter && keyval != GDK_KEY_space) {
    return GDK_EVENT_PROPAGATE;
  }
  return self->activateFocused() ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
}

bool GtkFocusManager::handleKey(guint keyval, GdkModifierType state) {
  // Almost every app claims nothing and presses plenty; without this the walk
  // below would run on every keystroke in every text field.
  if (handledKeyViewCount() == 0) {
    return false;
  }
  const std::optional<KeyCombination> pressed = keyCombinationFrom(keyval, state);
  if (!pressed.has_value()) {
    return false;
  }

  // From the focused widget outwards, so the innermost claim wins. Only RnViews,
  // because only they carry a React tag -- the box a scrolled window puts in
  // between is not a step in the React tree.
  std::vector<Tag> path;
  for (GtkWidget *widget = gtk_window_get_focus(window_); widget != nullptr;
       widget = gtk_widget_get_parent(widget)) {
    if (RN_IS_VIEW(widget)) {
      path.push_back(static_cast<Tag>(rn_view_get_tag(RN_VIEW(widget))));
    }
  }

  const std::optional<Tag> handler = handledBy(path, *pressed);
  if (!handler.has_value()) {
    return false;
  }
  reportKey(*handler, *pressed);
  return true;
}

bool GtkFocusManager::activateFocused() {
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

bool GtkFocusManager::moveFocus(bool forward) {
  if (surfaceRoot_ == nullptr) {
    return false;
  }
  // The blur first, before anything moves.
  //
  // React Native's own order is blur-then-focus, and a <TextInput> gaining
  // focus reports it from its peer's own focus controller, which runs *before*
  // the window gets round to saying which widget holds focus now. Left alone
  // that produces focus-then-blur for a Tab from a button into a field -- both
  // events, in the wrong order. A real Tab keypress is GTK's own and can still
  // interleave that way; this is what the automated path and anything calling
  // moveFocus get right.
  if (focusedTag_ != 0) {
    const Tag leaving = focusedTag_;
    focusedTag_ = 0;
    emitFocus(leaving, false);
  }

  // GTK's own chain, which is the point: a focusable widget joins the window's
  // focus order, so the order here is the order Tab produces and nothing in
  // this project decides what "next" means.
  const GtkDirectionType direction = forward ? GTK_DIR_TAB_FORWARD : GTK_DIR_TAB_BACKWARD;
  gboolean moved = gtk_widget_child_focus(GTK_WIDGET(surfaceRoot_), direction);

  // Wrapping. `gtk_widget_child_focus` on a container stops at its last child,
  // because a real window would carry on into whatever is next -- and here the
  // surface root *is* the window's content, so there is nothing after it. The
  // AppKit side wraps, and a Tab order that runs out on one desktop and goes
  // round on the other is a difference an app did not ask for.
  if (moved == FALSE) {
    if (window_ != nullptr) {
      gtk_window_set_focus(window_, nullptr);
    }
    moved = gtk_widget_child_focus(GTK_WIDGET(surfaceRoot_), direction);
  }
  refreshFocus();
  return moved != FALSE;
}

} // namespace basalt
