// Which components this platform claims.
//
// `getDefaultComponentRegistryFactory()` is declared by ReactCommon and defined
// nowhere in it: each host supplies its own and, in doing so, says what it can
// put on screen. Phase 19 found out the hard way that it cannot be shared --
// `ParagraphComponentDescriptor` constructs a `TextLayoutManager`, so a
// platform without a text engine fails to link rather than failing to render
// text. See `core/ComponentRegistry.h`.
//
// The set here must match what `Win32MountingManager::hasComponent` answers
// for. When they disagree the registry wins, Fabric builds shadow nodes nothing
// can mount, and the app renders blank rectangles rather than reporting
// anything -- which is why the two live in the same directory and should be
// read together.

#include "ComponentRegistry.h"

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
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

      // Image's descriptor pulls an ImageManager out of the ContextContainer,
      // creating one if absent. React Native's cxx ImageManager is a stub, so
      // it produces no pixels; the mounting manager loads them from the props,
      // which is what Android does too.
      registry->add(concreteComponentDescriptorProvider<ImageComponentDescriptor>());

      // ScrollView's content child arrives as "ScrollContentView", which
      // React Native's own registry rewrites to "View" before it reaches here,
      // so it needs no entry of its own. What the descriptor is really for is
      // ScrollViewState: without it the substituted UnimplementedNativeView has
      // nowhere to keep a content offset, so a ScrollView renders and never
      // scrolls -- silently, which is why the absence used to be worth naming.
      registry->add(concreteComponentDescriptorProvider<ScrollViewComponentDescriptor>());

      // React Native's *iOS* TextInput, whose shadow node, props, state and
      // event emitter are pure C++ and measure through a TextLayoutManager --
      // the DirectWrite one, here. Android's variant includes fbjni and calls
      // into a Java FabricUIManager, so it cannot be used outside an Android
      // build. Both other desktops made the same choice; see
      // docs/DECISIONS.md. Its component name is "TextInput", which is what
      // React Native's own componentNameByReactViewName maps
      // RCTSinglelineTextInputView to, so nothing here renames anything.
      registry->add(concreteComponentDescriptorProvider<TextInputComponentDescriptor>());

      // Deliberately absent, and the absence is a decision rather than an
      // oversight: ExpoImage's seam exists in core/ExpoImageComponent.h and its
      // props class is portable, but nothing here mounts one yet.
      //
      // See docs/BACKLOG.md.

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
