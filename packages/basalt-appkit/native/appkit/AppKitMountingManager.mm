#import "AppKitMountingManager.h"

#import "CoreTextLayout.h"

// CIFilter, which is the only way to tint an NSProgressIndicator.
#import <CoreImage/CoreImage.h>

#include "ComponentRegistry.h"
#include "BackgroundLayers.h"
#include "TextShadows.h"
#include "WritingDirections.h"
#include "BlendModes.h"
#include "LegacyShadow.h"
#include "CursorNames.h"
#include "Filters.h"
#include "Gradients.h"
#include "ExpoImageComponent.h"
#ifdef BASALT_HAS_SKIA
#include "AppKitSkiaPeer.h"
#endif
#include "UIManagerAccess.h"

#include <cstdint>

#include <react/renderer/components/view/AccessibilityProps.h>
#include <react/renderer/components/image/ImageEventEmitter.h>
#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/graphics/Color.h>

#include <glog/logging.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <string_view>
#include <type_traits>


// An NSSwitch that never takes a click.
//
// A <Switch> is toggled from React Native's own touch path -- see
// `pressedView` -- rather than by the control's own input, so that a press
// means the same thing on three desktops: the Windows one is painted and has
// no widget to click at all. Refusing the hit is what lets the press fall
// through to the RnAppKitView behind it, which is where the touch dispatcher
// picks it up. The switch still draws, animates and reports itself to
// accessibility exactly as it would.
@interface RnAppKitSwitch : NSSwitch
@end

@implementation RnAppKitSwitch
- (NSView *)hitTest:(NSPoint)point {
  (void)point;
  return nil;
}
@end

