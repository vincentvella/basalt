#include "Win32MountingManager.h"

#include "BackgroundLayers.h"
#include "BlendModes.h"
#include "CursorNames.h"
#include "Filters.h"
#include "Gradients.h"
#include "LegacyShadow.h"

#include "DirectWriteLayout.h"
#include "ImageBytes.h"
#include "PlatformServices.h"
#include "UIManagerAccess.h"

#ifdef BASALT_HAS_SKIA
#include "Win32SkiaPeer.h"
#endif

#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/text/ParagraphProps.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/uimanager/UIManager.h>

#include <glog/logging.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace basalt {

using facebook::react::ImageProps;
using facebook::react::ImageResizeMode;
using facebook::react::MountingTransaction;
using facebook::react::ParagraphState;
using facebook::react::ShadowView;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::ViewProps;
using win32::RnImageFit;
using win32::RnWin32View;

Win32MountingManager::Win32MountingManager()
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

Win32MountingManager::~Win32MountingManager() noexcept {
  // MountingWalk cannot do this from its own destructor: by the time a base
  // destructor runs, the platform half that knows how to free a view is already
  // gone. Its header says so, and every platform has to make this call.
  releaseAllViews();
}

// ---------------------------------------------------------------------------
// IMountingManager
// ---------------------------------------------------------------------------

void Win32MountingManager::executeMount(SurfaceId surfaceId, MountingTransaction &&transaction) {
  // This runs on the JS thread: Scheduler::uiManagerDidFinishTransaction queues
  // the mount via RuntimeScheduler::scheduleRenderingUpdate, which drains in the
  // event loop's "update the rendering" step with the jsi::Runtime live. A view
  // here is not thread-affine the way a GtkWidget or an NSView is -- it is a
  // plain C++ object -- but the *painting* is, and so is every HWND a later
  // <TextInput> will own. So nothing here may touch a view either.
  //
  // Always post, never run inline even when already on the UI thread. The queue
  // is FIFO, and running one transaction inline while another is already
  // queued would reorder them -- so this is about ordering rather than about
  // thread-safety, and it is why the condition is absent rather than merely
  // unnecessary.
  //
  // That is this project's choice rather than everyone's, and worth being exact
  // about: iOS takes the other side. RCTMountingManager::scheduleTransaction
  // runs `initiateTransaction` inline when `RCTIsMainQueue()`, and only
  // dispatches otherwise. It gets away with it because under Fabric the JS
  // thread is not the main thread, so the fast path is reached only when
  // everything is already on one thread and there is nothing to reorder against.
  // GTK and AppKit both queue unconditionally here, and this matches them.
  //
  // None of this changed with the New Architecture. What JSI removed is the
  // *bridge* -- the JSON serialisation and the asynchronous message queue
  // between JavaScript and native, which is why a TurboModule call is now a
  // direct C++ call and why `measure` can answer synchronously. Mounting was
  // never on that path: Fabric computes layout off the UI thread, commits to
  // the shadow tree, and the mutations still have to be applied where the
  // platform permits. Thread affinity is USER32's rule, not the bridge's.
  //
  // MountingTransaction is move-only, so it travels in a shared_ptr the lambda
  // can capture -- std::function requires its target to be copyable.
  //
  // `alive` and `epoch` are what stop a queued transaction being applied against
  // an instance that has since been destroyed, which a reload does on a thread of
  // its own. See MountingWalk::mountGuard.
  auto pending = std::make_shared<MountingTransaction>(std::move(transaction));
  postToUiThread([this, surfaceId, pending, alive = mountGuard(), epoch = mountEpoch()] {
    if (alive.expired() || mountEpoch() != epoch) {
      return;
    }
    applyTransaction(surfaceId, std::move(*pending));
  });
}

void Win32MountingManager::applyTransaction(SurfaceId surfaceId,
                                            MountingTransaction &&transaction) {
  // The walk itself is in core/MountingWalk.h and is shared with the other two
  // desktops; what is Windows about mounting is below, in the operations it
  // calls back into.
  applyMutations(transaction.getMutations());

  // Tell the UIManager the transaction is on screen. Anything registered as a
  // mount hook -- Reanimated's is the one that matters -- is waiting for this,
  // and without it an animated style is computed every frame, committed to the
  // shadow tree, and never resumed, so nothing moves. ReactCxxPlatform calls it
  // nowhere.
  // Refuses a surface the UIManager no longer has, which is what a reload's
  // teardown leaves behind. See core/UIManagerAccess.h.
  reportMountedSurface(surfaceId);

  // After the mutations, because a view can be labelled by one that mounts
  // after it: Fabric mounts in tree order, and the label of a field often
  // follows it.
  applyLabelRelations();

  // And after those, because an announcement is the last word on a
  // transaction: the text a live region says now is what every mutation in
  // this batch has left it saying.
  announceLiveRegions();

  // And ask the host to repaint, because on Windows nothing does that on its
  // own. See setOnDidMount. This also positions and shows the peers through
  // syncBounds, which is why the autoFocus flush comes after it rather than
  // before: a control is created without WS_VISIBLE.
  if (onDidMount_) {
    onDidMount_();
  }

  // Now that the tree is on screen, anything `autoFocus` asked for can be given
  // focus. See Win32TextInput.h.
  textInputs_.flushAutoFocus();
}

void Win32MountingManager::setOnDidMount(std::function<void()> onDidMount) {
  onDidMount_ = std::move(onDidMount);
}

