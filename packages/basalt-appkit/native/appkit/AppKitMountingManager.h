// The macOS mounting manager.
//
// `IMountingManager` is the one interface `ReactHost` requires in order to put
// a surface on screen. Almost none of what that takes is here: the mutation
// walk, the view registry, the event-emitter bookkeeping and the surface roots
// are in `core/MountingWalk.h`, written once and shared with GTK. What is left
// below is the part that is genuinely AppKit -- making a view, parenting it,
// and turning a ShadowView into layer state.
//
// That division is the claim this file exists to test. If it holds, a third
// desktop is a view layer and a few hundred lines rather than a second port of
// everything.

#pragma once

#import "AppKitImageLoader.h"
#import "AppKitScrollView.h"
#import "AppKitTextInput.h"

#include "MountingWalk.h"
#include "RnAppKitView.h"

#include <react/renderer/uimanager/IMountingManager.h>

#include <string>

namespace basalt {

class AppKitMountingManager final : public facebook::react::IMountingManager,
                                 public MountingWalk<AppKitMountingManager, RnAppKitView *> {
 public:

  // The image loader, for the host to hand to React Native's
  // `ImageLoaderModule` -- which is what `Image.getSize` and `Image.prefetch`
  // reach. Shared so that module can hold a weak reference to it.
  std::shared_ptr<AppKitImageLoader> imageLoader() const { return imageLoader_; }
  AppKitMountingManager();
  ~AppKitMountingManager() noexcept override;

  // --- IMountingManager -----------------------------------------------------

  // Called on the JS thread, from the event loop's "update the rendering" step.
  // Hands the transaction to the main queue; does not touch a view.
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

  // --- Host-facing ----------------------------------------------------------
  //
  // createSurfaceRoot, destroySurfaceRoot, getSurfaceRoot, viewForTag and
  // eventEmitterForTag all come from MountingWalk.

  // Applies a transaction to the view tree. Invoked on the main thread by
  // executeMount's marshalling -- not part of IMountingManager, and never to be
  // called directly. Public because the dispatch block has to reach it, and
  // because the tests call it rather than spinning a run loop.
  void applyTransaction(facebook::react::SurfaceId surfaceId,
                        facebook::react::MountingTransaction &&transaction);

  // A press ended on this view. A <Switch> is toggled from React Native's own
  // touch path rather than by the NSSwitch's own input, so that a press means
  // the same thing on three desktops -- and so that the same synthesised tap
  // the rest of the suite uses can reach it. See applyControlPeer.
  //
  // A no-op for every view that is not a switch.
  void pressedView(facebook::react::Tag tag);

  // The main-thread half of dispatchCommand, public for the same reason
  // applyTransaction is.
  void applyCommand(facebook::react::Tag tag,
                    const std::string &commandName,
                    const folly::dynamic &args);

 private:
  // --- What MountingWalk asks of a platform ---------------------------------
  friend class MountingWalk<AppKitMountingManager, RnAppKitView *>;

  RnAppKitView *createView(const facebook::react::ShadowView &shadowView);
  RnAppKitView *createRootView(facebook::react::Tag tag);
  void destroyView(RnAppKitView *view);
  void insertChild(RnAppKitView *parent, RnAppKitView *child, int index);
  void removeChild(RnAppKitView *parent, RnAppKitView *child);
  void updateView(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
  void forgetTag(facebook::react::Tag tag);

  // The AppKit half of a control: an NSProgressIndicator for
  // <ActivityIndicator> and <RefreshControl>, an NSSwitch for <Switch>.
  // Everything that is not a view -- which props mean what, which scroll view
  // a refresh control belongs to, when `onShow` fires -- is in MountingWalk.
  void applyControlPeer(RnAppKitView *view, const basalt::ControlState &state);

  // React DevTools' overlay rectangles. The parsing and the clearing are in
  // MountingWalk; this is the drawing.
  void setHighlights(RnAppKitView *view, const std::vector<basalt::Highlight> &highlights);

  // --- The AppKit half of updateView ----------------------------------------
  void applyProps(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
  void applyText(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
  void applyScrollView(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
  void applyImage(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
  void applyAccessibility(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
  void applyTextInput(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
  void applyLayoutMetrics(RnAppKitView *view, const facebook::react::ShadowView &shadowView);

  AppKitScrollViewManager scrollViews_;
  AppKitTextInputManager textInputs_;
  // Shared rather than held by value: React Native's `ImageLoaderModule` takes
  // a `weak_ptr<IImageLoader>`, and this is the loader it gets.
  std::shared_ptr<AppKitImageLoader> imageLoader_ = std::make_shared<AppKitImageLoader>();

  // A <Switch> is a controlled component: the widget is not allowed to decide
  // its own state, so the value React last sent is kept here and put straight
  // back after the toggle that told JavaScript about it -- which is what React
  // Native's own iOS switch does.
  std::unordered_map<facebook::react::Tag, bool> switchValues_;


  // The source each <Image> is currently showing, so that a mutation which
  // changed only layout does not restart the load.
  std::unordered_map<facebook::react::Tag, std::string> imageUris_;
};

} // namespace basalt