namespace basalt {

// `override` proves each signature matches the interface, but not that every
// pure virtual is implemented -- nothing here instantiates the class. This
// catches an IMountingManager method going unimplemented as the interface
// evolves upstream.
static_assert(!std::is_abstract_v<AppKitMountingManager>,
              "AppKitMountingManager must implement all of IMountingManager");

using facebook::react::ColorComponents;
using facebook::react::ComponentRegistryFactory;
using facebook::react::MountingTransaction;
using facebook::react::ImageEventEmitter;
using facebook::react::ImageProps;
using facebook::react::ImageResizeMode;
using facebook::react::ParagraphState;
using facebook::react::ShadowView;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::ViewProps;

AppKitMountingManager::AppKitMountingManager()
    : scrollViews_([this](Tag tag) { return eventEmitterForTag(tag); }),
      textInputs_([this](Tag tag) { return eventEmitterForTag(tag); }) {
  // The pull past the top of a list, counted in core/PullToRefresh.h and fired
  // at whichever <RefreshControl> that scroll view has. Identical on all three
  // desktops, which is the point of the two of them being portable.
  scrollViews_.setOverscrollTopHandler([this](Tag tag, double amount) {
    if (amount <= 0.0) {
      pullToRefresh().release(tag);
      return;
    }
    if (pullToRefresh().pull(tag, amount)) {
      fireRefresh(tag);
    }
  });
}

AppKitMountingManager::~AppKitMountingManager() noexcept {
  // MountingWalk cannot do this itself: by the time a base destructor runs, the
  // AppKit half that knows how to release a view is already gone.
  releaseAllViews();
}

// ---------------------------------------------------------------------------
// Getting onto the main thread
// ---------------------------------------------------------------------------

namespace {

// Carries a transaction from the JS thread to the main thread. A heap struct
// rather than a captured lambda because MountingTransaction is move-only, and
// this is the shape the GTK side uses for the same trip.
struct PendingMount {
  AppKitMountingManager *manager;
  SurfaceId surfaceId;
  MountingTransaction transaction;
  // Whether `manager` is still there, and which instance this came from. See
  // MountingWalk::mountGuard.
  std::weak_ptr<bool> alive;
  std::uint64_t epoch;
};

void applyPendingMount(void *data) {
  std::unique_ptr<PendingMount> pending{static_cast<PendingMount *>(data)};
  // The guard first: dereferencing `manager` to read its epoch is itself the
  // use-after-free when the host has already torn it down.
  if (pending->alive.expired()) {
    return;
  }
  if (pending->manager->mountEpoch() != pending->epoch) {
    return;
  }
  pending->manager->applyTransaction(pending->surfaceId, std::move(pending->transaction));
}

} // namespace

void AppKitMountingManager::executeMount(SurfaceId surfaceId, MountingTransaction &&transaction) {
  // This runs on the JS thread: Scheduler::uiManagerDidFinishTransaction queues
  // the mount via RuntimeScheduler::scheduleRenderingUpdate, which drains in the
  // event loop's "update the rendering" step with the jsi::Runtime live. AppKit
  // is main-thread-only -- and not politely so: it asserts and traps the process
  // -- so nothing here may touch a view.
  //
  // Always queue, never invoke directly even when already on the main thread.
  // The main queue is FIFO, which is what keeps mutation ordering intact, and
  // running one transaction inline while another is queued would break it. iOS
  // and Android marshal here too.
  auto *pending =
      new PendingMount{this, surfaceId, std::move(transaction), mountGuard(), mountEpoch()};
  dispatch_async_f(dispatch_get_main_queue(), pending, applyPendingMount);
}

void AppKitMountingManager::applyTransaction(SurfaceId surfaceId, MountingTransaction &&transaction) {
  // `maintainVisibleContentPosition` is measured before the mutations and acted
  // on after them, which is the only place either half can go: the prop is
  // about how far a child moved *during* a transaction. iOS hangs the same pair
  // off `mountingTransactionWillMount` and `...DidMount`.
  scrollViews_.prepareMaintainVisiblePosition();

  // The walk itself is in core/MountingWalk.h and is shared with GTK; what is
  // AppKit about mounting is below, in the operations it calls back into.
  applyMutations(transaction.getMutations());

  // And the other half, before anything repaints: the offset moves so that the
  // child the list was looking at is still under the eye.
  scrollViews_.adjustForMaintainVisiblePosition();

  // Now that the tree is on screen, anything `autoFocus` asked for can be given
  // focus. Not before: see AppKitTextInput.h.
  textInputs_.flushAutoFocus();

  // Tell the UIManager the transaction is on screen. Anything registered as a
  // mount hook -- Reanimated's is the one that matters here -- is waiting for
  // this, and without it an animated style is computed every frame, committed
  // to the shadow tree and never resumed, so nothing moves.
  //
  // iOS does this from RCTSurfacePresenter and Android from its mounting
  // manager. ReactCxxPlatform does it nowhere, which is a gap in the shared
  // platform rather than in either host: nothing in it had a mount hook until
  // a third-party library brought one.
  // Refuses a surface the UIManager no longer has, which is what a reload's
  // teardown leaves behind. See core/UIManagerAccess.h.
  reportMountedSurface(surfaceId);

  // After the mutations, because a view can be labelled by one that mounts after
  // it: Fabric mounts in tree order, and the label of a field often follows it.
  applyLabelRelations();

  // And after those, because an announcement is the last word on a transaction:
  // the text a live region says now is what every mutation in this batch has left
  // it saying.
  announceLiveRegions();
}

void AppKitMountingManager::announceLiveRegions() {
  if (liveRegions_.empty()) {
    return;
  }
  for (const facebook::react::Tag tag : liveRegions_.tags()) {
    RnAppKitView *view = viewForTag(tag);
    if (view == nil) {
      continue;
    }
    // A label stands in for the text when the app set one: an icon-only status
    // has no paragraph of its own, and the label is what would be read. The GTK
    // host makes the same choice in the same order.
    const auto label = liveRegionLabels_.find(tag);
    std::string text = label != liveRegionLabels_.end() ? label->second : std::string();
    if (text.empty()) {
      text = view.rnCollectedText.UTF8String != nullptr ? view.rnCollectedText.UTF8String : "";
    }
    const auto politeness = liveRegions_.noticed(tag, text);
    if (!politeness.has_value()) {
      continue;
    }
    const BOOL assertive = *politeness == basalt::LiveRegionPoliteness::Assertive ? YES : NO;
    [view rnAnnounce:[NSString stringWithUTF8String:text.c_str()] assertive:assertive];
    // Logged as well as announced: nothing in an automated run is running
    // VoiceOver, so this line is how an end-to-end test sees that it happened.
    LOG(INFO) << "announced" << (assertive ? " (assertive): " : ": ") << text;
  }
}

void AppKitMountingManager::applyLabelRelations() {
  if (labels_.empty()) {
    return;
  }
  for (const auto &change : labels_.changes()) {
    RnAppKitView *view = viewForTag(change.tag);
    if (view == nil) {
      continue;
    }
    NSMutableArray<RnAppKitView *> *resolved = [NSMutableArray array];
    for (const facebook::react::Tag tag : change.labels) {
      if (RnAppKitView *label = viewForTag(tag); label != nil) {
        [resolved addObject:label];
      }
    }
    [view setRnLabelledBy:resolved];
  }

  // The reading order, from the same registry and on the same terms: only what
  // changed, and an empty list is a view that stopped asking.
  for (const auto &change : labels_.orderChanges()) {
    RnAppKitView *view = viewForTag(change.tag);
    if (view == nil) {
      continue;
    }
    NSMutableArray<RnAppKitView *> *children = [NSMutableArray array];
    for (const facebook::react::Tag tag : change.labels) {
      if (RnAppKitView *child = viewForTag(tag); child != nil) {
        [children addObject:child];
      }
    }
    [view setRnAccessibilityOrder:children];
  }
}

namespace {

// The same trip an executeMount takes, for the same reason: this arrives on the
// JS thread, inside the event loop's rendering update, so it may not touch a
// view either.
//
// Queued on the same queue as a mount, so it stays behind the transaction that
// created the view it names -- the main queue is FIFO, and a scrollTo that
// arrived before its ScrollView was mounted would find no entry and be dropped.
struct PendingCommand {
  AppKitMountingManager *manager;
  Tag tag;
  std::string name;
  folly::dynamic args;
};

void applyPendingCommand(void *data) {
  std::unique_ptr<PendingCommand> pending{static_cast<PendingCommand *>(data)};
  pending->manager->applyCommand(pending->tag, pending->name, pending->args);
}

} // namespace

void AppKitMountingManager::dispatchCommand(const ShadowView &shadowView,
                                            const std::string &commandName,
                                            const folly::dynamic &args) {
  auto *pending = new PendingCommand{this, shadowView.tag, commandName, args};
  dispatch_async_f(dispatch_get_main_queue(), pending, applyPendingCommand);
}

void AppKitMountingManager::applyCommand(Tag tag,
                                         const std::string &commandName,
                                         const folly::dynamic &args) {
  assert(onMainThread() && "applyCommand must run on the main thread");

  if (scrollViews_.dispatchCommand(tag, commandName, args)) {
    return;
  }
  if (textInputs_.dispatchCommand(tag, commandName, args)) {
    return;
  }
  // React DevTools' overlay, answered the same way on all three: see
  // core/DebuggingOverlay.h.
  if (applyOverlayCommand(tag, commandName, args)) {
    return;
  }
  LOG(INFO) << "dispatchCommand '" << commandName << "' on tag " << tag
            << " is not implemented on macOS";
}

void AppKitMountingManager::setUIManager(std::weak_ptr<facebook::react::UIManager> uiManager) noexcept {
  setSharedUIManager(std::move(uiManager));
}

void AppKitMountingManager::setSchedulerTaskExecutor(
    facebook::react::SchedulerTaskExecutor &&schedulerTaskExecutor) noexcept {
  // A null executor means `destroyReactInstance`: the Scheduler and the
  // SurfaceManager are about to go, and anything this already queued to the main
  // queue would be applied against them. Non-null means a new instance, which
  // needs nothing from here.
  if (!schedulerTaskExecutor) {
    invalidatePendingMounts();
  }
}

ComponentRegistryFactory AppKitMountingManager::getComponentRegistryFactory() {
  return facebook::react::getDefaultComponentRegistryFactory();
}

bool AppKitMountingManager::hasComponent(const std::string &name) {
  // Only what actually mounts. Claiming more would be worse than admitting the
  // gap: the registry would build shadow nodes nothing can put on screen, and
  // the app would render blank rectangles instead of failing somewhere legible.
  //
  // Paragraph is the mountable half of <Text>; Text and RawText exist only in
  // the shadow tree, folded into the Paragraph's AttributedString.
  //
  // ScrollView's content child arrives as "ScrollContentView", which the
  // registry rewrites to "View" before it reaches here, so it needs no entry.
  //
  // The components an ordinary app is built from, plus the root; and below
  // them the controls, each of which is a real AppKit control rather than a
  // box this host draws. UnimplementedNativeView is here so that a component
  // no platform registered mounts as a view that says so rather than as
  // nothing at all.
  return name == "View" || name == "RootView" || name == "Paragraph" ||
      name == "ScrollView" || name == "Image" || name == "TextInput" ||
      name == "ActivityIndicatorView" || name == "Switch" || name == "ModalHostView" ||
      name == "PullToRefreshView" || name == "UnimplementedNativeView" ||
      name == "DebuggingOverlay";
}

// ---------------------------------------------------------------------------
// What MountingWalk asks of a platform
// ---------------------------------------------------------------------------

RnAppKitView *AppKitMountingManager::createView(const ShadowView &shadowView) {
  return [RnAppKitView viewWithTag:static_cast<NSInteger>(shadowView.tag)];
}

RnAppKitView *AppKitMountingManager::createRootView(Tag tag) {
  return [RnAppKitView viewWithTag:static_cast<NSInteger>(tag)];
}

void AppKitMountingManager::destroyView(RnAppKitView *view) {
  // Nothing to do: ARC owns these. The registry holds the only strong reference
  // between a Remove and its Delete, exactly as on GTK, but erasing the entry
  // is what releases it rather than an explicit unref. This hook stays because
  // the walk has to say *when* a view stops being owned, even on a platform
  // where saying it costs nothing.
  (void)view;
}

void AppKitMountingManager::insertChild(RnAppKitView *parent, RnAppKitView *child, int index) {
  [parent insertRnChild:child atIndex:index];
}

void AppKitMountingManager::removeChild(RnAppKitView *parent, RnAppKitView *child) {
  [parent removeRnChild:child];
}

void AppKitMountingManager::forgetTag(Tag tag) {
  liveRegions_.forget(tag);
  liveRegionLabels_.erase(tag);
  // Both sides of a label relation: a view that goes away stops naming anything
  // and stops being nameable, and a relation left pointing at it would have
  // VoiceOver read a view that is no longer on screen.
  labels_.forget(tag);
  scrollViews_.remove(tag);
  textInputs_.remove(tag);
  imageUris_.erase(tag);
  switchValues_.erase(tag);
#ifdef BASALT_HAS_SKIA
  // Unregisters the canvas and takes its layer off the view. This is the one
  // moment a view is known to be finished with, which matters more here than
  // elsewhere: a canvas left registered keeps a Metal layer and a GPU surface
  // alive for a tag nothing will draw again.
  forgetSkiaCanvas(tag);
#endif
}

namespace {

// Hands a view the frames of an animated image, or clears any it had.
//
// Asked of the loader rather than carried in the load callback, because being
// animated is a property of the file and the callback's job is the pixels. The
// arrays are the loader's and are the same arrays each time, which is what lets
// the view tell a re-mount from a new animation.
void applyImageAnimation(AppKitImageLoader *loader, RnAppKitView *view, const std::string &uri) {
  if (view == nil) {
    return;
  }
  const AppKitImageLoader::Animation *animation =
      uri.empty() ? nullptr : loader->animation(uri);
  if (animation == nullptr) {
    [view setRnImageFrames:nil delaysMs:nil loopCount:0];
    return;
  }
  [view setRnImageFrames:animation->frames
               delaysMs:animation->delaysMs
              loopCount:animation->loopCount];
  // And drive it, which the view deliberately does not do for itself; see
  // rnStartImageAnimation. Idempotent, so the re-apply on every mutation costs
  // nothing.
  [view rnStartImageAnimation];
}

RnAppKitImageFit toImageFit(ImageResizeMode mode) {
  switch (mode) {
    case ImageResizeMode::Contain:
      return RnAppKitImageFitContain;
    case ImageResizeMode::Stretch:
      return RnAppKitImageFitStretch;
    case ImageResizeMode::Center:
    case ImageResizeMode::None:
      return RnAppKitImageFitCenter;
    case ImageResizeMode::Repeat:
      return RnAppKitImageFitRepeat;
    case ImageResizeMode::Cover:
      break;
  }
  return RnAppKitImageFitCover;
}

} // namespace

// React Native's cxx ImageManager is a stub that never produces an
// ImageResponse, so nothing arrives through ImageState. The URI is read off the
// props and loaded here instead, which is also how Android does it.
void AppKitMountingManager::applyImage(RnAppKitView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr) {
    return;
  }