void Win32MountingManager::dispatchCommand(const ShadowView &shadowView,
                                           const std::string &commandName,
                                           const folly::dynamic &args) {
  // Same trip, for the same reason, and posted onto the same queue so it stays
  // behind the transaction that created the view it names. A scrollTo that
  // arrived before its ScrollView was mounted would find no entry and be
  // dropped.
  const Tag tag = shadowView.tag;
  postToUiThread([this, tag, commandName, args] { applyCommand(tag, commandName, args); });
}

void Win32MountingManager::applyCommand(Tag tag,
                                        const std::string &commandName,
                                        const folly::dynamic &args) {
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
  // Dropped silently rather than logged. Every command the other two desktops
  // implement is now implemented here, so what reaches this line is a command
  // for a component this platform does not claim -- which is expected rather
  // than exceptional, and would otherwise be logged on every frame of a list.
}

facebook::react::ComponentRegistryFactory Win32MountingManager::getComponentRegistryFactory() {
  return facebook::react::getDefaultComponentRegistryFactory();
}

bool Win32MountingManager::hasComponent(const std::string &name) {
  // Only what actually mounts. Claiming more would be worse than admitting the
  // gap: the registry would build shadow nodes nothing can put on screen, and
  // the app would render blank rectangles instead of failing somewhere legible.
  //
  // Paragraph is the mountable half of <Text>; Text and RawText exist only in
  // the shadow tree, folded into the Paragraph's AttributedString.
  //
  // This list and ComponentRegistryWin32.cpp are two statements of one fact and
  // must agree.
  return name == "View" || name == "RootView" || name == "Paragraph" ||
      name == "ScrollView" || name == "Image" || name == "TextInput" ||
      name == "ActivityIndicatorView" || name == "Switch" || name == "ModalHostView" ||
      name == "PullToRefreshView" || name == "UnimplementedNativeView" ||
      name == "DebuggingOverlay";
}

void Win32MountingManager::setUIManager(
    std::weak_ptr<facebook::react::UIManager> uiManager) noexcept {
  setSharedUIManager(std::move(uiManager));
}

void Win32MountingManager::setSchedulerTaskExecutor(
    facebook::react::SchedulerTaskExecutor &&schedulerTaskExecutor) noexcept {
  // A null executor means `destroyReactInstance`: the Scheduler and the
  // SurfaceManager are about to go, and anything this already queued to the UI
  // thread would be applied against them. Non-null means a new instance, which
  // needs nothing from here.
  if (!schedulerTaskExecutor) {
    invalidatePendingMounts();
  }
}

// ---------------------------------------------------------------------------
// What MountingWalk asks of a platform
// ---------------------------------------------------------------------------

RnWin32View *Win32MountingManager::createView(const ShadowView &shadowView) {
  return new RnWin32View(shadowView.tag);
}

RnWin32View *Win32MountingManager::createRootView(Tag tag) {
  return new RnWin32View(tag);
}

void Win32MountingManager::destroyView(RnWin32View *view) {
  // The one operation that is genuinely different here. On GTK the registry
  // holds a g_object_ref_sink and this is an unref; under ARC it is nothing at
  // all, because erasing the map entry releases the last strong reference. A
  // plain C++ object has neither, so the registry's ownership is written out:
  // this is where a view stops existing.
  //
  // Safe against a view that is still parented, because ~RnWin32View detaches
  // itself -- though Fabric guarantees a Remove before every Delete, so that
  // should never be the path taken.
  delete view;
}

void Win32MountingManager::insertChild(RnWin32View *parent, RnWin32View *child, int index) {
  parent->insertChild(child, index);
}

void Win32MountingManager::removeChild(RnWin32View *parent, RnWin32View *child) {
  parent->removeChild(child);
}

void Win32MountingManager::applyLabelRelations() {
  if (labels_.empty()) {
    return;
  }
  for (const auto &change : labels_.changes()) {
    RnWin32View *view = viewForTag(change.tag);
    if (view == nullptr) {
      continue;
    }
    std::vector<RnWin32View *> resolved;
    resolved.reserve(change.labels.size());
    for (const Tag tag : change.labels) {
      if (RnWin32View *label = viewForTag(tag); label != nullptr) {
        resolved.push_back(label);
      }
    }
    view->setLabelledBy(std::move(resolved));
  }

  // `experimental_accessibilityOrder` comes out of the same registry and is
  // deliberately not applied here. UIA has no reading-order property: a client
  // walks the fragment tree, and the order is the order the tree is in. The
  // other two hosts have `aria-flowto` and `accessibilityChildren` to point at;
  // reordering the view tree itself to fake it would move what is painted.
  // Recorded in backlog/platform-windows.md, and the scenario skips here by
  // name with that reason.
}

void Win32MountingManager::announceLiveRegions() {
  if (liveRegions_.empty()) {
    return;
  }
  for (const Tag tag : liveRegions_.tags()) {
    RnWin32View *view = viewForTag(tag);
    if (view == nullptr) {
      continue;
    }
    // A label stands in for the text when the app set one: an icon-only status
    // has no paragraph of its own, and the label is what would be read. Both
    // other hosts make the same choice in the same order.
    const auto label = liveRegionLabels_.find(tag);
    std::string text = label != liveRegionLabels_.end() ? label->second : std::string();
    if (text.empty()) {
      text = view->collectText();
    }
    const auto politeness = liveRegions_.noticed(tag, text);
    if (!politeness.has_value()) {
      continue;
    }
    const bool assertive = *politeness == basalt::LiveRegionPoliteness::Assertive;
    view->announce(text, assertive);
    // Logged as well as announced: nothing in an automated run has a screen
    // reader attached, so this line is how an end-to-end test sees that it
    // happened. The same line the GTK host writes, read by the same scenario.
    LOG(INFO) << "announced" << (assertive ? " (assertive): " : ": ") << text;
  }
}

