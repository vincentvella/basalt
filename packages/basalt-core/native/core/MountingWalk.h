// The mutation walk, with no toolkit in it.
//
// `IMountingManager` has two pure-virtual methods, and one of them --
// `executeMount` -- is where a platform is most likely to be wrong on its own.
// Fabric's mutation stream has rules that are not obvious from the interface:
// a Create does not attach, a Remove does not destroy, an Insert's index counts
// positions in the parent's *final* child list, virtual views have no peer at
// all, and a mutation naming a tag that no longer exists has to be survivable
// rather than fatal.
//
// None of that is about GTK or AppKit. Writing it twice means two chances to get
// it subtly different, and a difference here shows up as a layout that is wrong
// on one desktop and right on another -- which is the exact failure this project
// exists to avoid. So it is written once, here, and each platform supplies the
// half-dozen operations that do touch a view.
//
// CRTP rather than virtual dispatch: the view type differs per platform
// (`RnView *` under GObject, `RnAppKitView *` under ARC) and a template keeps it
// concrete in each translation unit, with no type erasure and no indirection on
// a path that runs for every mutation of every frame.
//
// A platform provides:
//
//   using ViewRef            = ...;       // a pointer, nullable
//   ViewRef createView(const ShadowView &);   // owned; props not yet applied
//   ViewRef createRootView(Tag);              // owned; a surface's root
//   void    destroyView(ViewRef);             // release the owned reference
//   void    insertChild(ViewRef parent, ViewRef child, int index);
//   void    removeChild(ViewRef parent, ViewRef child);
//   void    updateView(ViewRef, const ShadowView &);  // props, layout, state
//   void    forgetTag(Tag);                   // per-tag side tables, if any
//   void    applyControlPeer(ViewRef, const ControlState &);  // spinner/switch
//   void    setHighlights(ViewRef, const std::vector<Highlight> &);  // DevTools
//
// and must call `releaseAllViews()` from its own destructor: this base cannot,
// because by the time a base destructor runs the platform half is already gone.

#pragma once

#include "DebuggingOverlay.h"
#include "DesktopControls.h"
#include "HoverTracker.h"
#include "TestSettle.h"
#include "PlatformServices.h"
#include "PullToRefresh.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowViewMutation.h>

#include <glog/logging.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace basalt {

template <typename Platform, typename ViewRefT>
class MountingWalk {
 public:
  using ViewRef = ViewRefT;
  using Tag = facebook::react::Tag;
  using SurfaceId = facebook::react::SurfaceId;
  using ShadowView = facebook::react::ShadowView;

  MountingWalk() : mainThreadId_(std::this_thread::get_id()) {}

  // The mounted view for a tag, or null. Not `viewForTag` on purpose: platform
  // code reads better saying what kind of thing it wants back.
  ViewRef viewForTag(Tag tag) const {
    const auto it = registry_.find(tag);
    return it == registry_.end() ? ViewRef{} : it->second;
  }

  // The event emitter for a mounted view, or null.
  //
  // Kept per tag rather than on the view because a view holds no React Native
  // types, and because a view can sit detached between a Remove and its Delete
  // while still needing to deliver a cancel.
  facebook::react::EventEmitter::Shared eventEmitterForTag(Tag tag) const {
    const auto it = eventEmitters_.find(tag);
    return it == eventEmitters_.end() ? nullptr : it->second;
  }

  // Which hover events this view listens for, as a HoverListener mask.
  //
  // Recorded here, next to the emitter, for the same reason: it comes off the
  // props on every Create and Update, and a touch dispatcher asking per motion
  // event must not have to reach into the shadow tree to find it. Views that
  // listen for none -- nearly all of them -- are not stored at all.
  // --- Controls -------------------------------------------------------------
  //
  // <Switch>, <ActivityIndicator>, <RefreshControl> and <Modal> are the four
  // components that are more than a box, and every part of them that is not
  // pixels is here rather than in three view layers: which views are controls,
  // which scroll view a refresh control belongs to, which modals are open, and
  // what size a modal thinks the window is. Only `applyControlPeer` -- making a
  // GtkSwitch or drawing one -- is left to the platform.