  // Two components draw an image here: React Native's <Image> and expo-image's
  // view, which is an ordinary Fabric component with its own props (see
  // core/ExpoImageComponent.h). Everything past reading the source and the fit
  // off the props is identical, and the only other difference is which emitter
  // reports the result, because the two carry different payloads.
  const std::string_view componentName(shadowView.componentName);
  const bool isExpoImage = componentName == facebook::react::ExpoImageComponentName;
  if (componentName != "Image" && !isExpoImage) {
    return;
  }

  RnAppKitImageFit fit = RnAppKitImageFitCover;
  // Qualified: Carbon's headers put a CGImageSource in scope, and an
  // unqualified ImageSource resolves to that one.
  facebook::react::ImageSource source{};
  facebook::react::SharedColor tint{};
  // blurRadius alongside the tint, captured the same way: each branch below
  // knows its own props type, and both props are applied together after them.
  // expo-image has no blur prop, so that branch leaves this at zero.
  CGFloat blurRadius = 0;

  if (isExpoImage) {
    const auto props = std::dynamic_pointer_cast<const facebook::react::ExpoImageProps>(shadowView.props);
    if (props == nullptr) {
      return;
    }
    fit = toImageFit(props->contentFit);
    if (!props->sources.empty()) {
      source = props->sources.front();
    }
    tint = props->tintColor;
  } else {
    const auto props = std::dynamic_pointer_cast<const ImageProps>(shadowView.props);
    if (props == nullptr) {
      return;
    }
    fit = toImageFit(props->resizeMode);
    if (!props->sources.empty()) {
      source = props->sources.front();
    }
    // React Native's is optional where expo-image's is a plain SharedColor;
    // both spell "no tint" as a falsy SharedColor once unwrapped.
    if (props->tintColor) {
      tint = *props->tintColor;
    }
    blurRadius = (CGFloat)props->blurRadius;
  }

  const std::string uri = source.uri;
  const Tag tag = shadowView.tag;

  // Applied before the load rather than in its callback: the tint and the blur
  // are props where the image is a loader's answer, and either can arrive first.
  if (RnAppKitView *tinted = viewForTag(tag); tinted != nil) {
    if (tint) {
      const ColorComponents components = colorComponentsFromColor(tint);
      [tinted setRnImageTint:[NSColor colorWithSRGBRed:components.red
                                                 green:components.green
                                                  blue:components.blue
                                                 alpha:components.alpha]];
    } else {
      [tinted setRnImageTint:nil];
    }
    [tinted setRnImageBlur:blurRadius];
  }

  // A mutation that changed only layout must not restart the load, or an
  // <Image> would flicker every time its parent resized. Re-requesting the same
  // URI is cheap -- the loader answers from its cache on this thread -- and it
  // reapplies the fit, which is the only thing that can have changed.
  const auto known = imageUris_.find(tag);
  if (known != imageUris_.end() && known->second == uri) {
    if (!uri.empty()) {
      imageLoader_->load(uri, [this, tag, fit, uri](CGImageRef image, const std::string &) {
        if (RnAppKitView *target = viewForTag(tag); target != nil) {
          [target setRnImage:image fit:fit];
          applyImageAnimation(imageLoader_.get(), target, uri);
        }
      });
    }
    return;
  }

  imageUris_[tag] = uri;

  if (uri.empty()) {
    [view setRnImage:nullptr fit:fit];
    applyImageAnimation(imageLoader_.get(), view, uri);
    return;
  }

  // Always, rather than only when `shouldNotifyLoadEvents` is set.
  //
  // That prop is Android's signal, and it never arrives here.
  // `Image.android.js` sets it -- which is the Image.js this platform resolves
  // to -- but `ImageViewNativeComponent`'s view config branches on
  // `Platform.OS === 'android'`, and a platform that is neither takes the iOS
  // branch, whose `validAttributes` has no `shouldNotifyLoadEvents` in it. So
  // the prop is filtered out before it reaches C++ and onLoad/onError never
  // fire, silently, on both desktops.
  //
  // iOS does not use the prop at all: RCTImageComponentView emits load events
  // unconditionally and lets the emitter be the thing that knows whether
  // anybody is listening. Doing the same is both correct and cheaper than
  // forking a two-hundred-line view config to change one ternary -- and the
  // emitter lookup below already returns null when nothing is listening.
  if (isExpoImage) {
    if (auto emitter =
            std::dynamic_pointer_cast<const facebook::react::ExpoImageEventEmitter>(eventEmitterForTag(tag))) {
      emitter->onLoadStart();
    }
  } else if (auto emitter =
                 std::dynamic_pointer_cast<const ImageEventEmitter>(eventEmitterForTag(tag))) {
    emitter->onLoadStart();
  }