void Win32MountingManager::forgetTag(Tag tag) {
  scrollViews_.remove(tag);
  textInputs_.remove(tag);
  imageUris_.erase(tag);
  switchValues_.erase(tag);
  labels_.forget(tag);
  liveRegions_.forget(tag);
  liveRegionLabels_.erase(tag);
#ifdef BASALT_HAS_SKIA
  // Unregisters the canvas. This is the one moment a view is known to be
  // finished with, and a canvas left registered is a surface the package will
  // keep rendering into for a tag nothing will ever draw again.
  forgetSkiaCanvas(tag);
#endif
}

// ---------------------------------------------------------------------------
// Applying a ShadowView to a view
// ---------------------------------------------------------------------------

void Win32MountingManager::updateView(RnWin32View *view, const ShadowView &shadowView) {
  applyProps(view, shadowView);
  applyText(view, shadowView);
  applyImage(view, shadowView);
  applyAccessibility(view, shadowView);
#ifdef BASALT_HAS_SKIA
  // After applyProps, which is what puts nativeID on the view -- a canvas
  // registers itself by that id and cannot attach before it is there. The size
  // it reads comes from the shadow view rather than from the frame
  // applyLayoutMetrics is about to set, so being before that one is only
  // incidental.
  //
  // onDidMount_ is handed over as the way to say "the screen is stale": a
  // canvas renders between transactions, when nothing else will invalidate the
  // window. See Win32SkiaPeer.h.
  applySkiaCanvas(view, shadowView, onDidMount_);
#endif
  applyLayoutMetrics(view, shadowView);
  // Last: the scroll manager clamps its offset against the frame that was just
  // applied, and forces the clip that applyProps may have read as `visible`.
  applyScrollView(view, shadowView);
  applyTextInput(view, shadowView);
  // After layout: a control is drawn centred in the frame it was just given,
  // and a <Modal> commits that frame's size back into its own state.
  applyControls(view, shadowView);
}

