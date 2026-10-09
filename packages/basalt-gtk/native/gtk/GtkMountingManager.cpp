#include "GtkMountingManager.h"

#ifdef BASALT_HAS_SKIA
#include "GtkSkiaPeer.h"
#endif

#include "ComponentRegistry.h"
#include "ExpoImageComponent.h"
#include "BlendModes.h"
#include "LegacyShadow.h"
#include "CursorNames.h"
#include "Filters.h"
#include "Gradients.h"
#include "UIManagerAccess.h"
#include "PangoTextLayout.h"

#include <cstdint>

#include <react/renderer/components/image/ImageEventEmitter.h>
#include <react/renderer/components/view/AccessibilityProps.h>
#include <react/renderer/graphics/Transform.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/graphics/Color.h>

#include <cassert>
#include <cstdio>
#include <iterator>
#include <map>
#include <string>
#include <type_traits>

namespace basalt {

// `override` proves each signature matches the interface, but not that every
// pure virtual is implemented -- nothing here instantiates the class. This
// catches an IMountingManager method going unimplemented as the interface
// evolves upstream.
static_assert(!std::is_abstract_v<GtkMountingManager>,
              "GtkMountingManager must implement all of IMountingManager");

using facebook::react::ColorComponents;
using facebook::react::ComponentRegistryFactory;
using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::ImageEventEmitter;
using facebook::react::ImageProps;
using facebook::react::ImageResizeMode;
using facebook::react::ParagraphState;
using facebook::react::ViewProps;

namespace {

// React Native's accessibilityRole is an open string, and its vocabulary is
// mostly ARIA's, which is also what GTK's roles are modelled on. Anything
// unrecognised falls back to GENERIC rather than guessing: a wrong role is
// worse for a screen reader than no role, because it makes the widget announce
// itself as something it is not.
GtkAccessibleRole toAccessibleRole(const std::string &role) {
  static const std::unordered_map<std::string, GtkAccessibleRole> kRoles = {
      {"button", GTK_ACCESSIBLE_ROLE_BUTTON},
      {"togglebutton", GTK_ACCESSIBLE_ROLE_TOGGLE_BUTTON},
      {"link", GTK_ACCESSIBLE_ROLE_LINK},
      {"search", GTK_ACCESSIBLE_ROLE_SEARCH_BOX},
      {"image", GTK_ACCESSIBLE_ROLE_IMG},
      {"imagebutton", GTK_ACCESSIBLE_ROLE_BUTTON},
      {"text", GTK_ACCESSIBLE_ROLE_LABEL},
      {"header", GTK_ACCESSIBLE_ROLE_ROW_HEADER},
      {"adjustable", GTK_ACCESSIBLE_ROLE_SLIDER},
      {"alert", GTK_ACCESSIBLE_ROLE_ALERT},
      {"checkbox", GTK_ACCESSIBLE_ROLE_CHECKBOX},
      {"combobox", GTK_ACCESSIBLE_ROLE_COMBO_BOX},
      {"menu", GTK_ACCESSIBLE_ROLE_MENU},
      {"menubar", GTK_ACCESSIBLE_ROLE_MENU_BAR},
      {"menuitem", GTK_ACCESSIBLE_ROLE_MENU_ITEM},
      {"progressbar", GTK_ACCESSIBLE_ROLE_PROGRESS_BAR},
      {"radio", GTK_ACCESSIBLE_ROLE_RADIO},
      {"radiogroup", GTK_ACCESSIBLE_ROLE_RADIO_GROUP},
      {"scrollbar", GTK_ACCESSIBLE_ROLE_SCROLLBAR},
      {"spinbutton", GTK_ACCESSIBLE_ROLE_SPIN_BUTTON},
      {"switch", GTK_ACCESSIBLE_ROLE_SWITCH},
      {"tab", GTK_ACCESSIBLE_ROLE_TAB},
      {"tablist", GTK_ACCESSIBLE_ROLE_TAB_LIST},
      {"list", GTK_ACCESSIBLE_ROLE_LIST},
      {"grid", GTK_ACCESSIBLE_ROLE_GRID},
      {"toolbar", GTK_ACCESSIBLE_ROLE_TOOLBAR},
      {"tooltip", GTK_ACCESSIBLE_ROLE_TOOLTIP},
      {"none", GTK_ACCESSIBLE_ROLE_PRESENTATION},
      {"presentation", GTK_ACCESSIBLE_ROLE_PRESENTATION},
  };

  const auto it = kRoles.find(role);
  return it == kRoles.end() ? GTK_ACCESSIBLE_ROLE_GENERIC : it->second;
}

RnAccessibleFlag toFlag(bool value) {
  return value ? RN_A11Y_TRUE : RN_A11Y_FALSE;
}

GdkRGBA toRgba(const ColorComponents &components) {
  GdkRGBA rgba;
  rgba.red = components.red;
  rgba.green = components.green;
  rgba.blue = components.blue;
  rgba.alpha = components.alpha;
  return rgba;
}

} // namespace

GtkMountingManager::GtkMountingManager()
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

GtkMountingManager::~GtkMountingManager() noexcept {
  // MountingWalk cannot do this itself: by the time a base destructor runs, the
  // GTK half that knows how to release a widget is already gone.
  releaseAllViews();
}

// ---------------------------------------------------------------------------
// Mutation walk
// ---------------------------------------------------------------------------

namespace {

// Carries a transaction from the JS thread to the GTK main thread.
struct PendingMount {
  GtkMountingManager *manager;
  SurfaceId surfaceId;
  MountingTransaction transaction;
  // Whether `manager` is still there, and which instance this came from. See
  // MountingWalk::mountGuard.
  std::weak_ptr<bool> alive;
  std::uint64_t epoch;
};

gboolean applyPendingMount(gpointer data) {
  auto *pending = static_cast<PendingMount *>(data);
  // The guard first: dereferencing `manager` to read its epoch is itself the
  // use-after-free when the host has already torn it down.
  if (pending->alive.expired() || pending->manager->mountEpoch() != pending->epoch) {
    delete pending;
    return G_SOURCE_REMOVE;
  }
  pending->manager->applyTransaction(pending->surfaceId, std::move(pending->transaction));
  delete pending;
  return G_SOURCE_REMOVE;
}

// The same trip for an imperative command. See dispatchCommand below.
struct PendingCommand {
  GtkMountingManager *manager;
  Tag tag;
  std::string name;
  folly::dynamic args;
};

gboolean applyPendingCommand(gpointer data) {
  std::unique_ptr<PendingCommand> pending{static_cast<PendingCommand *>(data)};
  pending->manager->applyCommand(pending->tag, pending->name, pending->args);
  return G_SOURCE_REMOVE;
}

} // namespace

void GtkMountingManager::executeMount(SurfaceId surfaceId, MountingTransaction &&transaction) {
  // This runs on the JS thread: Scheduler::uiManagerDidFinishTransaction queues
  // the mount via RuntimeScheduler::scheduleRenderingUpdate, which drains in
  // the event loop's "update the rendering" step with the jsi::Runtime live.
  // GTK widgets are main-thread-only, so nothing here may touch them.
  //
  // Always queue, never invoke directly even when already on the main thread:
  // g_idle sources at equal priority run in the order they were added, which is
  // what keeps mutation ordering intact. iOS and Android marshal here too.
  auto *pending =
      new PendingMount{this, surfaceId, std::move(transaction), mountGuard(), mountEpoch()};
  g_idle_add_full(G_PRIORITY_DEFAULT, applyPendingMount, pending, nullptr);
}

void GtkMountingManager::applyTransaction(SurfaceId surfaceId, MountingTransaction &&transaction) {
  // The walk itself is in core/MountingWalk.h and is shared with every other
  // desktop platform; what is GTK about mounting is below, in the handful of
  // operations it calls back into.
  applyMutations(transaction.getMutations());

  // Now that the tree is on screen, anything `autoFocus` asked for can be given
  // focus. Not before: see GtkTextInput.h.
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
  // the text a live region says now is what every mutation in this batch has
  // left it saying.
  announceLiveRegions();
}

void GtkMountingManager::announceLiveRegions() {
  if (liveRegions_.empty()) {
    return;
  }
  for (const facebook::react::Tag tag : liveRegions_.tags()) {
    RnView *view = viewForTag(tag);
    if (view == nullptr) {
      continue;
    }
    // A label stands in for the text when the app set one: an icon-only status
    // has no paragraph of its own, and the label is what would be read. The
    // AppKit host makes the same choice in the same order.
    const auto label = liveRegionLabels_.find(tag);
    std::string text = label != liveRegionLabels_.end() ? label->second : std::string();
    if (text.empty()) {
      char *collected = rn_view_collect_text(view);
      text = collected != nullptr ? collected : "";
      g_free(collected);
    }
    const auto politeness = liveRegions_.noticed(tag, text);
    if (!politeness.has_value()) {
      continue;
    }
    const gboolean assertive = *politeness == basalt::LiveRegionPoliteness::Assertive;
    rn_view_announce(view, text.c_str(), assertive);
    // Logged as well as announced: nothing in an automated run is connected to
    // AT-SPI, so this line is how an end-to-end test sees that it happened.
    LOG(INFO) << "announced" << (assertive ? " (assertive): " : ": ") << text;
  }
}

void GtkMountingManager::applyLabelRelations() {
  if (labels_.empty()) {
    return;
  }
  for (const auto &change : labels_.changes()) {
    RnView *view = viewForTag(change.tag);
    if (view == nullptr) {
      continue;
    }
    std::vector<RnView *> resolved;
    resolved.reserve(change.labels.size());
    for (const facebook::react::Tag tag : change.labels) {
      if (RnView *label = viewForTag(tag); label != nullptr) {
        resolved.push_back(label);
      }
    }
    rn_view_set_labelled_by(view, resolved.data(), static_cast<int>(resolved.size()));
  }
}

// ---------------------------------------------------------------------------
// What MountingWalk asks of a platform
// ---------------------------------------------------------------------------

RnView *GtkMountingManager::createView(const ShadowView &shadowView) {
  // The accessible role has to be decided now: GTK4 makes it construct-only,
  // and RnView is one class for every React Native view. This is the reason
  // createView is handed the whole ShadowView rather than just a tag.
  // The role React Native effectively means for this view: what the app asked
  // for, or what the component implies. A <Text> is a label and an <Image> is an
  // image whether or not the app said so, which is what makes an ordinary screen
  // navigable without every developer having annotated it.
  std::string roleName;
  if (const auto accessibility =
          std::dynamic_pointer_cast<const facebook::react::AccessibilityProps>(shadowView.props)) {
    roleName = accessibility->accessibilityRole;
  }
  if (roleName.empty() && shadowView.componentName != nullptr) {
    const std::string_view name(shadowView.componentName);
    if (name == "Paragraph") {
      roleName = "text";
    } else if (name == "Image") {
      roleName = "image";
    }
  }
  GtkAccessibleRole role = toAccessibleRole(roleName);

  // A <TextInput>'s wrapper is not a control, and should not be an element.
  //
  // The peer inside it is a real GtkText and is already the text box AT-SPI
  // presents; the wrapper around it adds a second stop that says the same
  // thing or nothing. Presentation is how GTK says "I am scaffolding" -- and
  // it has to be decided here because a GtkAccessible role is construct-only,
  // which is the whole reason the AppKit side could do this in
  // applyAccessibility and this one could not.
  //
  // `roleName` is deliberately left empty rather than set to something like
  // "presentation": it is React Native's vocabulary and it prints in
  // describeTree, where AppKit has nothing to print. That asymmetry already
  // broke the cross-host diff once, in phase 53.
  if (roleName.empty() && shadowView.componentName != nullptr &&
      std::string_view(shadowView.componentName) == "TextInput") {
    role = GTK_ACCESSIBLE_ROLE_PRESENTATION;
  }

  RnView *view = rn_view_new_with_role(static_cast<int>(shadowView.tag), role);

  // React Native's own name for the same decision, for the tree dump. Set here
  // rather than in applyAccessibility so the two vocabularies always describe
  // the same choice: GTK's role is construct-only, so a role changed after
  // mount would leave the name telling a truth the widget does not.
  rn_view_set_role_name(view, roleName.c_str());
  // A fresh GtkWidget carries a floating reference. Sinking it here makes the
  // registry the owner, so the view survives the gap between a Remove and the
  // Insert that re-parents it.
  g_object_ref_sink(view);
  return view;
}

RnView *GtkMountingManager::createRootView(Tag tag) {
  RnView *root = rn_view_new(static_cast<int>(tag));
  g_object_ref_sink(root);
  return root;
}

void GtkMountingManager::destroyView(RnView *view) {
  g_object_unref(view);
}

void GtkMountingManager::insertChild(RnView *parent, RnView *child, int index) {
  rn_view_insert_child(parent, child, index);
}

void GtkMountingManager::removeChild(RnView *parent, RnView *child) {
  rn_view_remove_child(parent, child);
}

void GtkMountingManager::forgetTag(Tag tag) {
  liveRegions_.forget(tag);
  liveRegionLabels_.erase(tag);
  // Both sides of a label relation: a view that goes away stops naming anything
  // and stops being nameable, and a relation left pointing at it would have a
  // screen reader read a view that is no longer on screen.
  labels_.forget(tag);
  imageUris_.erase(tag);
  scrollViews_.remove(tag);
  textInputs_.remove(tag);
  switchValues_.erase(tag);
  controlClasses_.erase(tag);
#ifdef BASALT_HAS_SKIA
  // Unregisters the canvas. This is the one moment a view is known to be
  // finished with, and a canvas left registered is a surface the package
  // keeps rendering into for a widget nobody will draw again.
  forgetSkiaCanvas(tag);
#endif
}

void GtkMountingManager::dispatchCommand(const ShadowView &shadowView,
                                         const std::string &commandName,
                                         const folly::dynamic &args) {
  // This arrives on the JS thread, inside the event loop's rendering update,
  // exactly like executeMount -- so it may not touch a widget either. That was
  // survivable while the only commands were ScrollView's, which only move an
  // adjustment; TextInput's `focus` reaches the platform input method, and on
  // macOS AppKit asserts it is on the main thread and traps the process.
  //
  // Queued at the same priority as a mount, so it stays behind the transaction
  // that created the view it names: g_idle sources at equal priority run in
  // the order they were added.
  auto *pending = new PendingCommand{this, shadowView.tag, commandName, args};
  g_idle_add_full(G_PRIORITY_DEFAULT, applyPendingCommand, pending, nullptr);
}

void GtkMountingManager::applyCommand(Tag tag,
                                      const std::string &commandName,
                                      const folly::dynamic &args) {
  assert(onMainThread() && "applyCommand must run on the GTK main thread");

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
  g_debug("dispatchCommand '%s' on tag %d is not implemented",
          commandName.c_str(),
          static_cast<int>(tag));
}

void GtkMountingManager::setUIManager(std::weak_ptr<facebook::react::UIManager> uiManager) noexcept {
  setSharedUIManager(std::move(uiManager));
}

void GtkMountingManager::setSchedulerTaskExecutor(
    facebook::react::SchedulerTaskExecutor &&schedulerTaskExecutor) noexcept {
  // A null executor means `destroyReactInstance`: the Scheduler and the
  // SurfaceManager are about to go, and anything this already queued to the UI
  // thread would be applied against them. Non-null means a new instance, which
  // needs nothing from here.
  if (!schedulerTaskExecutor) {
    invalidatePendingMounts();
  }
}

ComponentRegistryFactory GtkMountingManager::getComponentRegistryFactory() {
  // The default factory registers the core components RN ships C++ descriptors
  // for (View, Text, Image, ScrollView, ...). Linux-specific components get
  // added here later.
  return facebook::react::getDefaultComponentRegistryFactory();
}

namespace {

RnImageFit toImageFit(ImageResizeMode mode) {
  switch (mode) {
    case ImageResizeMode::Contain:
      return RN_IMAGE_FIT_CONTAIN;
    case ImageResizeMode::Stretch:
      return RN_IMAGE_FIT_STRETCH;
    case ImageResizeMode::Center:
    case ImageResizeMode::None:
      return RN_IMAGE_FIT_CENTER;
    case ImageResizeMode::Repeat:
      return RN_IMAGE_FIT_REPEAT;
    case ImageResizeMode::Cover:
      break;
  }
  return RN_IMAGE_FIT_COVER;
}

} // namespace

// React Native's cxx ImageManager is a stub that never produces an
// ImageResponse, so nothing arrives through ImageState. The URI is read off the
// props and loaded here instead, which is also how Android does it.
void GtkMountingManager::applyImage(RnView *view, const ShadowView &shadowView) {
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

  RnImageFit fit = RN_IMAGE_FIT_COVER;
  facebook::react::ImageSource source{};
  // `tintColor` on React Native's <Image>, and the same prop on expo-image.
  facebook::react::SharedColor tint{};
  // blurRadius alongside it, captured the same way: both props are read in
  // branches that each know their own props type, and applied together below.
  // expo-image has no blur prop, so that branch leaves this at zero.
  float blurRadius = 0.0F;

  if (isExpoImage) {
    const auto props = std::dynamic_pointer_cast<const facebook::react::ExpoImageProps>(shadowView.props);
    if (props == nullptr) {
      return;
    }
    fit = toImageFit(props->contentFit);
    tint = props->tintColor;
    if (!props->sources.empty()) {
      source = props->sources.front();
    }
  } else {
    const auto props = std::dynamic_pointer_cast<const ImageProps>(shadowView.props);
    if (props == nullptr) {
      return;
    }
    fit = toImageFit(props->resizeMode);
    // React Native's is optional where expo-image's is a plain SharedColor;
    // both spell "no tint" as a falsy SharedColor once unwrapped.
    if (props->tintColor) {
      tint = *props->tintColor;
    }
    blurRadius = static_cast<float>(props->blurRadius);
    if (!props->sources.empty()) {
      source = props->sources.front();
    }
  }

  // Applied before the load rather than in its callback: the tint is a prop and
  // the texture is a loader's answer, and either can arrive first. Setting it
  // here means a re-render that only changes the colour repaints without
  // touching the image.
  if (RnView *tinted = viewForTag(shadowView.tag); tinted != nullptr) {
    if (tint) {
      const GdkRGBA rgba = toRgba(colorComponentsFromColor(tint));
      rn_view_set_image_tint(tinted, TRUE, &rgba);
    } else {
      rn_view_set_image_tint(tinted, FALSE, nullptr);
    }
    // blurRadius, for the same reason the tint is here: it is a prop rather than
    // a loader's answer, so a re-render that changes only the radius repaints
    // without touching the image.
    rn_view_set_image_blur(tinted, blurRadius);
  }

  const std::string uri = source.uri;
  const Tag tag = shadowView.tag;

  // A mutation that changed only layout must not restart the load, or an
  // <Image> would flicker every time its parent resized. Re-requesting the same
  // URI is cheap -- the loader answers from its cache on this thread -- and it
  // reapplies the fit, which is the only thing that can have changed.
  const auto known = imageUris_.find(tag);
  if (known != imageUris_.end() && known->second == uri) {
    if (!uri.empty()) {
      imageLoader_->load(uri, [this, tag, fit](GdkTexture *texture, const std::string &) {
        if (RnView *target = viewForTag(tag); target != nullptr) {
          rn_view_set_texture(target, texture, fit);
        }
      });
    }
    return;
  }

  imageUris_[tag] = uri;

  if (uri.empty()) {
    rn_view_set_texture(view, nullptr, fit);
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

  imageLoader_->load(uri, [this, tag, fit, source, isExpoImage](GdkTexture *texture,
                                                              const std::string &error) {
    // The view may have been deleted while the image was in flight, which is
    // why this looks the tag up again rather than capturing the widget.
    RnView *target = viewForTag(tag);
    if (target != nullptr) {
      rn_view_set_texture(target, texture, fit);
    }

    if (texture == nullptr) {
      g_warning("image failed to load: %s (%s)", source.uri.c_str(), error.c_str());
    }

    if (isExpoImage) {
      auto emitter =
          std::dynamic_pointer_cast<const facebook::react::ExpoImageEventEmitter>(eventEmitterForTag(tag));
      if (emitter == nullptr) {
        return;
      }
      if (texture != nullptr) {
        // The pixel dimensions, which is what expo-image's onLoad reports and
        // what an app sizing itself to an image reads.
        emitter->onLoad(source,
                        static_cast<double>(gdk_texture_get_width(texture)),
                        static_cast<double>(gdk_texture_get_height(texture)));
      } else {
        emitter->onError(error);
      }
      return;
    }

    auto emitter = std::dynamic_pointer_cast<const ImageEventEmitter>(eventEmitterForTag(tag));
    if (emitter == nullptr) {
      return;
    }
    if (texture != nullptr) {
      emitter->onLoad(source);
    } else {
      emitter->onError(facebook::react::ImageErrorInfo{.error = error});
    }
    emitter->onLoadEnd();
  });
}

// Accessibility, as AT-SPI and therefore Orca sees it.
//
// The role is not here: GTK4's accessible role is construct-only, so it is
// chosen when the widget is made. See the Create mutation.
void GtkMountingManager::applyAccessibility(RnView *view, const ShadowView &shadowView) {
  const auto props = std::dynamic_pointer_cast<const facebook::react::AccessibilityProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  // `accessibilityLiveRegion`: a status message to read out when it changes.
  // Recorded here and acted on after the transaction, the text being whatever
  // the region says once every mutation in the batch has landed.
  liveRegions_.setPoliteness(shadowView.tag, props->accessibilityLiveRegion);
  // The label, if the app set one, which stands in for the text: an icon-only
  // status has no paragraph of its own. Kept here rather than read back from
  // GTK, which has no getter for an accessible property.
  if (props->accessibilityLiveRegion == facebook::react::AccessibilityLiveRegion::None) {
    liveRegionLabels_.erase(shadowView.tag);
  } else {
    liveRegionLabels_[shadowView.tag] = props->accessibilityLabel;
  }

  // `accessibilityLabelledBy`: other views, named by their nativeID, whose text
  // names this one. Only recorded here -- the resolution needs every view in the
  // transaction to have been seen, so it happens once at the end of the mount.
  labels_.setLabelledBy(shadowView.tag, props->accessibilityLabelledBy.value);

  // A role that changed after the view was mounted, which GTK cannot honour:
  // `accessible-role` is construct-only, so the role this widget was created
  // with is fixed for its life. What can still be updated is the description a
  // screen reader reads out, so the announcement follows even though the role
  // does not. Anything inferred from the role itself, such as which navigation
  // commands apply, still follows the original. See backlog/accessibility.md.
  //
  // Compared against the widget rather than against remembered state, because
  // GTK already knows what it was built as and a second copy of that could
  // disagree with it.
  {
    const std::string &wantedName = props->accessibilityRole;
    const GtkAccessibleRole built = gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(view));
    if (!wantedName.empty() && toAccessibleRole(wantedName) != built) {
      rn_view_set_accessible_role_description(view, wantedName.c_str());
    } else {
      rn_view_set_accessible_role_description(view, nullptr);
    }
  }

  // A label given in props wins. Falling back to a Paragraph's own text means a
  // plain <Text> announces itself without the app having to repeat the string in
  // an accessibilityLabel.
  std::string label = props->accessibilityLabel;
  if (label.empty() && shadowView.componentName != nullptr &&
      std::string_view(shadowView.componentName) == "Paragraph") {
    if (const auto state =
            std::dynamic_pointer_cast<const facebook::react::ConcreteState<ParagraphState>>(shadowView.state)) {
      label = state->getData().attributedString.getString();
    }
  }

  // A <TextInput>'s label belongs on its peer, not on this view.
  //
  // The peer is a real GtkText and is already a text box to AT-SPI in its own
  // right, so it is the thing a screen reader lands on. Left alone the label
  // goes on the wrapper, and the field announces itself as an unnamed text box
  // -- a field the app carefully labelled read out as if it had none. AppKit
  // had the same split and it was visible there: the wrapper carried "your
  // name" and the AXTextField inside it carried nothing.
  //
  // The wrapper's own label is cleared rather than left as a duplicate, so the
  // name is announced once. Its *role* stays what it was built with: unlike
  // AppKit's, a GtkAccessible role is construct-only, so a wrapper that should
  // be presentational cannot become one here. That is the smaller half of the
  // problem and is in docs/BACKLOG.md.
  if (GtkWidget *peer = rn_view_get_editable(view)) {
    gtk_accessible_update_property(GTK_ACCESSIBLE(peer),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   label.c_str(),
                                   GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                   props->accessibilityHint.c_str(),
                                   -1);
    rn_view_set_accessible_text(view, "", "");
  } else {
    rn_view_set_accessible_text(view, label.c_str(), props->accessibilityHint.c_str());
  }

  // accessibilityValue, whose four parts are independent: a range, a current
  // position in it, and a text form that a screen reader prefers over the
  // number. Each absent part is reset rather than given a number, so a view that
  // says nothing about its range is not announced as a slider at zero.
  if (props->accessibilityValue.min.has_value() || props->accessibilityValue.max.has_value() ||
      props->accessibilityValue.now.has_value() || props->accessibilityValue.text.has_value()) {
    const auto &value = props->accessibilityValue;
    rn_view_set_accessible_value(
        view,
        value.min.has_value() ? *value.min : RN_A11Y_VALUE_UNSET,
        value.max.has_value() ? *value.max : RN_A11Y_VALUE_UNSET,
        value.now.has_value() ? *value.now : RN_A11Y_VALUE_UNSET,
        value.text.has_value() ? value.text->c_str() : nullptr);
  } else {
    rn_view_set_accessible_value(
        view, RN_A11Y_VALUE_UNSET, RN_A11Y_VALUE_UNSET, RN_A11Y_VALUE_UNSET, nullptr);
  }

  if (props->accessibilityState.has_value()) {
    const auto &state = *props->accessibilityState;
    RnAccessibleFlag checked = RN_A11Y_UNSET;
    switch (state.checked) {
      case facebook::react::AccessibilityState::Checked:
        checked = RN_A11Y_TRUE;
        break;
      case facebook::react::AccessibilityState::Unchecked:
        checked = RN_A11Y_FALSE;
        break;
      case facebook::react::AccessibilityState::Mixed:
      case facebook::react::AccessibilityState::None:
        break;
    }
    rn_view_set_accessible_state(
        view,
        toFlag(state.disabled),
        checked,
        toFlag(state.selected),
        state.expanded.has_value() ? toFlag(*state.expanded) : RN_A11Y_UNSET,
        toFlag(state.busy));
  } else {
    rn_view_set_accessible_state(view, RN_A11Y_UNSET, RN_A11Y_UNSET, RN_A11Y_UNSET, RN_A11Y_UNSET, RN_A11Y_UNSET);
  }

  // Keyboard focus. `accessible` is the signal because React Native's
  // `focusable` prop never reaches this platform -- see GtkFocus.h -- and
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
  rn_view_set_focusable(
      view,
      (props->accessible || isControl) && !props->accessibilityElementsHidden ? TRUE : FALSE);

  // accessibilityElementsHidden hides the subtree; importantForAccessibility
  // NoHideDescendants is Android's spelling of the same idea.
  const bool hidden = props->accessibilityElementsHidden ||
      props->importantForAccessibility == facebook::react::ImportantForAccessibility::NoHideDescendants;
  rn_view_set_accessible_hidden(view, hidden ? TRUE : FALSE);
}

void GtkMountingManager::applyTextInput(RnView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr || std::string_view(shadowView.componentName) != "TextInput") {
    return;
  }
  textInputs_.update(view, shadowView);
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

namespace {

// A colour as CSS, because that is the only way to reach the inside of a GTK
// control. A GtkSpinner's arc is its `color` and a GtkSwitch's track is its
// `background-color`; neither is a property, and neither is reachable through
// the widget API at all.
std::string cssColor(const float rgba[4]) {
  char buffer[64];
  std::snprintf(buffer,
                sizeof(buffer),
                "rgba(%d,%d,%d,%g)",
                static_cast<int>(rgba[0] * 255.0F + 0.5F),
                static_cast<int>(rgba[1] * 255.0F + 0.5F),
                static_cast<int>(rgba[2] * 255.0F + 0.5F),
                static_cast<double>(rgba[3]));
  return buffer;
}

// The CSS class for a control with these colours, installing a display-wide
// rule for it the first time that combination is seen.
//
// Display-wide rather than per-widget because the per-widget route is
// `gtk_widget_get_style_context`, deprecated since GTK 4.10 and gone in 5. One
// provider holding a rule per distinct colour combination costs an app that
// uses two tinted spinners exactly two rules, and an app that tints nothing
// costs nothing: the empty state returns no class at all.
const char *controlCssClass(const basalt::ControlState &state) {
  if (!state.hasForeground && !state.hasTrackOn && !state.hasTrackOff) {
    return nullptr;
  }

  // Keyed on the rule text, which is the only thing that actually has to be
  // distinct. Never emptied: an app has a handful of tints, not a stream of
  // them, and a rule removed while a widget still carries its class would
  // silently lose the colour.
  static std::map<std::string, std::string> classes;
  static GtkCssProvider *provider = nullptr;

  std::string body;
  if (state.kind == basalt::ControlKind::Switch) {
    // GtkSwitch draws the track on its own node and the thumb on a child
    // "slider" node. `:checked` is how GTK spells "on", which is what makes
    // React Native's two track colours expressible at all.
    if (state.hasTrackOff) {
      body += " { background-image: none; background-color: " + cssColor(state.trackOff) + "; }";
    }
    if (state.hasTrackOn) {
      body += ":checked { background-image: none; background-color: " + cssColor(state.trackOn) + "; }";
    }
    if (state.hasForeground) {
      body += " > slider { background-color: " + cssColor(state.foreground) + "; }";
    }
  } else if (state.hasForeground) {
    body += " { color: " + cssColor(state.foreground) + "; }";
  }
  if (body.empty()) {
    return nullptr;
  }

  const auto found = classes.find(body);
  if (found != classes.end()) {
    return found->second.c_str();
  }

  const std::string name = "rn-control-" + std::to_string(classes.size());
  classes.emplace(body, name);

  GdkDisplay *display = gdk_display_get_default();
  if (display == nullptr) {
    // No display yet, so nothing is being drawn either. The class is still
    // handed back, and the rule for it arrives with the next one.
    return classes.find(body)->second.c_str();
  }
  if (provider == nullptr) {
    provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
  std::string css;
  for (const auto &[rule, className] : classes) {
    css += "." + className + rule + "\n";
  }
  gtk_css_provider_load_from_string(provider, css.c_str());
  return classes.find(body)->second.c_str();
}

} // namespace

void GtkMountingManager::setHighlights(RnView *view,
                                      const std::vector<basalt::Highlight> &highlights) {
  // Flattened into the eight-floats-per-rectangle array RnView takes, because
  // that file has no React Native in it and is not about to start.
  std::vector<float> values;
  std::vector<gboolean> filled;
  values.reserve(highlights.size() * 8);
  filled.reserve(highlights.size());
  for (const basalt::Highlight &highlight : highlights) {
    values.push_back(highlight.x);
    values.push_back(highlight.y);
    values.push_back(highlight.width);
    values.push_back(highlight.height);
    values.insert(values.end(), std::begin(highlight.color), std::end(highlight.color));
    filled.push_back(highlight.filled ? TRUE : FALSE);
  }
  rn_view_set_highlights(view,
                         values.empty() ? nullptr : values.data(),
                         filled.empty() ? nullptr : filled.data(),
                         static_cast<int>(highlights.size()));
}

void GtkMountingManager::pressedView(Tag tag) {
  RnView *view = viewForTag(tag);
  if (view == nullptr || rn_view_get_control_kind(view) != RN_CONTROL_SWITCH) {
    return;
  }
  const auto known = switchValues_.find(tag);
  if (known == switchValues_.end() || rn_view_get_control_disabled(view)) {
    return;
  }

  // Told, and then left alone. React Native's <Switch> is a controlled
  // component: the app's `value` prop is the only thing that moves it, and a
  // switch that flipped itself would show a state its props do not agree with
  // -- which is exactly what an app that ignores onValueChange is supposed to
  // look like. The GtkSwitch never moved, because it never saw the click.
  basalt::emitSwitchChange(eventEmitterForTag(tag), tag, !known->second);
}

void GtkMountingManager::applyControlPeer(RnView *view, const basalt::ControlState &state) {
  const RnControlKind kind =
      state.kind == basalt::ControlKind::Switch ? RN_CONTROL_SWITCH : RN_CONTROL_SPINNER;
  GtkWidget *control = rn_view_set_control(view, kind);
  if (control == nullptr) {
    return;
  }

  const Tag tag = static_cast<Tag>(rn_view_get_tag(view));

  if (state.kind == basalt::ControlKind::Switch) {
    switchValues_[tag] = state.on;
    gtk_switch_set_active(GTK_SWITCH(control), state.on ? TRUE : FALSE);
    // Drawn, not driven. The GtkSwitch is taken out of hit testing so that a
    // press lands on the RnView behind it and reaches React Native's touch
    // path, which is what `pressedView` picks up -- and what makes a press on
    // a switch mean the same thing on three desktops, since the Win32 one is
    // painted and has no widget to click at all. It also means the synthesised
    // taps the test suite uses reach it, which a GtkSwitch handling its own
    // input would not.
    //
    // `sensitive` would do this too and would also grey the widget out, which
    // is what `disabled` is for and must stay distinguishable from it.
    gtk_widget_set_can_target(control, FALSE);
    gtk_widget_set_sensitive(control, state.disabled ? FALSE : TRUE);
  } else {
    gtk_spinner_set_spinning(GTK_SPINNER(control), state.on ? TRUE : FALSE);
    // `hidesWhenStopped` defaults to true, and a <RefreshControl> that is not
    // refreshing shows nothing at all -- which is the same rule.
    gtk_widget_set_visible(control, (state.on || !state.hidesWhenStopped) ? TRUE : FALSE);
  }

  // The tint, if the app asked for one. Removing the previous class matters:
  // an app animating a colour would otherwise accumulate every class it has
  // ever had, and the first one would keep winning.
  if (const auto previous = controlClasses_.find(tag); previous != controlClasses_.end()) {
    gtk_widget_remove_css_class(control, previous->second);
  }
  const char *className = controlCssClass(state);
  if (className != nullptr) {
    gtk_widget_add_css_class(control, className);
    controlClasses_[tag] = className;
  } else {
    controlClasses_.erase(tag);
  }

  rn_view_set_control_disabled(view, state.disabled ? TRUE : FALSE);
  const std::string described = basalt::describeControl(state);
  rn_view_set_control_description(view, described.c_str());
}

void GtkMountingManager::applyScrollView(RnView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr || std::string_view(shadowView.componentName) != "ScrollView") {
    return;
  }
  scrollViews_.update(view, shadowView);
}

bool GtkMountingManager::hasComponent(const std::string &name) {
  // Paragraph is the mountable half of <Text>; Text and RawText exist only in
  // the shadow tree, folded into the Paragraph's AttributedString. Image still
  // needs an IImageLoader, and ScrollView a GtkScrolledWindow peer.
  // ScrollView's content child arrives as "ScrollContentView", which the
  // registry rewrites to "View" before it reaches here, so it needs no entry.
  return name == "View" || name == "RootView" || name == "Paragraph" || name == "Image" ||
      name == "ScrollView" || name == "TextInput" || name == "ActivityIndicatorView" ||
      name == "Switch" || name == "ModalHostView" || name == "PullToRefreshView" ||
      name == "UnimplementedNativeView" || name == "DebuggingOverlay";
}

// ---------------------------------------------------------------------------
// Applying a ShadowView to a widget
// ---------------------------------------------------------------------------

void GtkMountingManager::updateView(RnView *view, const ShadowView &shadowView) {
  applyProps(view, shadowView);
  applyText(view, shadowView);
  applyImage(view, shadowView);
#ifdef BASALT_HAS_SKIA
  // After applyProps, which is what puts nativeID on the view: a canvas
  // registers itself by that id and cannot attach before it is there.
  //
  // Sharing rn_view_set_texture with applyImage is safe rather than lucky:
  // applyImage returns on anything that is not <Image> or expo-image, so a
  // SkiaPictureView never reaches its texture call. The two write the same
  // field and can never both want it, because no component is both.
  applySkiaCanvas(GTK_WIDGET(view), shadowView);
#endif
  // The peer first, then accessibility. A <TextInput>'s label belongs on its
  // peer and applyAccessibility can only put it there if the peer exists --
  // and on the mount that creates it, in this order it does.
  applyTextInput(view, shadowView);
  applyAccessibility(view, shadowView);
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

// A <Paragraph> carries its text in state, not props: ParagraphShadowNode
// resolves the whole <Text> subtree into one AttributedString and commits it as
// ParagraphState, which is why nothing here walks child shadow nodes.
void GtkMountingManager::applyText(RnView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "Paragraph") {
    return;
  }

  const auto state = std::dynamic_pointer_cast<const facebook::react::ConcreteState<ParagraphState>>(shadowView.state);
  if (state == nullptr) {
    return;
  }

  const auto &data = state->getData();

  // Measurement already ran through the same builder, with the width Yoga then
  // assigned. Rebuilding it here at that width is what makes the painted lines
  // break where the measured ones did.
  const float width = static_cast<float>(shadowView.layoutMetrics.frame.size.width);
  // On the main thread, and this is the one assertion the per-thread Pango
  // context rests on. Each thread lays out against its own context and font map,
  // which is what removed the process-wide mutex, and the price is that a layout
  // must be painted by the thread that built it. This one is handed to a widget
  // and painted during a snapshot, so it has to be built here. The condition
  // holds today, every mutation arriving through the main queue, but it holds
  // incidentally, and a future change that built a layout on the layout thread
  // and gave it to a widget would be a silent race rather than a failure. See
  // threadPangoContext in PangoTextLayout.cpp.
  assert(onMainThread() && "a painted PangoLayout must be built on the GTK main thread");
  PangoLayout *layout = basalt::buildTextLayout(data.attributedString, data.paragraphAttributes, width);

  // Fragments carry their own colours as Pango attributes; this is the fallback
  // for text that set none. React Native's default is opaque black.
  GdkRGBA color{0.0F, 0.0F, 0.0F, 1.0F};
  const auto &fragments = data.attributedString.getFragments();
  if (!fragments.empty() && fragments.front().textAttributes.foregroundColor) {
    color = toRgba(colorComponentsFromColor(fragments.front().textAttributes.foregroundColor));
  }

  rn_view_set_text_layout(view, layout, &color);
  g_object_unref(layout);
}

void GtkMountingManager::applyProps(RnView *view, const ShadowView &shadowView) {
  const auto props = std::dynamic_pointer_cast<const ViewProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  if (props->backgroundColor) {
    const GdkRGBA rgba = toRgba(colorComponentsFromColor(props->backgroundColor));
    rn_view_set_background_color(view, TRUE, &rgba);
  } else {
    rn_view_set_background_color(view, FALSE, nullptr);
  }

  rn_view_set_opacity(view, props->opacity);

  // Only a hidden title bar reads this, to find the drag regions an app marked
  // with <TitleBar.DragRegion>. Win32MountingManager does the same with the
  // same comment, which is the point: the two hosts have to agree about what
  // reaches them, or a header drags on one desktop and not the other.
  rn_view_set_native_id(view, props->nativeId.c_str());
  // And remember which view it names, which is what `accessibilityLabelledBy`
  // needs: that prop names other views by their nativeID and nothing else here
  // can look one up. See core/LabelRegistry.h.
  labels_.setNativeId(shadowView.tag, props->nativeId);

  // overflow: 'hidden'. React Native's default is 'visible', which is why the
  // phase-1 screenshots show a child outgrowing its shrunk parent.
  rn_view_set_clips_children(view, props->getClipsContentToBounds() ? TRUE : FALSE);

  rn_view_set_z_index(view, props->zIndex.value_or(0));

  // Radii and widths depend on the frame -- percentage radii, and the clamping
  // that stops opposite corners overlapping -- so they are resolved against the
  // layout metrics rather than read raw.
  const auto borders = props->resolveBorderMetrics(shadowView.layoutMetrics);

  // GRAPHENE_SIZE_INIT is a C99 compound literal, which C++ rejects here.
  const auto cornerSize = [](const auto &corner) {
    return graphene_size_t{static_cast<float>(corner.horizontal), static_cast<float>(corner.vertical)};
  };
  const graphene_size_t radii[4] = {
      cornerSize(borders.borderRadii.topLeft),
      cornerSize(borders.borderRadii.topRight),
      cornerSize(borders.borderRadii.bottomRight),
      cornerSize(borders.borderRadii.bottomLeft),
  };
  rn_view_set_border_radii(view, radii);

  // GTK's border node wants top, right, bottom, left -- the order CSS names
  // them in, and the order React Native's RectangleEdges is not stored in.
  const float widths[4] = {
      static_cast<float>(borders.borderWidths.top),
      static_cast<float>(borders.borderWidths.right),
      static_cast<float>(borders.borderWidths.bottom),
      static_cast<float>(borders.borderWidths.left),
  };
  const auto edgeColor = [](const facebook::react::SharedColor &color) {
    return color ? toRgba(colorComponentsFromColor(color)) : GdkRGBA{0.0F, 0.0F, 0.0F, 0.0F};
  };
  const GdkRGBA colors[4] = {
      edgeColor(borders.borderColors.top),
      edgeColor(borders.borderColors.right),
      edgeColor(borders.borderColors.bottom),
      edgeColor(borders.borderColors.left),
  };
  rn_view_set_borders(view, widths, colors);

  // resolveTransform folds in transformOrigin, but only when one was set: the
  // default anchor is the view's centre, and the widget layer applies that.
  const auto transform = props->resolveTransform(shadowView.layoutMetrics);
  if (transform == facebook::react::Transform::Identity()) {
    rn_view_set_transform(view, nullptr);
  } else {
    // React Native's matrix is CSS matrix3d order, which puts translation at
    // indices 12..14 -- the same slots graphene uses. Rotations are the
    // transpose of each other, which is why the demo checks a rotation on
    // screen rather than trusting the memory layout.
    float values[16];
    for (int i = 0; i < 16; i++) {
      values[i] = static_cast<float>(transform.matrix[static_cast<size_t>(i)]);
    }
    graphene_matrix_t matrix;
    graphene_matrix_init_from_float(&matrix, values);
    rn_view_set_transform(view, &matrix);
  }

  // `backfaceVisibility: 'hidden'`, which stops the back of a card being drawn
  // mirrored halfway through a flip. Whether a transform has turned away is
  // core/Backface.h's to decide, so all three hide the same face.
  //
  // Applied after the transform, because the widget has to know the matrix
  // before it can say which way it is facing.
  rn_view_set_hides_back_face(
      view, props->backfaceVisibility == facebook::react::BackfaceVisibility::Hidden);

  // Hit testing only. `none` becomes GTK's can-target inside the setter; the
  // other two are resolved by GtkTouchDispatcher, which is the only thing that
  // reads them.
  switch (props->pointerEvents) {
    case facebook::react::PointerEventsMode::None:
      rn_view_set_pointer_events(view, RN_POINTER_EVENTS_NONE);
      break;
    case facebook::react::PointerEventsMode::BoxNone:
      rn_view_set_pointer_events(view, RN_POINTER_EVENTS_BOX_NONE);
      break;
    case facebook::react::PointerEventsMode::BoxOnly:
      rn_view_set_pointer_events(view, RN_POINTER_EVENTS_BOX_ONLY);
      break;
    case facebook::react::PointerEventsMode::Auto:
      rn_view_set_pointer_events(view, RN_POINTER_EVENTS_AUTO);
      break;
  }

  // backgroundImage, as far as linear gradients. The angle, the box size and
  // CSS's colour-stop fixup are resolved here through core/Gradients.h, shared
  // with the AppKit host, and the widget is handed two points and a list of
  // stops: the resolution is the specified part and belongs in one place, and
  // the drawing is the toolkit's.
  //
  // Against this mutation's own frame, and applyProps runs on every update, so a
  // resized view gets a resized gradient without the widget knowing anything
  // about angles.
  //
  // A radial gradient is parsed by React Native and dropped here; see
  // backlog/correctness.md.
  {
    const auto &size = shadowView.layoutMetrics.frame.size;
    std::vector<RnLinearGradient> gradients;
    std::vector<std::vector<RnGradientStop>> stops;
    gradients.reserve(props->backgroundImage.size());
    stops.reserve(props->backgroundImage.size());
    for (const auto &image : props->backgroundImage) {
      if (!std::holds_alternative<facebook::react::LinearGradient>(image)) {
        continue;
      }
      const auto &gradient = std::get<facebook::react::LinearGradient>(image);
      const basalt::GradientLine line = basalt::linearGradientLine(
          gradient, static_cast<float>(size.width), static_cast<float>(size.height));
      const auto resolved = basalt::resolveGradientStops(gradient.colorStops, line.length());
      if (resolved.empty()) {
        continue;
      }
      std::vector<RnGradientStop> converted;
      converted.reserve(resolved.size());
      for (const auto &stop : resolved) {
        converted.push_back(RnGradientStop{
            stop.offset, GdkRGBA{stop.red, stop.green, stop.blue, stop.alpha}});
      }
      // Reserved above, so pushing cannot reallocate and the pointer handed to
      // the widget below stays good for the length of this block. The widget
      // copies the stops it is given.
      stops.push_back(std::move(converted));
      gradients.push_back(RnLinearGradient{
          graphene_point_t{line.startX, line.startY},
          graphene_point_t{line.endX, line.endY},
          stops.back().data(),
          static_cast<int>(stops.back().size()),
      });
    }
    rn_view_set_linear_gradients(view, gradients.data(), static_cast<int>(gradients.size()));
  }

  // boxShadow. React Native's BoxShadow carries the six CSS fields and GSK's
  // shadow nodes take the same six, so nothing is converted: the widget layer
  // gets the list as the app wrote it and decides where in the paint order each
  // one goes. A shadow with no colour is one React Native could not parse, and
  // the widget skips it rather than painting black.
  //
  // The list also carries the older iOS shadow props, converted in
  // core/LegacyShadow.h: one mechanism from here down, so a view with
  // `shadowOpacity` and a view with `boxShadow` take the same path.
  {
    const std::vector<facebook::react::BoxShadow> all = basalt::allShadows(*props);
    std::vector<RnBoxShadow> shadows;
    shadows.reserve(all.size());
    for (const auto &shadow : all) {
      shadows.push_back(RnBoxShadow{
          .dx = static_cast<float>(shadow.offsetX),
          .dy = static_cast<float>(shadow.offsetY),
          .blur = static_cast<float>(shadow.blurRadius),
          .spread = static_cast<float>(shadow.spreadDistance),
          .color = shadow.color ? toRgba(colorComponentsFromColor(shadow.color))
                                : GdkRGBA{0.0F, 0.0F, 0.0F, 0.0F},
          .inset = shadow.inset ? TRUE : FALSE,
      });
    }
    rn_view_set_box_shadows(view, shadows.data(), static_cast<int>(shadows.size()));
  }

  // The outline: CSS's, which is drawn outside the box and takes no layout
  // space, so nothing about it touches the frame. React Native carries one
  // width, one colour, one offset and one style for the whole ring, unlike the
  // border's four of each.
  {
    const GdkRGBA colour = props->outlineColor
        ? toRgba(colorComponentsFromColor(props->outlineColor))
        : GdkRGBA{0.0F, 0.0F, 0.0F, 0.0F};
    RnBorderStyle style = RN_BORDER_SOLID;
    switch (props->outlineStyle) {
      case facebook::react::OutlineStyle::Dotted:
        style = RN_BORDER_DOTTED;
        break;
      case facebook::react::OutlineStyle::Dashed:
        style = RN_BORDER_DASHED;
        break;
      case facebook::react::OutlineStyle::Solid:
        break;
    }
    rn_view_set_outline(view,
                        static_cast<float>(props->outlineWidth),
                        static_cast<float>(props->outlineOffset),
                        &colour,
                        style);
  }

  // filter. The arithmetic is core/Filters.h's and is shared with the AppKit
  // host: the list of CSS functions comes to one colour matrix, one blur and one
  // opacity, and the widget layer pushes a node for each.
  //
  // A dropShadow() in the list is reported rather than applied -- it is the one
  // function that does not commute with the others, so folding it in with them
  // would be wrong rather than approximate. Logged once per view that asks,
  // because silence here looks like a working filter.
  {
    const basalt::ResolvedFilters resolved = basalt::resolveFilters(props->filter);
    if (resolved.empty()) {
      rn_view_set_filters(view, nullptr);
    } else {
      RnFilters filters{};
      filters.has_matrix = resolved.hasMatrix ? TRUE : FALSE;
      for (int i = 0; i < 16; i++) {
        filters.matrix[i] = resolved.matrix.m[i];
      }
      for (int i = 0; i < 4; i++) {
        filters.offset[i] = resolved.matrix.offset[i];
      }
      filters.blur_radius = resolved.blurRadius;
      filters.opacity = resolved.opacity;
      rn_view_set_filters(view, &filters);
    }
    if (resolved.hasDropShadow) {
      LOG(WARNING) << "filter: dropShadow() is not applied on this platform; "
                   << "the rest of the filter list is";
    }
  }

  // hitSlop, which grows what a press can land on without moving a pixel. The
  // widget layer answers picking with it; see rn_view_contains.
  {
    const float insets[4] = {
        static_cast<float>(props->hitSlop.top),
        static_cast<float>(props->hitSlop.right),
        static_cast<float>(props->hitSlop.bottom),
        static_cast<float>(props->hitSlop.left),
    };
    rn_view_set_hit_slop(view, insets);
  }

  // The `cursor` style property, as a CSS keyword. GDK's names are CSS's, so the
  // keyword goes straight through; core/CursorNames.h is shared with the AppKit
  // host so the two cannot disagree about what a value is called.
  rn_view_set_cursor(view, basalt::cursorName(props->cursor));

  // `mixBlendMode`, also as a CSS keyword, and shared with the AppKit host for
  // the same reason: GSK's blend modes are CSS's and Core Image's are too, so
  // the keyword is the thing that crosses and each host maps it.
  rn_view_set_blend_mode(view, basalt::blendModeName(props->mixBlendMode));

  // borderStyle. React Native carries one per side and a stroked path carries
  // one dash pattern, so the first side that asks for something other than
  // solid decides the whole outline. A border with different styles per side is
  // rare enough to be worth that; see backlog/correctness.md.
  //
  // From the *resolved* metrics, like the widths and the colours above. The raw
  // `props->borderStyles` is a cascade of optionals with a slot per spelling,
  // and `borderStyle: 'dashed'` sets the `all` slot: reading the four sides
  // directly found nothing, so every dashed border in a React app drew solid
  // while the tests that set the style on the widget by hand passed.
  {
    const auto &styles = borders.borderStyles;
    RnBorderStyle style = RN_BORDER_SOLID;
    const facebook::react::BorderStyle sides[4] = {
        styles.top, styles.right, styles.bottom, styles.left};
    for (const auto &side : sides) {
      if (side == facebook::react::BorderStyle::Dotted) {
        style = RN_BORDER_DOTTED;
        break;
      }
      if (side == facebook::react::BorderStyle::Dashed) {
        style = RN_BORDER_DASHED;
        break;
      }
    }
    rn_view_set_border_style(view, style);
  }
}

void GtkMountingManager::applyLayoutMetrics(RnView *view, const ShadowView &shadowView) {
  const auto &frame = shadowView.layoutMetrics.frame;
  rn_view_set_frame(view,
                    static_cast<float>(frame.origin.x),
                    static_cast<float>(frame.origin.y),
                    static_cast<float>(frame.size.width),
                    static_cast<float>(frame.size.height));

  // display: 'none' keeps the node in the shadow tree but takes it out of
  // layout and painting. gtk_widget_should_layout is false for an invisible
  // widget, so RnLayout skips it too.
  rn_view_set_hidden(view,
                     shadowView.layoutMetrics.displayType == facebook::react::DisplayType::None);

  // TODO(layout): pointScaleFactor matters once fractional scaling is wired up.
}

} // namespace basalt