  imageLoader_->load(uri, [this, tag, fit, source, isExpoImage](CGImageRef image,
                                                              const std::string &error) {
    // The view may have been deleted while the image was in flight, which is
    // why this looks the tag up again rather than capturing the view.
    if (RnAppKitView *target = viewForTag(tag); target != nil) {
      [target setRnImage:image fit:fit];
      applyImageAnimation(imageLoader_.get(), target, source.uri);
    }

    if (image == nullptr) {
      LOG(WARNING) << "image failed to load: " << source.uri << " (" << error << ")";
    }

    if (isExpoImage) {
      auto emitter =
          std::dynamic_pointer_cast<const facebook::react::ExpoImageEventEmitter>(eventEmitterForTag(tag));
      if (emitter == nullptr) {
        return;
      }
      if (image != nullptr) {
        // The pixel dimensions, which is what expo-image's onLoad reports and
        // what an app sizing itself to an image reads.
        emitter->onLoad(source,
                        static_cast<double>(CGImageGetWidth(image)),
                        static_cast<double>(CGImageGetHeight(image)));
      } else {
        emitter->onError(error);
      }
      return;
    }

    auto emitter = std::dynamic_pointer_cast<const ImageEventEmitter>(eventEmitterForTag(tag));
    if (emitter == nullptr) {
      return;
    }
    if (image != nullptr) {
      emitter->onLoad(source);
    } else {
      emitter->onError(facebook::react::ImageErrorInfo{.error = error});
    }
    emitter->onLoadEnd();
  });
}

void AppKitMountingManager::updateView(RnAppKitView *view, const ShadowView &shadowView) {
  applyProps(view, shadowView);
  applyText(view, shadowView);
  applyImage(view, shadowView);
  // The peer first, then accessibility. A <TextInput>'s label belongs on its
  // peer and applyAccessibility can only put it there if the peer exists --
  // and on the mount that creates it, in this order it does. The GTK side
  // needed the same swap for the same reason.
  applyTextInput(view, shadowView);
  applyAccessibility(view, shadowView);
#ifdef BASALT_HAS_SKIA
  // After applyProps, which is what puts nativeID on the view -- a canvas
  // registers itself by that id and cannot be attached before it is there. And
  // before applyLayoutMetrics only incidentally: the size it reads comes from the
  // shadow view rather than from the frame this is about to set, so the order
  // does not matter here the way it does for a <TextInput>'s peer.
  applySkiaCanvas(view, shadowView);
#endif
  applyLayoutMetrics(view, shadowView);
  // Last: the scroll manager clamps its offset against the frame it was just
  // given, and iOS documents the same ordering requirement -- layout before
  // state, or the offset is clamped against a stale size.
  applyScrollView(view, shadowView);
  // After layout for the same reason the scroll view is: a control is centred
  // in the frame it was just given, and a <Modal> commits that frame's size
  // back into its own state.
  applyControls(view, shadowView);
}


// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

void AppKitMountingManager::setHighlights(RnAppKitView *view,
                                         const std::vector<basalt::Highlight> &highlights) {
  // Flattened into the arrays RnAppKitView takes, because that file has no
  // React Native in it and is not about to start.
  std::vector<float> values;
  std::vector<bool> filled;
  values.reserve(highlights.size() * 8);
  filled.reserve(highlights.size());
  for (const basalt::Highlight &highlight : highlights) {
    values.push_back(highlight.x);
    values.push_back(highlight.y);
    values.push_back(highlight.width);
    values.push_back(highlight.height);
    values.insert(values.end(), std::begin(highlight.color), std::end(highlight.color));
    filled.push_back(highlight.filled);
  }
  // `std::vector<bool>` has no `data()`, so the flags are copied into something
  // that does. A handful of rectangles, once a frame at worst.
  std::vector<char> flags(filled.begin(), filled.end());
  [view setRnHighlights:values.empty() ? nullptr : values.data()
                 filled:flags.empty() ? nullptr : reinterpret_cast<const bool *>(flags.data())
                  count:static_cast<NSInteger>(highlights.size())];
}

void AppKitMountingManager::pressedView(Tag tag) {
  RnAppKitView *view = viewForTag(tag);
  if (view == nil || view.rnControlKind != RnAppKitControlSwitch) {
    return;
  }
  const auto known = switchValues_.find(tag);
  if (known == switchValues_.end()) {
    return;
  }
  if ([view.rnControl isKindOfClass:[NSSwitch class]] && !((NSSwitch *)view.rnControl).enabled) {
    return;
  }

  // Told, and then left alone. React Native's <Switch> is a controlled
  // component: the app's `value` prop is the only thing that moves it, and a
  // switch that flipped itself would show a state its props do not agree with
  // -- which is exactly what an app that ignores onValueChange is supposed to
  // look like. The NSSwitch never moved, because it never saw the click.
  basalt::emitSwitchChange(eventEmitterForTag(tag), tag, !known->second);
}

void AppKitMountingManager::applyControlPeer(RnAppKitView *view, const basalt::ControlState &state) {
  const RnAppKitControlKind kind =
      state.kind == basalt::ControlKind::Switch ? RnAppKitControlSwitch : RnAppKitControlSpinner;

  // A view never changes which control it is -- React remounts rather than
  // turning a switch into a spinner -- but a stale NSSwitch inside a spinner's
  // frame is the kind of thing only a screenshot would show.
  if (view.rnControl != nil && view.rnControlKind != kind) {
    [view.rnControl removeFromSuperview];
    view.rnControl = nil;
  }

  const Tag tag = static_cast<Tag>(view.rnTag);
  NSView *control = view.rnControl;
  if (control == nil) {
    if (kind == RnAppKitControlSwitch) {
      control = [[RnAppKitSwitch alloc] initWithFrame:NSZeroRect];
    } else {
      NSProgressIndicator *spinner = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
      spinner.style = NSProgressIndicatorStyleSpinning;
      spinner.indeterminate = YES;
      spinner.wantsLayer = YES;
      control = spinner;
    }
    view.rnControl = control;
    view.rnControlKind = kind;
    [view addSubview:control];
  }

  if (kind == RnAppKitControlSwitch) {
    switchValues_[tag] = state.on;
    NSSwitch *toggle = (NSSwitch *)control;
    toggle.state = state.on ? NSControlStateValueOn : NSControlStateValueOff;
    toggle.enabled = !state.disabled;
  } else {
    NSProgressIndicator *spinner = (NSProgressIndicator *)control;
    spinner.controlSize = state.large ? NSControlSizeRegular : NSControlSizeSmall;
    // `displayedWhenStopped` is AppKit's spelling of `hidesWhenStopped`, and
    // React Native's default for it is the same as AppKit's.
    spinner.displayedWhenStopped = !state.hidesWhenStopped;
    if (state.on) {
      [spinner startAnimation:nil];
    } else {
      [spinner stopAnimation:nil];
    }

    // The tint. NSProgressIndicator has no colour property at all -- it draws
    // in the system's -- so the only way through is a Core Image filter over
    // its layer, which is what every macOS app that tints one does. A switch
    // gets no equivalent: NSSwitch follows the system accent colour and
    // exposes nothing per instance, so `trackColor` and `thumbColor` are
    // honoured on GTK and Win32 and not here. The tree dump does not print
    // colours, so this is a difference in pixels rather than in behaviour.
    if (state.hasForeground) {
      CIFilter *filter = [CIFilter filterWithName:@"CIFalseColor"];
      [filter setDefaults];
      CIColor *tint = [CIColor colorWithRed:state.foreground[0]
                                      green:state.foreground[1]
                                       blue:state.foreground[2]
                                      alpha:state.foreground[3]];
      [filter setValue:tint forKey:@"inputColor0"];
      [filter setValue:tint forKey:@"inputColor1"];
      spinner.contentFilters = @[filter];
    } else {
      spinner.contentFilters = @[];
    }
  }

  // Centred at its natural size rather than filling the frame: a switch
  // stretched to whatever box the app gave it is a rectangle with a circle in
  // it, and neither React Native nor AppKit draws one that way.
  const NSSize natural = control.intrinsicContentSize;
  const NSRect bounds = view.bounds;
  const CGFloat width = std::min(natural.width, bounds.size.width);
  const CGFloat height = std::min(natural.height, bounds.size.height);
  control.frame = NSMakeRect(std::round((bounds.size.width - width) / 2.0),
                             std::round((bounds.size.height - height) / 2.0),
                             width,
                             height);

  const std::string described = basalt::describeControl(state);
  view.rnControlDescription = described.empty()
      ? nil
      : [NSString stringWithUTF8String:described.c_str()];
}