void Win32MountingManager::applyProps(RnWin32View *view, const ShadowView &shadowView) {
  const auto props = std::dynamic_pointer_cast<const ViewProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  if (props->backgroundColor) {
    const auto components = facebook::react::colorComponentsFromColor(props->backgroundColor);
    view->setBackgroundColor(
        components.red, components.green, components.blue, components.alpha, true);
  } else {
    // Not transparent black: a view with no background does not paint at all.
    view->setBackgroundColor(0, 0, 0, 0, false);
  }

  view->setOpacity(props->opacity);

  // overflow: 'hidden'. React Native's default is 'visible'.
  view->setClipsChildren(props->getClipsContentToBounds());

  // Radii depend on the frame -- percentage radii, and the clamping that stops
  // opposite corners overlapping -- so they are resolved against the layout
  // metrics rather than read raw.
  const auto borders = props->resolveBorderMetrics(shadowView.layoutMetrics);
  // All four corners, each with its own horizontal and vertical radius, in the
  // order GtkMountingManager passes them and describeTree prints them. This
  // used to hand over the top-left horizontal radius alone, which was right for
  // a single `borderRadius` and visibly wrong for anything else.
  const float radii[8] = {
      static_cast<float>(borders.borderRadii.topLeft.horizontal),
      static_cast<float>(borders.borderRadii.topLeft.vertical),
      static_cast<float>(borders.borderRadii.topRight.horizontal),
      static_cast<float>(borders.borderRadii.topRight.vertical),
      static_cast<float>(borders.borderRadii.bottomRight.horizontal),
      static_cast<float>(borders.borderRadii.bottomRight.vertical),
      static_cast<float>(borders.borderRadii.bottomLeft.horizontal),
      static_cast<float>(borders.borderRadii.bottomLeft.vertical),
  };
  view->setCornerRadii(radii);

  // Top, right, bottom, left, as GTK takes them -- the order CSS names the
  // edges in, and not the order RectangleEdges stores them. An edge without a
  // colour is transparent, so it draws nothing whatever its width.
  const float widths[4] = {
      static_cast<float>(borders.borderWidths.top),
      static_cast<float>(borders.borderWidths.right),
      static_cast<float>(borders.borderWidths.bottom),
      static_cast<float>(borders.borderWidths.left),
  };
  float colours[16] = {};
  const facebook::react::SharedColor *edges[4] = {
      &borders.borderColors.top,
      &borders.borderColors.right,
      &borders.borderColors.bottom,
      &borders.borderColors.left,
  };
  for (int i = 0; i < 4; i++) {
    if (*edges[i]) {
      const auto components = facebook::react::colorComponentsFromColor(*edges[i]);
      colours[i * 4 + 0] = components.red;
      colours[i * 4 + 1] = components.green;
      colours[i * 4 + 2] = components.blue;
      colours[i * 4 + 3] = components.alpha;
    }
  }
  view->setBorders(widths, colours);

  // `borderStyle`, from the *resolved* metrics like the widths and the colours
  // above. The raw `props->borderStyles` is a cascade of optionals with a slot
  // per spelling, and `borderStyle: 'dashed'` sets the `all` slot: reading the
  // four sides directly finds nothing, which is how every dashed border in a
  // React app came to draw solid on the other two hosts while the tests that
  // set the style on the view by hand passed.
  //
  // The first side that asks for something other than solid decides the whole
  // border, because a dashed border is one stroked path and a path carries one
  // dash pattern. Both other hosts made the same choice; see
  // backlog/correctness.md.
  {
    const auto &styles = borders.borderStyles;
    win32::RnWin32View::LineStyle style = win32::RnWin32View::LineStyle::Solid;
    const facebook::react::BorderStyle sides[4] = {
        styles.top, styles.right, styles.bottom, styles.left};
    for (const auto &side : sides) {
      if (side == facebook::react::BorderStyle::Dotted) {
        style = win32::RnWin32View::LineStyle::Dotted;
        break;
      }
      if (side == facebook::react::BorderStyle::Dashed) {
        style = win32::RnWin32View::LineStyle::Dashed;
        break;
      }
    }
    view->setBorderStyle(style);
  }

  view->setZIndex(static_cast<int>(props->zIndex.value_or(0)));

  // resolveTransform folds in transformOrigin, but only when one was set: the
  // default anchor is the view's centre, which is what RnWin32View::paint
  // already assumes.
  const auto transform = props->resolveTransform(shadowView.layoutMetrics);
  if (transform == facebook::react::Transform::Identity()) {
    view->setTransform(nullptr);
  } else {
    view->setTransform(transform.matrix.data());
  }

  // `backfaceVisibility: 'hidden'`, which stops the back of a card being drawn
  // mirrored halfway through a flip. Whether a transform has turned away is
  // core/Backface.h's to decide, so all three hide the same face.
  //
  // Applied after the transform, because the view has to know the matrix
  // before it can say which way it is facing.
  view->setHidesBackFace(props->backfaceVisibility ==
                         facebook::react::BackfaceVisibility::Hidden);

  // Only a hidden title bar reads this, to find the drag regions an app marked
  // with <TitleBar.DragRegion>.
  view->setNativeId(props->nativeId);

  // And the same id in the registry, so that another view's
  // `accessibilityLabelledBy` can look one up. See `core/LabelRegistry.h`.
  labels_.setNativeId(shadowView.tag, props->nativeId);
  // `accessibilityLabelledBy`: other views, named by their nativeID, whose text
  // names this one. Only recorded here -- resolving needs every view in the
  // transaction to have been seen, so it happens once at the end of the mount.
  labels_.setLabelledBy(shadowView.tag, props->accessibilityLabelledBy.value);
  // `experimental_accessibilityOrder` is deliberately not recorded here, and the
  // support page says so rather than this host quietly holding a resolution
  // nothing reads: UIA has no reading-order property. A client walks the
  // fragment tree and the order is the order the tree is in, where GTK has
  // `aria-flowto` and AppKit can replace `accessibilityChildren`. Reordering the
  // view tree to fake it would move what is painted. See applyLabelRelations.

  // `accessibilityLiveRegion`: a status message to read out when it changes.
  // Recorded here and acted on after the transaction, the text being whatever
  // the region says once every mutation in the batch has landed.
  liveRegions_.setPoliteness(shadowView.tag, props->accessibilityLiveRegion);
  // The label, if the app set one, which stands in for the text: an icon-only
  // status has no paragraph of its own.
  if (props->accessibilityLiveRegion == facebook::react::AccessibilityLiveRegion::None) {
    liveRegionLabels_.erase(shadowView.tag);
  } else {
    liveRegionLabels_[shadowView.tag] = props->accessibilityLabel;
  }

  // Hit testing only. `hitTest` reads it; nothing about painting does.
  switch (props->pointerEvents) {
    case facebook::react::PointerEventsMode::None:
      view->setPointerEvents(win32::RnWin32View::PointerEvents::None);
      break;
    case facebook::react::PointerEventsMode::BoxNone:
      view->setPointerEvents(win32::RnWin32View::PointerEvents::BoxNone);
      break;
    case facebook::react::PointerEventsMode::BoxOnly:
      view->setPointerEvents(win32::RnWin32View::PointerEvents::BoxOnly);
      break;
    case facebook::react::PointerEventsMode::Auto:
      view->setPointerEvents(win32::RnWin32View::PointerEvents::Auto);
      break;
  }

  // `backgroundImage`: linear and radial gradients. The angle or the ending
  // shape, the box size and CSS's colour-stop fixup are resolved here through
  // `core/Gradients.h`, shared with the other two hosts, and the view layer is
  // handed points and radii and a list of stops: the resolution is the
  // specified part and belongs in one place, and the drawing is the toolkit's.
  //
  // One list rather than two, because `background-image` is one list and the
  // first in it is the one on top: two lists would lose the order between a
  // radial gradient and a linear one.
  //
  // Against this mutation's own frame, and `applyProps` runs on every update,
  // so a resized view gets a resized gradient without the view layer knowing
  // anything about angles or corners.
  {
    const auto &metrics = shadowView.layoutMetrics;
    // The two areas CSS gives a background. The painting area is the border
    // box, which is what it is clipped to; the positioning area is the padding
    // box, which sizes and positions it. They differ on any view with a border.
    const basalt::BackgroundArea painting{0.0f,
                                          0.0f,
                                          static_cast<float>(metrics.frame.size.width),
                                          static_cast<float>(metrics.frame.size.height)};
    // getPaddingFrame's origin is already relative to the view -- it is the
    // border widths -- so nothing here subtracts the frame's own position.
    const auto paddingFrame = metrics.getPaddingFrame();
    const basalt::BackgroundArea positioning{
        static_cast<float>(paddingFrame.origin.x),
        static_cast<float>(paddingFrame.origin.y),
        static_cast<float>(paddingFrame.size.width),
        static_cast<float>(paddingFrame.size.height)};

    std::vector<win32::RnWin32View::Gradient> gradients;
    gradients.reserve(props->backgroundImage.size());
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
      // where the image goes: a gradient that resolved against the view would
      // ignore `backgroundSize` even with the rectangle right.
      win32::RnWin32View::Gradient converted;
      converted.area[0] = layer.x;
      converted.area[1] = layer.y;
      converted.area[2] = layer.width;
      converted.area[3] = layer.height;
      converted.tile[0] = layer.tileX;
      converted.tile[1] = layer.tileY;
      converted.tile[2] = layer.tileWidth;
      converted.tile[3] = layer.tileHeight;
      converted.repeats = layer.repeats();

      const std::vector<facebook::react::ColorStop> *colorStops = nullptr;
      float rayLength = 0.0f;
      if (std::holds_alternative<facebook::react::LinearGradient>(image)) {
        const auto &gradient = std::get<facebook::react::LinearGradient>(image);
        const basalt::GradientLine line =
            basalt::linearGradientLine(gradient, layer.width, layer.height);
        converted.kind = win32::RnWin32View::Gradient::Kind::Linear;
        converted.startX = line.startX + layer.x;
        converted.startY = line.startY + layer.y;
        converted.endX = line.endX + layer.x;
        converted.endY = line.endY + layer.y;
        colorStops = &gradient.colorStops;
        rayLength = line.length();
      } else {
        const auto &gradient = std::get<facebook::react::RadialGradient>(image);
        const basalt::GradientEllipse shape =
            basalt::radialGradientEllipse(gradient, layer.width, layer.height);
        converted.kind = win32::RnWin32View::Gradient::Kind::Radial;
        converted.centreX = shape.centerX + layer.x;
        converted.centreY = shape.centerY + layer.y;
        converted.radiusX = shape.radiusX;
        converted.radiusY = shape.radiusY;
        colorStops = &gradient.colorStops;
        rayLength = shape.rayLength();
      }

      const auto resolved = basalt::resolveGradientStops(*colorStops, rayLength);
      if (resolved.empty()) {
        continue;
      }
      converted.stops.reserve(resolved.size());
      for (const auto &stop : resolved) {
        win32::RnWin32View::GradientStop one;
        one.offset = stop.offset;
        one.colour[0] = stop.red;
        one.colour[1] = stop.green;
        one.colour[2] = stop.blue;
        one.colour[3] = stop.alpha;
        converted.stops.push_back(one);
      }
      gradients.push_back(std::move(converted));
    }
    view->setGradients(std::move(gradients));
  }

  // `boxShadow`, as React Native's own six fields: GSK's shadow nodes take the
  // same six and CALayer's properties are built from them, so nothing converts
  // here either and each view layer decides what a shadow is made of.
  //
  // The list also carries the older iOS shadow props, converted in
  // `core/LegacyShadow.h`: one mechanism from here down, so a view with
  // `shadowOpacity` and a view with `boxShadow` take the same path, and a
  // legacy shadow goes behind every CSS one.
  {
    const std::vector<facebook::react::BoxShadow> all = basalt::allShadows(*props);
    std::vector<win32::RnWin32View::BoxShadow> shadows;
    shadows.reserve(all.size());
    for (const auto &shadow : all) {
      win32::RnWin32View::BoxShadow one;
      one.dx = static_cast<float>(shadow.offsetX);
      one.dy = static_cast<float>(shadow.offsetY);
      one.blur = static_cast<float>(shadow.blurRadius);
      one.spread = static_cast<float>(shadow.spreadDistance);
      if (shadow.color) {
        const auto components = facebook::react::colorComponentsFromColor(shadow.color);
        one.colour[0] = components.red;
        one.colour[1] = components.green;
        one.colour[2] = components.blue;
        one.colour[3] = components.alpha;
      }
      one.inset = shadow.inset;
      shadows.push_back(one);
    }
    view->setBoxShadows(std::move(shadows));
  }

  // The `cursor` style property, as a CSS keyword. GDK's names are CSS's so the
  // GTK host passes it straight through; here the view stores it and the window
  // answers `WM_SETCURSOR` with whatever is under the pointer, Win32 having no
  // per-view cursor at all. `core/CursorNames.h` is what says a value's name.
  view->setCursor(basalt::cursorName(props->cursor));

  // `mixBlendMode`, as a CSS keyword, shared with the other two hosts for the
  // reason `core/BlendModes.h` gives: React Native's list is CSS's and so is
  // every compositor's, so the keyword is the thing that crosses the seam and
  // each view layer maps it. Direct2D's blend modes are CSS's plus a few of its
  // own, including the one GSK has no node for.
  view->setBlendMode(basalt::blendModeName(props->mixBlendMode));

  // `filter`. The arithmetic is `core/Filters.h`'s and is shared with the other
  // two hosts: nine CSS functions collapse to one colour matrix, one blur, one
  // opacity and the drop shadows in order, because seven of them are affine
  // maps of colour and composing those is multiplying their matrices.
  {
    const basalt::ResolvedFilters resolved = basalt::resolveFilters(props->filter);
    win32::RnWin32View::Filters filters;
    if (!resolved.empty()) {
      filters.hasMatrix = resolved.hasMatrix;
      for (int i = 0; i < 16; i++) {
        filters.matrix[i] = resolved.matrix.m[i];
      }
      for (int i = 0; i < 4; i++) {
        filters.offset[i] = resolved.matrix.offset[i];
      }
      filters.blurRadius = resolved.blurRadius;
      filters.opacity = resolved.opacity;
      filters.shadows.reserve(resolved.dropShadows.size());
      for (const auto &shadow : resolved.dropShadows) {
        win32::RnWin32View::FilterShadow one;
        one.dx = shadow.dx;
        one.dy = shadow.dy;
        // The standard deviation unchanged: `CLSID_D2D1Shadow` takes one, where
        // GSK's shadow node takes CSS's radius and the GTK half doubles it.
        one.standardDeviation = shadow.standardDeviation;
        one.colour[0] = shadow.red;
        one.colour[1] = shadow.green;
        one.colour[2] = shadow.blue;
        one.colour[3] = shadow.alpha;
        filters.shadows.push_back(one);
      }
    }
    view->setFilters(filters);
  }

  // The outline: CSS's, drawn outside the box and taking no layout space, so
  // nothing about it touches the frame. React Native carries one width, one
  // colour, one offset and one style for the whole ring, unlike the border's
  // four of each.
  {
    float colour[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (props->outlineColor) {
      const auto components = facebook::react::colorComponentsFromColor(props->outlineColor);
      colour[0] = components.red;
      colour[1] = components.green;
      colour[2] = components.blue;
      colour[3] = components.alpha;
    }
    win32::RnWin32View::LineStyle style = win32::RnWin32View::LineStyle::Solid;
    switch (props->outlineStyle) {
      case facebook::react::OutlineStyle::Dotted:
        style = win32::RnWin32View::LineStyle::Dotted;
        break;
      case facebook::react::OutlineStyle::Dashed:
        style = win32::RnWin32View::LineStyle::Dashed;
        break;
      case facebook::react::OutlineStyle::Solid:
        break;
    }
    view->setOutline(static_cast<float>(props->outlineWidth),
                     static_cast<float>(props->outlineOffset),
                     colour,
                     style);
  }

  // `hitSlop`, which grows what a press can land on without moving a pixel.
  // Top, right, bottom, left, the order the other two hosts store it in and
  // the order CSS names the edges.
  const float insets[4] = {
      static_cast<float>(props->hitSlop.top),
      static_cast<float>(props->hitSlop.right),
      static_cast<float>(props->hitSlop.bottom),
      static_cast<float>(props->hitSlop.left),
  };
  view->setHitSlop(insets);
}

