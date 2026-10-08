// basalt-core — Fabric mounting layer
//
// Implements ReactCxxPlatform's IMountingManager: the single interface
// ReactHost requires in order to put a React Native surface on screen.
// Everything below translates ShadowViewMutation into GTK4 widget operations.

#pragma once

#include "GtkImageLoader.h"
#include "GtkScrollView.h"
#include "GtkTextInput.h"
#include "LabelRegistry.h"
#include "MountingWalk.h"
#include "RnView.h"

#include <react/renderer/uimanager/IMountingManager.h>

#include <memory>
#include <unordered_map>

namespace basalt {

// The mutation walk, the registry and the surface-root handling all live in
// MountingWalk and are shared with every other desktop platform. What is left
// here is the part that is genuinely GTK: making a widget, parenting it, and
// turning a ShadowView into widget state.
class GtkMountingManager final : public facebook::react::IMountingManager,
                                 public MountingWalk<GtkMountingManager, RnView *> {
 public:

  // The image loader, for the host to hand to React Native's
  // `ImageLoaderModule` -- which is what `Image.getSize` and `Image.prefetch`
  // reach. Shared so that module can hold a weak reference to it.
  std::shared_ptr<GtkImageLoader> imageLoader() const { return imageLoader_; }
  GtkMountingManager();
  ~GtkMountingManager() noexcept override;

  // --- IMountingManager -----------------------------------------------------

  // Called on the JS thread, from the event loop's "update the rendering"
  // step. Hands the transaction to the GTK main thread; does not touch widgets.
  void executeMount(facebook::react::SurfaceId surfaceId,
                    facebook::react::MountingTransaction &&transaction) override;

  void dispatchCommand(const facebook::react::ShadowView &shadowView,
                       const std::string &commandName,
                       const folly::dynamic &args) override;

  facebook::react::ComponentRegistryFactory getComponentRegistryFactory() override;

  bool hasComponent(const std::string &name) override;

  // ReactCxxPlatform hands the UIManager to the mounting manager and to nothing
  // else. Reanimated needs it, and is constructed from JavaScript where no host
  // object is in reach, so this passes it on. See core/UIManagerAccess.h.
  void setUIManager(std::weak_ptr<facebook::react::UIManager> uiManager) noexcept override;

  // React Native hands this a null executor from `destroyReactInstance`, which
  // is the only notice a mounting manager gets that the instance is going away
  // -- a reload destroys one and builds another while this object survives. See
  // MountingWalk::invalidatePendingMounts.
  void setSchedulerTaskExecutor(
      facebook::react::SchedulerTaskExecutor &&schedulerTaskExecutor) noexcept override;

  // --- Host-facing ----------------------------------------------------------
  //
  // createSurfaceRoot, destroySurfaceRoot, getSurfaceRoot, viewForTag and
  // eventEmitterForTag come from MountingWalk. Callers cast the emitter to what
  // they need: GtkTouchDispatcher to TouchEventEmitter, the image path to
  // ImageEventEmitter.

  // Applies a transaction to the widget tree. Invoked on the GTK main thread by
  // executeMount's marshalling -- not part of IMountingManager, and never to be
  // called directly.
  // The main-thread half of dispatchCommand, public for the same reason
  // applyTransaction is: the idle callback that carries it across has to call
  // it, and the tests call it directly rather than spinning a main loop.
  void applyCommand(facebook::react::Tag tag,
                    const std::string &commandName,
                    const folly::dynamic &args);

  // A press ended on this view. A <Switch> is toggled from React Native's own
  // touch path rather than by the GtkSwitch's own input, so that a press means
  // the same thing on three desktops -- and so that the same synthesised tap
  // the rest of the suite uses can reach it. See applyControlPeer.
  //
  // A no-op for every view that is not a switch.
  void pressedView(facebook::react::Tag tag);

  void applyTransaction(facebook::react::SurfaceId surfaceId,
                        facebook::react::MountingTransaction &&transaction);

  // The scroll views, for the host's BASALT_TEST_SCROLL instrument. Nothing
  // else reaches in here: a real wheel arrives at the GtkEventControllerScroll
  // the manager attached itself.
  GtkScrollViewManager &scrollViews() {
    return scrollViews_;
  }

 private:
  // --- What MountingWalk asks of a platform ---------------------------------
  friend class MountingWalk<GtkMountingManager, RnView *>;

  RnView *createView(const facebook::react::ShadowView &shadowView);
  RnView *createRootView(facebook::react::Tag tag);
  void destroyView(RnView *view);
  void insertChild(RnView *parent, RnView *child, int index);
  void removeChild(RnView *parent, RnView *child);
  void updateView(RnView *view, const facebook::react::ShadowView &shadowView);
  void forgetTag(facebook::react::Tag tag);

  // The GTK half of a control: a GtkSpinner for <ActivityIndicator> and
  // <RefreshControl>, a GtkSwitch for <Switch>. Everything that is not a
  // widget -- which props mean what, which scroll view a refresh control
  // belongs to, when `onShow` fires -- is in MountingWalk.
  void applyControlPeer(RnView *view, const basalt::ControlState &state);

  // React DevTools' overlay rectangles. The parsing and the clearing are in
  // MountingWalk; this is the drawing.
  void setHighlights(RnView *view, const std::vector<basalt::Highlight> &highlights);

  // --- The GTK half of updateView -------------------------------------------
  void applyProps(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyText(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyImage(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyScrollView(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyAccessibility(RnView *view, const facebook::react::ShadowView &shadowView);

  // The LABELLED_BY relations whose resolution changed in the transaction that
  // just mounted. Once per transaction rather than per view, because a view can
  // name a label that mounts after it.
  void applyLabelRelations();
  void applyTextInput(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyLayoutMetrics(RnView *view, const facebook::react::ShadowView &shadowView);

  // Shared rather than held by value: React Native's `ImageLoaderModule` takes
  // a `weak_ptr<IImageLoader>`, and this is the loader it gets. See
  // `imageLoader()`.
  std::shared_ptr<GtkImageLoader> imageLoader_ = std::make_shared<GtkImageLoader>();
  GtkScrollViewManager scrollViews_;
  GtkTextInputManager textInputs_;

  // A <Switch> is a controlled component: the widget is not allowed to decide
  // its own state, so the value React last sent is kept here and put straight
  // back after the toggle that told JavaScript about it. React Native's iOS
  // switch does exactly this.
  std::unordered_map<facebook::react::Tag, bool> switchValues_;

  // The CSS class each tinted control is currently carrying, so the next tint
  // can take it off again. Interned strings owned by the table in the .cpp,
  // never freed here.
  std::unordered_map<facebook::react::Tag, const char *> controlClasses_;

  // The source each <Image> is currently showing, so that a mutation which
  // changed only layout does not restart the load.
  std::unordered_map<facebook::react::Tag, std::string> imageUris_;

  // Which view each `nativeID` names, and which views are waiting to be labelled
  // by one. Shared with the AppKit host; see core/LabelRegistry.h for why the
  // resolution cannot happen as the props arrive.
  basalt::LabelRegistry labels_;
};

} // namespace basalt