  // The pull past the top of a list. The platform's scroll view feeds it and
  // calls `fireRefresh` when it says so; see core/PullToRefresh.h.
  PullToRefreshTracker &pullToRefresh() {
    return pullToRefresh_;
  }

  // Tells the <RefreshControl> belonging to `scrollTag` that it was pulled.
  // Safe on a scroll view that has none.
  void fireRefresh(Tag scrollTag) {
    const Tag control = pullToRefresh_.controlFor(scrollTag);
    if (control != 0) {
      emitRefresh(eventEmitterForTag(control));
    }
  }

  // React DevTools' overlay commands, which every host answers the same way.
  //
  // Returns false for anything else, so a platform's `applyCommand` can try
  // this first and carry on. The rectangles are parsed in
  // core/DebuggingOverlay.h; what a platform supplies is the drawing.
  bool applyOverlayCommand(Tag tag, const std::string &name, const folly::dynamic &args) {
    ViewRef view = viewForTag(tag);
    if (view == ViewRef{}) {
      return false;
    }

    if (name == "clearElementsHighlights") {
      platform().setHighlights(view, {});
      return true;
    }
    if (name == "highlightElements") {
      platform().setHighlights(view, parseElementHighlights(args));
      return true;
    }
    if (name != "highlightTraceUpdates") {
      return false;
    }

    platform().setHighlights(view, parseTraceUpdates(args));

    // A trace update flashes: React Native's own overlay clears them rather
    // than waiting to be told, because a box left behind after a component
    // stopped re-rendering says the opposite of what it means.
    //
    // The generation is what keeps a stale timer from clearing a *newer* set:
    // these arrive many times a second while the DevTools option is on, so
    // there is almost always more than one in flight.
    const std::uint64_t generation = ++overlayGeneration_;
    postDelayed(kTraceUpdateLifetimeMs, [this, alive = alive_, tag, generation] {
      if (alive.use_count() == 1 || overlayGeneration_ != generation) {
        return;
      }
      ViewRef target = viewForTag(tag);
      if (target != ViewRef{}) {
        platform().setHighlights(target, {});
      }
    });
    return true;
  }

  // The user asked to close the topmost <Modal> -- Escape, on all three
  // desktops. Returns false when no modal is open, so the host can let the key
  // do whatever it did before.
  bool requestCloseTopModal() {
    if (modalStack_.empty()) {
      return false;
    }
    emitModalRequestClose(eventEmitterForTag(modalStack_.back()));
    return true;
  }

  bool hasOpenModal() const {
    return !modalStack_.empty();
  }

  // The size of the window a surface is in, which a <Modal> lays out against.
  //
  // React Native's own cxx platform answers `ModalHostViewScreenSize()` with
  // zero, so a modal that is never told otherwise lays out 0x0 and the app
  // renders nothing at all -- no error, no warning, an empty window. The host
  // calls this wherever it already updates its layout constraints.
  void setSurfaceSize(float width, float height) {
    if (width == surfaceWidth_ && height == surfaceHeight_) {
      return;
    }
    surfaceWidth_ = width;
    surfaceHeight_ = height;
    for (const auto &[tag, shadowView] : modalViews_) {
      (void)tag;
      if (modalScreenSizeNeedsUpdate(shadowView, surfaceWidth_, surfaceHeight_)) {
        updateModalScreenSize(shadowView, surfaceWidth_, surfaceHeight_);
      }
    }
  }

  std::uint16_t hoverListenersForTag(Tag tag) const {
    const auto it = hoverListeners_.find(tag);
    return it == hoverListeners_.end() ? HoverListenerNone : it->second;
  }