// A <Paragraph> carries its text in state, not props: ParagraphShadowNode
// resolves the whole <Text> subtree into one AttributedString and commits it as
// ParagraphState, which is why nothing here walks child shadow nodes.
void Win32MountingManager::applyText(RnWin32View *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "Paragraph") {
    return;
  }

  const auto state =
      std::dynamic_pointer_cast<const facebook::react::ConcreteState<ParagraphState>>(
          shadowView.state);
  if (state == nullptr) {
    return;
  }

  const auto &data = state->getData();
  // Built through the same function the measurement seam uses, which is what
  // makes the painted lines break where the measured ones did.
  view->setTextLayout(
      win32::buildTextLayout(data.attributedString, data.paragraphAttributes));
}

namespace {

RnImageFit toImageFit(ImageResizeMode mode) {
  switch (mode) {
    case ImageResizeMode::Contain:
      return RnImageFit::Contain;
    case ImageResizeMode::Stretch:
      return RnImageFit::Stretch;
    case ImageResizeMode::Center:
    case ImageResizeMode::None:
      return RnImageFit::Center;
    case ImageResizeMode::Repeat:
      return RnImageFit::Repeat;
    case ImageResizeMode::Cover:
      break;
  }
  return RnImageFit::Cover;
}

} // namespace