void AppKitMountingManager::applyTextInput(RnAppKitView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "TextInput") {
    return;
  }
  textInputs_.update(view, shadowView);
}

void AppKitMountingManager::applyScrollView(RnAppKitView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "ScrollView") {
    return;
  }
  scrollViews_.update(view, shadowView);
}

// ---------------------------------------------------------------------------
// Applying a ShadowView to a view
// ---------------------------------------------------------------------------

// A <Paragraph> carries its text in state, not props: ParagraphShadowNode
// resolves the whole <Text> subtree into one AttributedString and commits it as
// ParagraphState, which is why nothing here walks child shadow nodes.
void AppKitMountingManager::applyText(RnAppKitView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "Paragraph") {
    return;
  }

  const auto state =
      std::dynamic_pointer_cast<const facebook::react::ConcreteState<ParagraphState>>(shadowView.state);
  if (state == nullptr) {
    return;
  }

  const auto &data = state->getData();
  // Built through the same function the measurement seam uses, which is what
  // makes the painted lines break where the measured ones did. The width is not
  // baked in -- the view draws at whatever size Yoga gave it.
  RnTextLayout *layout =
      basalt::buildTextLayout(data.attributedString, data.paragraphAttributes);

  // The paragraph's text shadow, resolved in core/TextShadows.h: the props are
  // per fragment and one Core Text frame draws them all, so the first fragment
  // that asks for a shadow decides it. The standard deviation crosses unchanged,
  // `CGContextSetShadowWithColor`'s blur being one, where the GTK side doubles
  // it for GSK.
  // The paragraph's writing direction, for the dump: the paragraph style the
  // layout carries is what actually decides it.
  const char *const direction =
      basalt::writingDirectionName(basalt::writingDirection(data.attributedString));
  view.rnWritingDirection = direction != nullptr ? @(direction) : nil;

  if (const auto shadow = basalt::textShadow(data.attributedString)) {
    layout.shadowOffset = CGSizeMake(shadow->dx, shadow->dy);
    layout.shadowStandardDeviation = shadow->standardDeviation;
    layout.shadowColor = [NSColor colorWithSRGBRed:shadow->red
                                             green:shadow->green
                                              blue:shadow->blue
                                             alpha:shadow->alpha];
  }

  [view setRnTextLayout:layout];
}