  // Fabric emits no Create for a surface's root: the root shadow node is the
  // base of every diff, so it must already exist when the first transaction
  // arrives. The host makes one before `ReactHost::startSurface` and parents it
  // into a window. In Fabric a SurfaceId *is* the root node's tag, which is what
  // lets the root take part in the registry like any other view.
  ViewRef createSurfaceRoot(SurfaceId surfaceId) {
    const Tag rootTag = static_cast<Tag>(surfaceId);
    if (const auto it = registry_.find(rootTag); it != registry_.end()) {
      return it->second;
    }
    ViewRef root = platform().createRootView(rootTag);
    registry_[rootTag] = root;
    return root;
  }

  void destroySurfaceRoot(SurfaceId surfaceId) {
    const Tag rootTag = static_cast<Tag>(surfaceId);
    if (const auto it = registry_.find(rootTag); it != registry_.end()) {
      platform().destroyView(it->second);
      registry_.erase(it);
    }
  }

  ViewRef getSurfaceRoot(SurfaceId surfaceId) const {
    return viewForTag(static_cast<Tag>(surfaceId));
  }

  // Applies one transaction's mutations, in order, on the platform's main
  // thread. Read off `StubViewTree::mutate`, React Native's own reference walk.
  void applyMutations(const facebook::react::ShadowViewMutationList &mutations) {
    assert(std::this_thread::get_id() == mainThreadId_ &&
           "mutations must be applied on the platform's main thread");

    // That React has produced a tree and a host has put it on screen. The one
    // place all three mount through, which is why BASALT_QUIT_WHEN_SETTLED needs
    // this in one file rather than in three. Only the first is kept; see
    // core/TestSettle.h.
    noteMountApplied();

    for (const auto &mutation : mutations) {
      switch (mutation.type) {
        case facebook::react::ShadowViewMutation::Create:
          create(mutation.newChildShadowView);
          break;
        case facebook::react::ShadowViewMutation::Delete:
          destroy(mutation.oldChildShadowView.tag);
          break;
        case facebook::react::ShadowViewMutation::Insert:
          insert(mutation);
          break;
        case facebook::react::ShadowViewMutation::Remove:
          remove(mutation);
          break;
        case facebook::react::ShadowViewMutation::Update:
          update(mutation.newChildShadowView);
          break;
      }
    }

    // After the whole transaction rather than at each Insert.
    //
    // Fabric inserts a subtree from the bottom up: a <RefreshControl> is put
    // inside the scroll view's content view before that content view is put
    // inside the scroll view, so at the moment the control arrives its
    // grandparent is not known yet and walking up finds nothing. The tree is
    // whole here.
    resolveRefreshControls();
  }

 protected:
  // Not virtual and not public: this is a mixin, never a base pointer.
  ~MountingWalk() = default;

  // Platforms call this from their own destructor. See the note at the top.
  void releaseAllViews() {
    for (auto &[tag, view] : registry_) {
      (void)tag;
      platform().destroyView(view);
    }
    registry_.clear();
    eventEmitters_.clear();
    componentNames_.clear();
    hoverListeners_.clear();
    parentOf_.clear();
    scrollTags_.clear();
    modalViews_.clear();
    modalStack_.clear();
  }

  void rememberEventEmitter(const ShadowView &shadowView) {
    if (shadowView.eventEmitter != nullptr) {
      eventEmitters_[shadowView.tag] = shadowView.eventEmitter;
    }
    if (shadowView.componentName != nullptr) {
      componentNames_[shadowView.tag] = shadowView.componentName;
    }
    rememberHoverListeners(shadowView);
  }

  // An Update can take listeners away as well as add them, so a view that stops
  // listening is erased rather than left with its old mask.
  void rememberHoverListeners(const ShadowView &shadowView) {
    const auto *viewProps = dynamic_cast<const facebook::react::ViewProps *>(shadowView.props.get());
    const std::uint16_t mask =
        viewProps == nullptr ? HoverListenerNone : hoverListenersFrom(viewProps->events);
    if (mask == HoverListenerNone) {
      hoverListeners_.erase(shadowView.tag);
    } else {
      hoverListeners_[shadowView.tag] = mask;
    }
  }

  bool onMainThread() const {
    return std::this_thread::get_id() == mainThreadId_;
  }