void Win32MountingManager::applyImage(RnWin32View *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "Image") {
    return;
  }

  const auto props = std::dynamic_pointer_cast<const ImageProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  const RnImageFit fit = toImageFit(props->resizeMode);
  const std::string uri =
      props->sources.empty() ? std::string{} : props->sources.front().uri;

  // `tintColor`, applied before the load rather than in its callback: the tint
  // is a prop and the image is a loader's answer, and either can arrive first.
  if (props->tintColor) {
    const auto components = facebook::react::colorComponentsFromColor(*props->tintColor);
    const float rgba[4] = {components.red, components.green, components.blue, components.alpha};
    view->setImageTint(true, rgba);
  } else {
    view->setImageTint(false, nullptr);
  }

  // `blurRadius`, for the same reason and in the same place: a prop beside a
  // loader's answer. The GTK host had this assignment *inside* the branch that
  // read the tint, so a blurred image with no tintColor came out sharp and
  // every unit test still passed; see the scenario that caught it.
  view->setImageBlur(static_cast<float>(props->blurRadius));

  // The frames of an animated image, asked of the loader rather than carried in
  // the load callback: being animated is a property of the file, and the
  // callback's job is the pixels. Applied on every mutation like the fit, and
  // the view compares the first frame so a re-apply does not restart it.
  const auto applyAnimation = [this](RnWin32View *target, const std::string &forUri) {
    if (target == nullptr) {
      return;
    }
    win32::RnWin32ImageFrames frames = forUri.empty()
        ? win32::RnWin32ImageFrames{}
        : imageLoader_->animation(forUri);
    target->setImageFrames(
        std::move(frames.frames), std::move(frames.delaysMs), frames.loopCount);
  };

  // A mutation that changed only layout must not restart the load, or an
  // <Image> flickers whenever its parent resizes. The fit is applied every time
  // regardless, because changing it is cheap and does not touch the pixels.
  const auto previous = imageUris_.find(shadowView.tag);
  if (previous != imageUris_.end() && previous->second == uri) {
    view->setImage(view->image(), fit);
    applyAnimation(view, uri);
    return;
  }
  imageUris_[shadowView.tag] = uri;

  if (uri.empty()) {
    view->setImage(nullptr, fit);
    applyAnimation(view, uri);
    return;
  }

  // Clear whatever was showing: the source changed, and leaving the old picture
  // up while the new one loads is worse than a blank box, because it looks like
  // the change did not take.
  view->setImage(nullptr, fit);

  // The fetch is in the shared half, which knows file, data: and http URIs --
  // a URI means the same thing on every desktop -- and the loader puts both it
  // and the decode on a worker thread.
  //
  // The completion captures the *tag*, not the view. A view can be deleted
  // while its bytes are in flight, and capturing the pointer would be a
  // use-after-free on a slow network; looking it up again is the arrangement
  // GTK settled on for the same reason. `this` is safe to capture because the
  // loader is a member and cannot outlive the manager -- and the loader itself
  // guards the case where the manager goes first.
  const Tag tag = shadowView.tag;
  imageLoader_->load(uri, [this, tag, uri, fit, applyAnimation](
                              std::shared_ptr<win32::RnWin32Image> image,
                              const std::string &error) {
    (void)error;
    RnWin32View *target = viewForTag(tag);
    if (target == nullptr) {
      // Deleted while loading. Not an error, and not worth logging: scrolling a
      // list past an image faster than it arrives does exactly this.
      return;
    }
    // And the source may have changed *again* while this one was loading, in
    // which case a later load owns the view and this result is stale.
    const auto current = imageUris_.find(tag);
    if (current == imageUris_.end() || current->second != uri) {
      return;
    }
    target->setImage(std::move(image), fit);
    applyAnimation(target, uri);
    // The screen is stale and no transaction is coming: the bytes arrived on
    // their own clock, after the mount that asked for them. On GTK and AppKit
    // the view is a widget and queues its own draw; here only the host has the
    // HWND, and `onDidMount_` is the way to say so -- which also starts the
    // animation timer, without which an animated GIF would hold its frames and
    // never advance.
    if (onDidMount_) {
      onDidMount_();
    }
  });
}