void AppKitMountingManager::applyProps(RnAppKitView *view, const ShadowView &shadowView) {
  const auto props = std::dynamic_pointer_cast<const ViewProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  if (props->backgroundColor) {
    const ColorComponents components = colorComponentsFromColor(props->backgroundColor);
    [view setRnBackgroundColorRed:components.red
                            green:components.green
                             blue:components.blue
                            alpha:components.alpha
                         hasColor:YES];
  } else {
    [view setRnBackgroundColorRed:0 green:0 blue:0 alpha:0 hasColor:NO];
  }

  [view setRnOpacity:props->opacity];

  // overflow: 'hidden'. React Native's default is 'visible'.
  [view setRnClipsChildren:props->getClipsContentToBounds()];

  // Radii depend on the frame -- percentage radii, and the clamping that stops
  // opposite corners overlapping -- so they are resolved against the layout
  // metrics rather than read raw.
  const auto borders = props->resolveBorderMetrics(shadowView.layoutMetrics);

  // All four corners, each with its own horizontal and vertical radius. The
  // view layer keeps a single circular set on CALayer's own cornerRadius and
  // puts anything else on a mask layer; see setRnBorderRadii:.
  const CGFloat radii[8] = {
      (CGFloat)borders.borderRadii.topLeft.horizontal,
      (CGFloat)borders.borderRadii.topLeft.vertical,
      (CGFloat)borders.borderRadii.topRight.horizontal,
      (CGFloat)borders.borderRadii.topRight.vertical,
      (CGFloat)borders.borderRadii.bottomRight.horizontal,
      (CGFloat)borders.borderRadii.bottomRight.vertical,
      (CGFloat)borders.borderRadii.bottomLeft.horizontal,
      (CGFloat)borders.borderRadii.bottomLeft.vertical,
  };
  [view setRnBorderRadii:radii];

  // Widths and colours, top/right/bottom/left -- the order CSS names them and
  // the order the GTK side passes them in, so the two dumps line up.
  const CGFloat widths[4] = {
      (CGFloat)borders.borderWidths.top,
      (CGFloat)borders.borderWidths.right,
      (CGFloat)borders.borderWidths.bottom,
      (CGFloat)borders.borderWidths.left,
  };
  const auto edgeColor = [](const auto &color, CGFloat *out) {
    if (!color) {
      // Transparent, not black -- an edge with a width and no colour paints
      // nothing. The GTK side makes exactly the same call, and the two have to
      // agree or the dumps diverge on a view neither actually draws a border
      // for.
      out[0] = out[1] = out[2] = out[3] = 0;
      return;
    }
    const auto components = facebook::react::colorComponentsFromColor(*color);
    out[0] = (CGFloat)components.red;
    out[1] = (CGFloat)components.green;
    out[2] = (CGFloat)components.blue;
    out[3] = (CGFloat)components.alpha;
  };
  CGFloat colors[16];
  edgeColor(borders.borderColors.top, &colors[0]);
  edgeColor(borders.borderColors.right, &colors[4]);
  edgeColor(borders.borderColors.bottom, &colors[8]);
  edgeColor(borders.borderColors.left, &colors[12]);
  [view setRnBorderWidths:widths colors:colors];

  // resolveTransform folds in transformOrigin, but only when one was set: the
  // default anchor is the view's centre, which is what a layer-backed NSView
  // already uses.
  const auto transform = props->resolveTransform(shadowView.layoutMetrics);
  if (transform == facebook::react::Transform::Identity()) {
    [view setRnTransform:nullptr];
  } else {
    [view setRnTransform:transform.matrix.data()];
  }

  // `backfaceVisibility: 'hidden'`, which stops the back of a card being drawn
  // mirrored halfway through a flip. Whether a transform has turned away is
  // core/Backface.h's to decide, so all three hide the same face.
  [view setRnHidesBackFace:props->backfaceVisibility ==
                           facebook::react::BackfaceVisibility::Hidden];

  // The app's nativeID, which marks a view for a behaviour React Native has
  // no prop for -- a drop target today. See core/DragAndDrop.h, and
  // core/TitleBarRegions.h for the older use of the same idea.
  // Which view this nativeID names, which is what `accessibilityLabelledBy`
  // needs: that prop names other views by their nativeID and nothing else here
  // can look one up. See core/LabelRegistry.h.
  labels_.setNativeId(shadowView.tag, props->nativeId);
  // `experimental_accessibilityOrder`, which names children by their nativeID
  // the way `accessibilityLabelledBy` names labels, and is resolved by the same
  // registry for the same reason: a view can be named before it mounts.
  labels_.setAccessibilityOrder(shadowView.tag, props->accessibilityOrder);

  // `testID`, which is not `nativeID`: one is how an app names a view to
  // itself, the other is how something outside the process finds it.
  view.rnTestId = props->testId.empty()
      ? nil
      : [NSString stringWithUTF8String:props->testId.c_str()];

  // `accessibilityViewIsModal`, which keeps VoiceOver inside a <Modal>.
  view.rnAccessibleModal = props->accessibilityViewIsModal ? YES : NO;

  view.rnNativeId = props->nativeId.empty()
      ? nil
      : [NSString stringWithUTF8String:props->nativeId.c_str()];

  // Painting and hit testing only; see setRnZIndex:.
  [view setRnZIndex:(NSInteger)props->zIndex.value_or(0)];

  // Hit testing only. RnAppKitHitTest reads it; nothing about drawing does.
  switch (props->pointerEvents) {
    case facebook::react::PointerEventsMode::None:
      view.rnPointerEvents = RnAppKitPointerEventsNone;
      break;
    case facebook::react::PointerEventsMode::BoxNone:
      view.rnPointerEvents = RnAppKitPointerEventsBoxNone;
      break;
    case facebook::react::PointerEventsMode::BoxOnly:
      view.rnPointerEvents = RnAppKitPointerEventsBoxOnly;
      break;
    case facebook::react::PointerEventsMode::Auto:
      view.rnPointerEvents = RnAppKitPointerEventsAuto;
      break;
  }

  // backgroundImage: linear and radial gradients. The angle or the ending shape,
  // the box size and CSS's colour-stop fixup are resolved here through
  // core/Gradients.h, shared with the GTK host, and the view is handed points
  // and radii and a list of stops.
  //
  // One list rather than two, because `background-image` is one list and the
  // first in it is the one on top: two lists would lose the order between a
  // radial gradient and a linear one.
  //
  // Against this mutation's own frame, and applyProps runs on every update, so a
  // resized view gets a resized gradient without the view layer knowing anything
  // about angles or corners.
  {
    const auto &metrics = shadowView.layoutMetrics;
    // The two areas CSS gives a background: the painting area is the border box,
    // which it is clipped to, and the positioning area is the padding box, which
    // sizes and positions it. getPaddingFrame's origin is already relative to
    // the view, being the border widths.
    const basalt::BackgroundArea painting{
        0.0F, 0.0F, (float)metrics.frame.size.width, (float)metrics.frame.size.height};
    const auto paddingFrame = metrics.getPaddingFrame();
    const basalt::BackgroundArea positioning{(float)paddingFrame.origin.x,
                                             (float)paddingFrame.origin.y,
                                             (float)paddingFrame.size.width,
                                             (float)paddingFrame.size.height};

    std::vector<RnAppKitGradient> gradients;
    std::vector<std::vector<RnAppKitGradientStop>> stops;
    gradients.reserve(props->backgroundImage.size());
    stops.reserve(props->backgroundImage.size());
    for (size_t index = 0; index < props->backgroundImage.size(); index++) {
      const auto &image = props->backgroundImage[index];
      // Each list is indexed modulo its own length, which is CSS's rule for a
      // list shorter than the image list and is what iOS does.
      const basalt::BackgroundLayer layer = basalt::resolveBackgroundLayer(
          positioning,
          painting,
          basalt::backgroundSizeAt(props->backgroundSize, index),
          basalt::backgroundPositionAt(props->backgroundPosition, index),
          basalt::backgroundRepeatAt(props->backgroundRepeat, index));
      if (layer.empty()) {
        continue;
      }
      // The gradient is resolved against the image's size and then offset to
      // where the image goes: resolving against the view would ignore
      // `backgroundSize` even with the rectangle right.
      const auto width = layer.width;
      const auto height = layer.height;

      RnAppKitGradient converted{};
      converted.area = CGRectMake(layer.x, layer.y, layer.width, layer.height);
      converted.tile = CGRectMake(layer.tileX, layer.tileY, layer.tileWidth, layer.tileHeight);
      converted.repeats = layer.repeats();
      const std::vector<facebook::react::ColorStop> *colorStops = nullptr;
      float rayLength = 0.0F;

      if (std::holds_alternative<facebook::react::LinearGradient>(image)) {
        const auto &gradient = std::get<facebook::react::LinearGradient>(image);
        const basalt::GradientLine line = basalt::linearGradientLine(gradient, width, height);
        converted.kind = RnAppKitGradientKindLinear;
        converted.start = CGPointMake(line.startX + layer.x, line.startY + layer.y);
        converted.end = CGPointMake(line.endX + layer.x, line.endY + layer.y);
        colorStops = &gradient.colorStops;
        rayLength = line.length();
      } else {
        const auto &gradient = std::get<facebook::react::RadialGradient>(image);
        const basalt::GradientEllipse shape =
            basalt::radialGradientEllipse(gradient, width, height);
        converted.kind = RnAppKitGradientKindRadial;
        converted.center = CGPointMake(shape.centerX + layer.x, shape.centerY + layer.y);
        converted.radiusX = shape.radiusX;
        converted.radiusY = shape.radiusY;
        colorStops = &gradient.colorStops;
        rayLength = shape.rayLength();
      }

      const auto resolved = basalt::resolveGradientStops(*colorStops, rayLength);
      if (resolved.empty()) {
        continue;
      }
      std::vector<RnAppKitGradientStop> stopList;
      stopList.reserve(resolved.size());
      for (const auto &stop : resolved) {
        RnAppKitGradientStop one{};
        one.offset = stop.offset;
        one.color[0] = stop.red;
        one.color[1] = stop.green;
        one.color[2] = stop.blue;
        one.color[3] = stop.alpha;
        stopList.push_back(one);
      }
      // Reserved above, so pushing cannot reallocate and the pointer handed over
      // below stays good for this block. The view copies what it is given.
      stops.push_back(std::move(stopList));
      converted.stops = stops.back().data();
      converted.stopCount = (NSInteger)stops.back().size();
      gradients.push_back(converted);
    }
    [view setRnGradients:gradients.data() count:(NSInteger)gradients.size()];
  }

  // boxShadow. React Native's BoxShadow carries the six CSS fields and the view
  // layer takes the same six, so nothing is converted here: it decides what a
  // shadow is made of on this platform, and this decides nothing.
  //
  // The list also carries the older iOS shadow props, converted in
  // core/LegacyShadow.h: one mechanism from here down, so a view with
  // `shadowOpacity` and a view with `boxShadow` take the same path.
  {
    const std::vector<facebook::react::BoxShadow> all = basalt::allShadows(*props);
    std::vector<RnAppKitBoxShadow> shadows;
    shadows.reserve(all.size());
    for (const auto &shadow : all) {
      RnAppKitBoxShadow converted{};
      converted.dx = (CGFloat)shadow.offsetX;
      converted.dy = (CGFloat)shadow.offsetY;
      converted.blur = (CGFloat)shadow.blurRadius;
      converted.spread = (CGFloat)shadow.spreadDistance;
      converted.inset = shadow.inset;
      if (shadow.color) {
        const ColorComponents components = colorComponentsFromColor(shadow.color);
        converted.color[0] = components.red;
        converted.color[1] = components.green;
        converted.color[2] = components.blue;
        converted.color[3] = components.alpha;
      }
      shadows.push_back(converted);
    }
    [view setRnBoxShadows:shadows.data() count:(NSInteger)shadows.size()];
  }

  // The outline: CSS's, drawn outside the box and taking no layout space, so
  // nothing about it touches the frame. One width, one colour, one offset and one
  // style for the whole ring, unlike the border's four of each.
  {
    CGFloat colour[4] = {0, 0, 0, 0};
    if (props->outlineColor) {
      const ColorComponents components = colorComponentsFromColor(props->outlineColor);
      colour[0] = components.red;
      colour[1] = components.green;
      colour[2] = components.blue;
      colour[3] = components.alpha;
    }
    RnAppKitBorderStyle style = RnAppKitBorderStyleSolid;
    switch (props->outlineStyle) {
      case facebook::react::OutlineStyle::Dotted:
        style = RnAppKitBorderStyleDotted;
        break;
      case facebook::react::OutlineStyle::Dashed:
        style = RnAppKitBorderStyleDashed;
        break;
      case facebook::react::OutlineStyle::Solid:
        break;
    }
    [view setRnOutlineWidth:(CGFloat)props->outlineWidth
                     offset:(CGFloat)props->outlineOffset
                      color:colour
                      style:style];
  }

  // filter. The arithmetic is core/Filters.h's and is shared with the GTK host:
  // the list of CSS functions comes to one colour matrix, one blur and one
  // opacity, and the view layer turns the first two into Core Image filters.
  //
  // A dropShadow() in the list is reported rather than applied -- it is the one
  // function that does not commute with the others. Logged once per view that
  // asks, because silence here looks like a working filter.
  {
    const basalt::ResolvedFilters resolved = basalt::resolveFilters(props->filter);
    if (resolved.empty()) {
      [view setRnFilters:nullptr];
    } else {
      RnAppKitFilters filters{};
      filters.hasMatrix = resolved.hasMatrix;
      for (int i = 0; i < 16; i++) {
        filters.matrix[i] = resolved.matrix.m[i];
      }
      for (int i = 0; i < 4; i++) {
        filters.offset[i] = resolved.matrix.offset[i];
      }
      filters.blurRadius = resolved.blurRadius;
      filters.opacity = resolved.opacity;

      // `dropShadow()`, which this host draws as the layer's own shadow. The
      // standard deviation crosses unchanged, `CALayer.shadowRadius` being one,
      // where the GTK side doubles it for GSK.
      std::vector<RnAppKitFilterShadow> shadows;
      shadows.reserve(resolved.dropShadows.size());
      for (const auto &shadow : resolved.dropShadows) {
        RnAppKitFilterShadow converted{};
        converted.dx = (CGFloat)shadow.dx;
        converted.dy = (CGFloat)shadow.dy;
        converted.standardDeviation = (CGFloat)shadow.standardDeviation;
        converted.color[0] = shadow.red;
        converted.color[1] = shadow.green;
        converted.color[2] = shadow.blue;
        converted.color[3] = shadow.alpha;
        shadows.push_back(converted);
      }
      filters.shadows = shadows.data();
      filters.shadowCount = (NSInteger)shadows.size();
      // The view copies what it keeps, so the vector may go out of scope.
      [view setRnFilters:&filters];

      if (resolved.dropShadows.size() > 1) {
        LOG(WARNING) << "filter: only the first dropShadow() is drawn on this "
                     << "platform; a CALayer has one shadow";
      }
    }
  }

  // hitSlop, which grows what a press can land on without moving a pixel.
  // RnAppKitHitTest is the only reader; see -rnHitArea.
  {
    const CGFloat insets[4] = {
        (CGFloat)props->hitSlop.top,
        (CGFloat)props->hitSlop.right,
        (CGFloat)props->hitSlop.bottom,
        (CGFloat)props->hitSlop.left,
    };
    [view setRnHitSlop:insets];
  }

  // The `cursor` style property, as a CSS keyword. core/CursorNames.h is shared
  // with the GTK host, which hands the same keyword to GDK; what macOS has no
  // cursor for is decided in the view layer, beside the NSCursors.
  {
    const char *name = basalt::cursorName(props->cursor);
    [view setRnCursorName:name != nullptr ? [NSString stringWithUTF8String:name] : nil];
  }

  // `mixBlendMode`, also as a CSS keyword and also shared: Core Image has a
  // blend-mode filter per CSS mode, so the view layer maps the keyword onto one
  // and Core Animation does the blending.
  {
    const char *name = basalt::blendModeName(props->mixBlendMode);
    [view setRnBlendModeName:name != nullptr ? [NSString stringWithUTF8String:name] : nil];
  }

  // borderStyle. React Native carries one per side and a stroked path carries
  // one dash pattern, so the first side that asks for something other than solid
  // decides the whole outline, exactly as on GTK. A border with different styles
  // per side is rare enough to be worth that; see backlog/correctness.md.
  //
  // From the *resolved* metrics, like the widths and the colours: the raw
  // `props->borderStyles` is a cascade of optionals with a slot per spelling, and
  // `borderStyle: 'dashed'` sets the `all` slot rather than the four sides. The
  // GTK side read the sides and so drew every dashed border solid.
  {
    const auto &styles =
        props->resolveBorderMetrics(shadowView.layoutMetrics).borderStyles;
    RnAppKitBorderStyle style = RnAppKitBorderStyleSolid;
    const facebook::react::BorderStyle sides[4] = {
        styles.top, styles.right, styles.bottom, styles.left};
    for (const auto &side : sides) {
      if (side == facebook::react::BorderStyle::Dotted) {
        style = RnAppKitBorderStyleDotted;
        break;
      }
      if (side == facebook::react::BorderStyle::Dashed) {
        style = RnAppKitBorderStyleDashed;
        break;
      }
    }
    [view setRnBorderStyle:style];
  }
}