  // The portable half of mounting a control, called from the platform's
  // `updateView`. Reads the props once, hands the platform the result, and
  // keeps the bookkeeping the events need.
  //
  // Called from `updateView` rather than from the walk so that a platform
  // applies it in its own order -- after layout on every host, because a
  // spinner is centred in a frame that has to exist first.
  void applyControls(ViewRef view, const ShadowView &shadowView) {
    const ControlState state = controlStateOf(shadowView);
    if (state.kind != ControlKind::None) {
      platform().applyControlPeer(view, state);
      return;
    }

    if (shadowView.componentName == nullptr) {
      return;
    }
    const std::string_view name(shadowView.componentName);
    if (name == "ScrollView") {
      scrollTags_.insert(shadowView.tag);
      return;
    }
    if (name != "ModalHostView") {
      return;
    }

    // A modal, which is where the screen size has to be corrected. The first
    // sighting is also where `onShow` belongs: React Native fires it once, when
    // the modal appears, and a modal appears by being mounted -- `visible` is
    // false by not rendering the component at all.
    const bool firstSighting = modalViews_.find(shadowView.tag) == modalViews_.end();
    modalViews_[shadowView.tag] = shadowView;
    if (firstSighting) {
      modalStack_.push_back(shadowView.tag);
      emitModalShow(shadowView.eventEmitter);
    }
    if (modalScreenSizeNeedsUpdate(shadowView, surfaceWidth_, surfaceHeight_)) {
      updateModalScreenSize(shadowView, surfaceWidth_, surfaceHeight_);
    }
  }

 private:
  Platform &platform() {
    return static_cast<Platform &>(*this);
  }

  // Create allocates a view but does not attach it; an Insert follows, possibly
  // in a later transaction.
  void create(const ShadowView &shadowView) {
    ViewRef view = platform().createView(shadowView);
    platform().updateView(view, shadowView);
    registry_[shadowView.tag] = view;
    rememberEventEmitter(shadowView);
  }

  void destroy(Tag tag) {
    const auto it = registry_.find(tag);
    if (it == registry_.end()) {
      LOG(WARNING) << "Delete for unknown tag " << tag;
      return;
    }
    platform().destroyView(it->second);
    registry_.erase(it);
    eventEmitters_.erase(tag);
    componentNames_.erase(tag);
    hoverListeners_.erase(tag);
    parentOf_.erase(tag);
    scrollTags_.erase(tag);
    pullToRefresh_.forget(tag);
    modalViews_.erase(tag);
    modalStack_.erase(std::remove(modalStack_.begin(), modalStack_.end(), tag), modalStack_.end());
    platform().forgetTag(tag);
  }

  void insert(const facebook::react::ShadowViewMutation &mutation) {
    // Virtual views exist in the shadow tree only, to keep an EventEmitter
    // alive. They have no peer to parent.
    if (mutation.mutatedViewIsVirtual()) {
      return;
    }
    ViewRef parent = viewForTag(mutation.parentTag);
    ViewRef child = viewForTag(mutation.newChildShadowView.tag);
    if (parent == ViewRef{} || child == ViewRef{}) {
      // Survivable rather than fatal: this is what a bug upstream, or a
      // transaction racing a surface teardown, looks like from here.
      LOG(WARNING) << "Insert with unknown tag (parent " << mutation.parentTag << ", child "
                   << mutation.newChildShadowView.tag << ")";
      return;
    }
    // Props can change in the same transaction that inserts the view.
    platform().updateView(child, mutation.newChildShadowView);
    platform().insertChild(parent, child, static_cast<int>(mutation.index));
    parentOf_[mutation.newChildShadowView.tag] = mutation.parentTag;
    if (controlKindFor(mutation.newChildShadowView.componentName) == ControlKind::PullToRefresh) {
      unresolvedRefreshControls_.push_back(mutation.newChildShadowView.tag);
    }
  }