namespace {

win32::RnAccessibleFlag toFlag(bool value) {
  return value ? win32::RnAccessibleFlag::True : win32::RnAccessibleFlag::False;
}

// The role React Native effectively means for this view: what the app asked
// for, or what the component implies.
//
// A <Text> is a label and an <Image> is an image whether or not the app said
// so, which is what makes an ordinary screen navigable without every developer
// having annotated it. Both other platforms infer the same two.
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

void Win32MountingManager::applyAccessibility(RnWin32View *view, const ShadowView &shadowView) {
  const auto props =
      std::dynamic_pointer_cast<const facebook::react::AccessibilityProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  win32::RnAccessibleInfo info;
  info.role = effectiveRole(shadowView);

  // A label given in props wins. Falling back to a Paragraph's own text means a
  // plain <Text> announces itself without the app having repeated the string in
  // an accessibilityLabel, which is what the other two hosts do.
  info.label = props->accessibilityLabel;
  if (info.label.empty() && view->textLayout() != nullptr) {
    info.label = view->textLayout()->text();
  }
  info.hint = props->accessibilityHint;
  // `testID`, which no host read until 2026-10-09. UIA publishes it as the
  // automation id; see RnWin32Accessible.cpp.
  info.testId = props->testId;
  // `accessibilityViewIsModal`, which UIA calls a dialog.
  info.modal = props->accessibilityViewIsModal;

  const auto &state = props->accessibilityState;
  if (state.has_value()) {
    info.state.disabled = toFlag(state->disabled);
    info.state.selected = toFlag(state->selected);
    info.state.busy = toFlag(state->busy);
    if (state->checked != facebook::react::AccessibilityState::Unchecked ||
        !info.role.empty()) {
      // Tri-state on purpose: `checked` unset is not the same as false, and a
      // view that never mentions being checked must not report ToggleState_Off.
      // React Native's own enum has no "unset", so Unchecked on a view with no
      // role is treated as "did not say".
      info.state.checked = state->checked == facebook::react::AccessibilityState::Checked
          ? win32::RnAccessibleFlag::True
          : win32::RnAccessibleFlag::False;
    }
    if (state->expanded.has_value()) {
      info.state.expanded = toFlag(*state->expanded);
    }
  }

  // `accessibilityValue`: a range, a position in it and a text form, each
  // independent. Carried as it arrived, optionals and all, because an absent
  // part has to stay absent all the way to the provider -- a view that said
  // nothing about its range must not be announced as sitting at the bottom of
  // one. Both other hosts keep the same distinction; see RnWin32Accessible.h.
  info.value.min = props->accessibilityValue.min;
  info.value.max = props->accessibilityValue.max;
  info.value.now = props->accessibilityValue.now;
  info.value.text = props->accessibilityValue.text;

  // accessible={false} and accessibilityElementsHidden both mean "not for a
  // screen reader".
  info.hidden = !props->accessible ||
      props->importantForAccessibility == facebook::react::ImportantForAccessibility::NoHideDescendants;

  view->setAccessibleInfo(info);

  // Keyboard focus. `accessible` is the signal because React Native's
  // `focusable` prop never reaches this platform -- see Win32Focus.h -- and
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
  view->setFocusable((props->accessible || isControl) && !props->accessibilityElementsHidden);
}

void Win32MountingManager::applyLayoutMetrics(RnWin32View *view, const ShadowView &shadowView) {
  const auto &frame = shadowView.layoutMetrics.frame;
  view->setFrame(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);

  // display: 'none' keeps the node in the shadow tree but takes it out of
  // layout and painting -- and, here, out of hit testing too.
  view->setHidden(shadowView.layoutMetrics.displayType == facebook::react::DisplayType::None);

  // TODO(layout): pointScaleFactor, once a high-DPI backing store is involved.
}

void Win32MountingManager::applyScrollView(RnWin32View *view, const ShadowView &shadowView) {
  if (std::string_view(shadowView.componentName) != "ScrollView") {
    return;
  }
  scrollViews_.update(view, shadowView);
}

// ---------------------------------------------------------------------------
// The wheel
// ---------------------------------------------------------------------------

bool Win32MountingManager::scrollAt(
    RnWin32View *root, double x, double y, double deltaX, double deltaY) {
  return scrollViews_.scrollAt(root, x, y, deltaX, deltaY);
}

// ---------------------------------------------------------------------------
// <TextInput>
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

void Win32MountingManager::applyControlPeer(RnWin32View *view, const basalt::ControlState &state) {
  RnWin32View::ControlStyle style;
  style.on = state.on;
  style.disabled = state.disabled;
  style.large = state.large;
  style.hidesWhenStopped = state.hidesWhenStopped;
  style.hasThumb = state.hasForeground;
  style.hasTrackOn = state.hasTrackOn;
  style.hasTrackOff = state.hasTrackOff;
  std::copy(std::begin(state.foreground), std::end(state.foreground), std::begin(style.thumb));
  std::copy(std::begin(state.trackOn), std::end(state.trackOn), std::begin(style.trackOn));
  std::copy(std::begin(state.trackOff), std::end(state.trackOff), std::begin(style.trackOff));

  if (state.kind == basalt::ControlKind::Switch) {
    switchValues_[static_cast<Tag>(view->tag())] = state.on;
  }

  view->setControl(state.kind == basalt::ControlKind::Switch ? RnWin32View::Control::Switch
                                                            : RnWin32View::Control::Spinner,
                   style,
                   basalt::describeControl(state));
}

void Win32MountingManager::setHighlights(RnWin32View *view,
                                        const std::vector<basalt::Highlight> &highlights) {
  std::vector<RnWin32View::Highlight> converted;
  converted.reserve(highlights.size());
  for (const basalt::Highlight &highlight : highlights) {
    RnWin32View::Highlight entry;
    entry.x = highlight.x;
    entry.y = highlight.y;
    entry.width = highlight.width;
    entry.height = highlight.height;
    std::copy(std::begin(highlight.color), std::end(highlight.color), std::begin(entry.color));
    entry.filled = highlight.filled;
    converted.push_back(entry);
  }
  view->setHighlights(std::move(converted));
  // Nothing else asks for a repaint: an overlay command arrives outside a mount
  // transaction, so the window would otherwise show the highlight at whatever
  // moment something else happened to invalidate it.
  if (onDidMount_) {
    onDidMount_();
  }
}

void Win32MountingManager::pressedView(Tag tag) {
  RnWin32View *view = viewForTag(tag);
  if (view == nullptr || view->control() != RnWin32View::Control::Switch) {
    return;
  }
  if (view->controlStyle().disabled) {
    return;
  }
  const auto known = switchValues_.find(tag);
  if (known == switchValues_.end()) {
    return;
  }

  // Told, and then left alone. React Native's <Switch> is a controlled
  // component: the app's `value` prop is the only thing that moves it, and a
  // switch that flipped itself would show a state its props do not agree with
  // -- which is exactly what an app that ignores onValueChange is supposed to
  // look like. The other two hosts have to put their real widget back for the
  // same reason; here there is nothing to put back, because nothing moved.
  basalt::emitSwitchChange(eventEmitterForTag(tag), tag, !known->second);
}

void Win32MountingManager::applyTextInput(RnWin32View *view, const ShadowView &shadowView) {
  if (std::string_view(shadowView.componentName) != "TextInput") {
    return;
  }
  textInputs_.update(view, shadowView);
}

void Win32MountingManager::setHostWindow(HWND window) {
  textInputs_.setHostWindow(window);
}

void Win32MountingManager::syncTextInputBounds(RnWin32View *root) {
  textInputs_.syncBounds(root);
}

bool Win32MountingManager::handleControlCommand(WPARAM wparam, LPARAM lparam) {
  return textInputs_.handleControlCommand(wparam, lparam);
}

HBRUSH Win32MountingManager::controlColor(HDC deviceContext, HWND control) {
  return textInputs_.controlColor(deviceContext, control);
}

bool Win32MountingManager::typeIntoFocusedTextInput(const std::string &text) {
  return textInputs_.typeIntoFocused(text);
}

bool Win32MountingManager::focusTextInputAt(RnWin32View *root, double x, double y) {
  return textInputs_.focusAt(root, x, y);
}

} // namespace basalt