namespace {

RnAppKitAccessibleFlag toFlag(bool value) {
  return value ? RnAppKitAccessibleTrue : RnAppKitAccessibleFalse;
}

// The role React Native effectively means for this view: what the app asked
// for, or what the component implies.
//
// A <Text> is a label and an <Image> is an image whether or not the app said
// so, which is what makes an ordinary screen navigable without every developer
// having annotated it. The GTK side infers the same two, at construction time,
// because its role is construct-only.
std::string effectiveRole(const ShadowView &shadowView) {
  if (const auto props =
          std::dynamic_pointer_cast<const facebook::react::AccessibilityProps>(shadowView.props)) {
    if (!props->accessibilityRole.empty()) {
      return props->accessibilityRole;
    }
  }
  if (shadowView.componentName != nullptr) {
    const std::string_view name(shadowView.componentName);
    if (name == "Paragraph") {
      return "text";
    }
    if (name == "Image") {
      return "image";
    }
  }
  return {};
}

} // namespace

// Accessibility, as VoiceOver sees it.
void AppKitMountingManager::applyAccessibility(RnAppKitView *view, const ShadowView &shadowView) {
  const auto props =
      std::dynamic_pointer_cast<const facebook::react::AccessibilityProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  const std::string role = effectiveRole(shadowView);
  [view setRnAccessibleRole:role.empty() ? nil : [NSString stringWithUTF8String:role.c_str()]];

  // `accessibilityLiveRegion`: a status message to read out when it changes.
  // Recorded here and acted on after the transaction, the text being whatever the
  // region says once every mutation in the batch has landed.
  liveRegions_.setPoliteness(shadowView.tag, props->accessibilityLiveRegion);
  if (props->accessibilityLiveRegion == facebook::react::AccessibilityLiveRegion::None) {
    liveRegionLabels_.erase(shadowView.tag);
  } else {
    liveRegionLabels_[shadowView.tag] = props->accessibilityLabel;
  }

  // `accessibilityLabelledBy`: other views, named by their nativeID, whose text
  // names this one. Only recorded here -- the resolution needs every view in the
  // transaction to have been seen, so it happens once at the end of the mount.
  labels_.setLabelledBy(shadowView.tag, props->accessibilityLabelledBy.value);

  // A label given in props wins. Falling back to a Paragraph's own text means a
  // plain <Text> announces itself without the app having to repeat the string
  // in an accessibilityLabel.
  std::string label = props->accessibilityLabel;
  if (label.empty() && shadowView.componentName != nullptr &&
      std::string_view(shadowView.componentName) == "Paragraph") {
    if (const auto state =
            std::dynamic_pointer_cast<const facebook::react::ConcreteState<ParagraphState>>(
                shadowView.state)) {
      label = state->getData().attributedString.getString();
    }
  }

  NSString *labelText = label.empty() ? nil : [NSString stringWithUTF8String:label.c_str()];
  NSString *hintText = props->accessibilityHint.empty()
      ? nil
      : [NSString stringWithUTF8String:props->accessibilityHint.c_str()];

  // A <TextInput>'s label belongs on its peer, not on this view.
  //
  // The peer is a real NSTextField and so is already an AXTextField in its own
  // right -- which is the element VoiceOver lands on. Left alone, the label
  // goes on the wrapper and the field announces itself as an unnamed "text
  // field", so a field the app carefully labelled is read out as if it had no
  // label at all. Checked with System Events: the group carried "your name"
  // and the AXTextField inside it carried nothing.
  //
  // The wrapper then stops being an element of its own, because two nested
  // elements for one control is a worse tree than one: a screen reader stops
  // twice and says the name once.
  if (NSView *peer = view.rnEditable) {
    peer.accessibilityLabel = labelText;
    if (hintText != nil) {
      peer.accessibilityHelp = hintText;
    }
    [view setRnAccessibleLabel:nil hint:nil];
    // Not `setRnAccessibleRole:@"none"`, which would be React Native's role
    // vocabulary and so would print in `describeTree` -- and GTK cannot answer
    // it, because a GtkAccessible role is construct-only. That made the two
    // hosts' trees disagree on this view and nothing else. Taking the wrapper
    // out of the accessibility tree directly says the same thing to VoiceOver
    // and nothing at all to the dump.
    view.accessibilityElement = NO;
    return;
  }

  [view setRnAccessibleLabel:labelText hint:hintText];

  if (props->accessibilityState.has_value()) {
    const auto &state = *props->accessibilityState;
    RnAppKitAccessibleFlag checked = RnAppKitAccessibleUnset;
    switch (state.checked) {
      case facebook::react::AccessibilityState::Checked:
        checked = RnAppKitAccessibleTrue;
        break;
      case facebook::react::AccessibilityState::Unchecked:
        checked = RnAppKitAccessibleFalse;
        break;
      case facebook::react::AccessibilityState::Mixed:
      case facebook::react::AccessibilityState::None:
        break;
    }
    [view setRnAccessibleStateDisabled:toFlag(state.disabled)
                               checked:checked
                              selected:toFlag(state.selected)
                              expanded:state.expanded.has_value()
                                           ? toFlag(*state.expanded)
                                           : RnAppKitAccessibleUnset
                                  busy:toFlag(state.busy)];
  } else {
    [view setRnAccessibleStateDisabled:RnAppKitAccessibleUnset
                               checked:RnAppKitAccessibleUnset
                              selected:RnAppKitAccessibleUnset
                              expanded:RnAppKitAccessibleUnset
                                  busy:RnAppKitAccessibleUnset];
  }

  // After the state, deliberately: AppKit carries both `checked` and
  // accessibilityValue in one property, and an explicit value is the app's own
  // word so it wins over one derived from a checkbox. See the setter.
  //
  // Each part passed as nil when absent, so a view that gave only `now` is not
  // announced as sitting at the bottom of a range it never mentioned.
  {
    const auto &value = props->accessibilityValue;
    [view setRnAccessibleValueMin:value.min.has_value() ? @(*value.min) : nil
                              max:value.max.has_value() ? @(*value.max) : nil
                              now:value.now.has_value() ? @(*value.now) : nil
                             text:value.text.has_value()
                                      ? [NSString stringWithUTF8String:value.text->c_str()]
                                      : nil];
  }

  // Keyboard focus. `accessible` is the signal because React Native's
  // `focusable` prop never reaches this platform -- see AppKitFocus.h -- and
  // because it is what <Pressable> sets on everything it renders. A hidden view
  // is not focusable whatever it says: Tab stopping on something nobody can see
  // is worse than Tab skipping it.
  // A control is focusable whether or not the app said `accessible`.
  //
  // React Native's `focusable` prop never reaches this platform -- ReactCommon
  // parses it only into Android's and tvOS's props -- so `accessible` is the
  // signal, and <Pressable> sets it. <Switch> does not: on a phone the native
  // control is focusable by being a control, and React Native never had to say
  // so. On a desktop a switch that Tab skips is simply broken, so being a
  // control is the signal here too.
  // Disabled is excluded, which is what every desktop does: Tab skips a control
  // that cannot be operated rather than stopping on one that does nothing.
  const basalt::ControlState control = basalt::controlStateOf(shadowView);
  const bool isControl = control.kind == basalt::ControlKind::Switch && !control.disabled;
  view.rnFocusable =
      (props->accessible || isControl) && !props->accessibilityElementsHidden ? YES : NO;

  // accessibilityElementsHidden hides the subtree; importantForAccessibility
  // NoHideDescendants is Android's spelling of the same idea.
  const bool hidden = props->accessibilityElementsHidden ||
      props->importantForAccessibility ==
          facebook::react::ImportantForAccessibility::NoHideDescendants;
  [view setRnAccessibleHidden:hidden ? YES : NO];
}

void AppKitMountingManager::applyLayoutMetrics(RnAppKitView *view, const ShadowView &shadowView) {
  const auto &frame = shadowView.layoutMetrics.frame;
  [view setRnFrameX:frame.origin.x
                  y:frame.origin.y
              width:frame.size.width
             height:frame.size.height];

  // display: 'none' keeps the node in the shadow tree but takes it out of
  // layout and painting.
  [view setRnHidden:shadowView.layoutMetrics.displayType ==
                            facebook::react::DisplayType::None
                        ? YES
                        : NO];

  // TODO(layout): pointScaleFactor, once a Retina backing store is involved.
}

} // namespace basalt