  void remove(const facebook::react::ShadowViewMutation &mutation) {
    if (mutation.mutatedViewIsVirtual()) {
      return;
    }
    ViewRef parent = viewForTag(mutation.parentTag);
    ViewRef child = viewForTag(mutation.oldChildShadowView.tag);
    if (parent == ViewRef{} || child == ViewRef{}) {
      LOG(WARNING) << "Remove with unknown tag (parent " << mutation.parentTag << ", child "
                   << mutation.oldChildShadowView.tag << ")";
      return;
    }
    // Detached, not destroyed: the Delete is a separate mutation and may never
    // come -- a reparent is a Remove and an Insert with no Delete between them.
    platform().removeChild(parent, child);
  }

  void update(const ShadowView &shadowView) {
    ViewRef view = viewForTag(shadowView.tag);
    if (view == ViewRef{}) {
      LOG(WARNING) << "Update for unknown tag " << shadowView.tag;
      return;
    }
    platform().updateView(view, shadowView);
    // A clone carries a new emitter instance; keeping the old one would deliver
    // touches to a stale target.
    rememberEventEmitter(shadowView);
  }

  // Ties each <RefreshControl> mounted in this transaction to the scroll view
  // it refreshes.
  //
  // Not its parent: React Native's ScrollView puts the control inside the
  // content container, so the scroll view is an ancestor. Walking up is also
  // what makes this survive somebody wrapping the control in a <View>.
  //
  // A control whose scroll view is still not found is dropped rather than
  // carried: it is a <RefreshControl> outside any list, which is an app bug
  // and not something to keep retrying on every frame.
  void resolveRefreshControls() {
    for (const Tag tag : unresolvedRefreshControls_) {
      for (Tag walk = tag; walk != 0;) {
        const auto parent = parentOf_.find(walk);
        if (parent == parentOf_.end()) {
          break;
        }
        walk = parent->second;
        if (scrollTags_.count(walk) != 0) {
          pullToRefresh_.attach(walk, tag);
          break;
        }
      }
    }
    unresolvedRefreshControls_.clear();
  }

  // Views are held with a strong reference from Create until Delete. Between a
  // Remove and its Delete a view has no parent, so this is the only thing
  // keeping it alive.
  std::unordered_map<Tag, ViewRef> registry_;

  // Parallel to registry_, and torn down with it on Delete.
  std::unordered_map<Tag, facebook::react::EventEmitter::Shared> eventEmitters_;

  // Every view's component name, as the static string Fabric holds. Needed
  // because a Remove and an Insert carry a ShadowView and the walk's own
  // bookkeeping does not.
  std::unordered_map<Tag, const char *> componentNames_;

  // Sparse, unlike the two above: only views that listen for a hover event
  // appear, so an app that uses none carries an empty map.
  std::unordered_map<Tag, std::uint16_t> hoverListeners_;

  // --- Controls ---------------------------------------------------------------

  // Who each view's parent is, so a <RefreshControl> can find the scroll view
  // it is nested inside. Fabric's mutations carry it and nothing else here
  // needed it until now.
  std::unordered_map<Tag, Tag> parentOf_;
  std::unordered_set<Tag> scrollTags_;
  PullToRefreshTracker pullToRefresh_;
  // Controls mounted in the transaction being applied, resolved at the end of
  // it; see resolveRefreshControls.
  std::vector<Tag> unresolvedRefreshControls_;

  // Modals, and the order they opened in. A vector rather than a set: Escape
  // closes the topmost one, which needs an order.
  std::unordered_map<Tag, ShadowView> modalViews_;
  std::vector<Tag> modalStack_;

  float surfaceWidth_{0.0F};
  float surfaceHeight_{0.0F};

  // DevTools' trace updates, and the token that tells a timer whether this
  // manager is still here. The scroll views keep the same pair for the same
  // reason: a delayed callback outlives whatever scheduled it.
  std::uint64_t overlayGeneration_{0};
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};

  // The main thread, recorded at construction. `executeMount` arrives on the JS
  // thread and marshals here; `applyMutations` asserts it got there.
  std::thread::id mainThreadId_;
};

} // namespace basalt
