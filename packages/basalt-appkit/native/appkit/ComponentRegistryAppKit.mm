// The component registry for the macOS platform. See core/ComponentRegistry.h
// for why this is per-platform rather than shared.
//
// `<View>`, `<Text>`, `<ScrollView>`, `<Image>` and `<TextInput>` -- the
// components an ordinary app is built from, plus the root; and below them the
// controls, which are the ones a view layer needs a real toolkit peer for.
//
// Paragraph arrived with CoreTextLayoutManager.mm, and could not have arrived
// before it: registering it constructs a `TextLayoutManager`, whose stub this
// build drops so a platform's own can be the only definition. That is the link
// error phase 19 found, and it is what kept this list at one entry.
//
// Registering them early would cost nothing visible and be worse than the gap.
// A registered descriptor with no mounting peer does not error -- Fabric builds
// the shadow nodes, lays them out, and the app renders blank rectangles where
// its text should be. Leaving them out means React Native substitutes
// UnimplementedNativeView, which at least says so.

#include "ComponentRegistry.h"
#include "ExpoImageComponent.h"

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>

#include "SkiaPictureViewComponent.h"
#include <react/renderer/components/FBReactNativeSpec/ComponentDescriptors.h>
#include <react/renderer/components/image/ImageComponentDescriptor.h>
#include <react/renderer/components/iostextinput/TextInputComponentDescriptor.h>
#include <react/renderer/components/modal/ModalHostViewComponentDescriptor.h>
#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
#include <react/renderer/components/switch/AppleSwitchComponentDescriptor.h>
#include <react/renderer/components/text/ParagraphComponentDescriptor.h>
#include <react/renderer/components/text/RawTextComponentDescriptor.h>
#include <react/renderer/components/text/TextComponentDescriptor.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>

namespace facebook::react {

ComponentRegistryFactory getDefaultComponentRegistryFactory() {
  return [](const EventDispatcher::Weak &eventDispatcher,
            const std::shared_ptr<const ContextContainer> &contextContainer) {
    static auto providerRegistry = []() {
      auto registry = std::make_shared<ComponentDescriptorProviderRegistry>();
      registry->add(concreteComponentDescriptorProvider<ViewComponentDescriptor>());
      // Text is three descriptors and only one of them mounts. <Text> becomes a
      // Text node, its string a RawText node, and the outermost <Text> a
      // Paragraph that folds the whole subtree into a single AttributedString.
      registry->add(concreteComponentDescriptorProvider<ParagraphComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<TextComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<RawTextComponentDescriptor>());
      // Unlike Image's, ScrollView's descriptor is a bare alias for
      // ConcreteComponentDescriptor -- no manager, no ContextContainer entry.
      // Leaving it out does not fail loudly: the registry silently substitutes
      // UnimplementedNativeView, which has no ScrollViewState, and the symptom
      // is a ScrollView that renders but never scrolls.
      registry->add(concreteComponentDescriptorProvider<ScrollViewComponentDescriptor>());
      // Image's descriptor pulls an ImageManager out of the ContextContainer,
      // creating one if absent. React Native's cxx ImageManager is a stub, so
      // it produces no pixels; AppKitImageLoader does that from the props.
      registry->add(concreteComponentDescriptorProvider<ImageComponentDescriptor>());
      // React Native's *iOS* TextInput, whose C++ is portable: it measures
      // through a TextLayoutManager, which here is the Core Text one.
      // Android's needs fbjni. Its component name is "TextInput".
      registry->add(concreteComponentDescriptorProvider<TextInputComponentDescriptor>());
      // @shopify/react-native-skia's <Canvas>. Registered whether or not the
      // build has Skia in it, for the reason the Expo one below gives: it costs
      // a name in a registry. Without Skia the view mounts and stays empty, and
      // an app that draws has already failed at getEnforcing('RNSkiaModule').
      registry->add(concreteComponentDescriptorProvider<SkiaPictureViewComponentDescriptor>());
      // expo-image's view. Registered whether or not the build has Expo in it:
      // the descriptor is ordinary Fabric C++, and an app that never renders
      // one pays a registry entry. See core/ExpoImageComponent.h.
      registry->add(concreteComponentDescriptorProvider<ExpoImageComponentDescriptor>());

      // --- The controls -----------------------------------------------------
      //
      // Everything above is a box with something drawn in it. These are not:
      // each one is a toolkit control, or a window, and each one needed the
      // view layer to grow a peer before registering it here would have meant
      // anything. Registering a descriptor with nothing to mount it does not
      // error -- Fabric builds the shadow nodes, lays them out, and the app
      // renders blank rectangles -- so this list and the mounting manager's
      // `hasComponent` are two statements of the same fact and have to agree.

      // <ActivityIndicator>. React Native's JavaScript writes the 20pt or 36pt
      // box into the view's style, so the shadow node needs no measurement of
      // its own; what the host draws inside it is a spinner.
      registry->add(concreteComponentDescriptorProvider<ActivityIndicatorViewComponentDescriptor>());
      // <Switch>. The descriptor is React Native's iOS one -- portable C++
      // apart from a `measureContent` that instantiates a UISwitch, which
      // core/DesktopControls.cpp replaces with a constant so a switch is the
      // same size on all three desktops.
      registry->add(concreteComponentDescriptorProvider<SwitchComponentDescriptor>());
      // <Modal>. Not a second window: the shadow node is a root-kind node sized
      // from its state and positioned absolutely, so a modal is an overlay
      // filling the surface -- which is also what it is on the web. The host
      // commits the window's size into that state; see
      // core/DesktopControls.h's modalScreenSizeNeedsUpdate for why it must.
      registry->add(concreteComponentDescriptorProvider<ModalHostViewComponentDescriptor>());
      // <RefreshControl>. The spinner a scroll view shows while it refreshes.
      // The gesture that starts it is in core/PullToRefresh.h, because a
      // desktop has no rubber band to release.
      registry->add(concreteComponentDescriptorProvider<PullToRefreshViewComponentDescriptor>());
      // What React Native substitutes for a component no platform registered.
      // Registering it is what turns "nothing happens" into a mounted view the
      // tree dump can show, and it is what the comments above mean when they
      // say the substitution is silent.
      registry->add(concreteComponentDescriptorProvider<UnimplementedNativeViewComponentDescriptor>());
      // React DevTools' element highlighter. It draws nothing here yet -- the
      // highlight arrives as a command rather than as props -- but a DevTools
      // session mounts one, and an unregistered component in the tree is a
      // blank rectangle in the middle of the app.
      registry->add(concreteComponentDescriptorProvider<DebuggingOverlayComponentDescriptor>());

      return registry;
    }();
    return providerRegistry->createComponentDescriptorRegistry(
        {eventDispatcher, contextContainer, nullptr});
  };
}

} // namespace facebook::react
