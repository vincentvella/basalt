// basalt-core — the macOS host process.
//
// Constructs a ReactHost, loads a script into Hermes, and runs one surface in
// an NSWindow. Everything on screen from here on is produced by Fabric: the
// mutation stream arrives through AppKitMountingManager exactly as it does on iOS
// and Android, and nothing hand-builds a ShadowViewMutation.
//
// The counterpart of gtk/main.cpp, and deliberately the same shape. The pieces
// a host has to supply, and where each comes from:
//
//   IMountingManager         AppKitMountingManager        (this repo)
//   Input                    AppKitTouchDispatcher        (this repo)
//   RunLoopObserverManager   ReactCxxPlatform          (event beat)
//   AnimationChoreographer   AppKitAnimationChoreographer (this repo, display link)
//   ContextContainer         http + websocket client factories, below
//   ComponentRegistryFactory ComponentRegistryAppKit      (via the mounting manager)
//   FontRegistry             FontRegistryCoreText      (this repo, link-time seam)
//
// Threading: AppKit owns this thread. ReactHost spins up its own JS thread and
// every mount is marshalled back here by AppKitMountingManager. Nothing below
// touches a view off the main thread.
//
// What this host can render is what AppKitMountingManager can mount, which today
// is <View>. `e2e/demo.js` -- a surface driven straight through
// nativeFabricUIManager, with no React and no react-native JavaScript -- is
// therefore the bundle this runs, and is the same first light-up the GTK host
// had. A React app needs the JavaScript platform layer to answer to a name
// other than "linux", which is its own piece of work.

#import "AppKitAnimationChoreographer.h"
#import "AppKitMountingManager.h"
#import "CoreTextLayout.h"

#include "FontScaling.h"

#include <react/io/ImageLoaderModule.h>

#include "CrashHandler.h"
#include "AppIdentity.h"
#include "AppPaths.h"
#include "DevMenu.h"
#include "PointerButtons.h"
#include "WindowControl.h"
#include "WindowHost.h"
#include "DialogModule.h"
#include "MenuModel.h"
#include "MenuModule.h"
#include "WindowsModule.h"
#include "TestKeys.h"
#include "TestQuitFile.h"
#ifdef BASALT_HAS_SKIA
#include "AppKitSkiaModule.h"
#endif
#include "TestSettle.h"
#import "AppKitRunLoopObserver.h"
#import "AppKitFocus.h"
#import "AppKitTouchDispatcher.h"
#import "AppKitSnapshot.h"
#import "RnAppKitView.h"

#include "AppearanceModule.h"
#include "BlobModule.h"
#include "CoreModules.h"
#include "LogBoxSurface.h"

#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>
#include "ColorScheme.h"
#include "DragAndDrop.h"

#import "AppKitDropTarget.h"
#import "AppKitTitleBar.h"
#import "AppKitWindowModule.h"
#include "DevBundle.h"

#include <algorithm>
#include <csignal>
#include "ExpoModules.h"
#include "GestureHandlerModule.h"
#include "SafeAreaComponent.h"
#include "ScreensComponent.h"
#include "ReanimatedModule.h"
#include "JsRuntimeAccess.h"
#include "KeyEvents.h"
#include "PackageModules.h"
#include "UIManagerAccess.h"
#include "WorkletsModule.h"
#include "ExpoRuntime.h"
#include "TurboModuleProxy.h"
#include "PlatformConstantsModule.h"
#include "SourceCodeModule.h"
#include "StatusBarModule.h"

#include <glog/logging.h>
#include <jsi/jsi.h>
#include <logger/react_native_log.h>
#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDefaults.h>
#include <react/http/IHttpClient.h>
#include <react/http/IWebSocketClient.h>
#include <react/logging/DefaultOnJsErrorHandler.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/runtime/ReactHost.h>
#include <react/runtime/ReactInstanceConfig.h>
#include <react/utils/ContextContainer.h>
#include <react/utils/RunLoopObserverManager.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

using facebook::react::ContextContainer;
using facebook::react::Float;
using facebook::react::LayoutConstraints;
using facebook::react::LayoutContext;
using facebook::react::LayoutDirection;
using facebook::react::ReactHost;
using facebook::react::ReactInstanceConfig;
using facebook::react::RunLoopObserverManager;
using facebook::react::SurfaceId;

namespace {

// In Fabric a SurfaceId *is* the root shadow node's tag, which is what lets the
// surface root live in the mounting manager's registry like any other view.
constexpr SurfaceId kSurfaceId = 1;

// React Native's error inspector, which is a surface of its own -- registered
// by AppRegistry under the name "LogBox", exactly as an app registers its own
// component. Started on top of the app's when NativeLogBox.show() asks; see
// core/LogBoxSurface.h.
constexpr SurfaceId kLogBoxSurfaceId = 2;

constexpr int kInitialWidth = 900;
constexpr int kInitialHeight = 700;

// The entry point for the *scriptless* mode, where the bundle is a hand-written
// script talking to nativeFabricUIManager directly rather than a React app.
// See e2e/demo.js. Only used when no module name is given.
constexpr const char *kRenderFunctionName = "basaltRender";

// One window, and the surface in it.
//
// A window is a surface: a surface is what has a size, a layout context and a
// root shadow node, and two windows sharing one would be two windows sharing a
// layout. So the id a window is known by *is* its surface id. See
// core/WindowHost.h.
//
// Everything here used to be a field on Host, because there was only ever one.
// What is still on Host is what is genuinely per-process, and what this project
// has not made per-window yet: the title bar and the error inspector are the
// main window's.
struct HostWindow {
  facebook::react::SurfaceId surfaceId{0};
  NSWindow *window{nil};
  RnAppKitView *root{nil};
  // The window's contentView, holding the app's surface root and -- when the
  // error inspector is showing -- a second root above it.
  //
  // A container rather than making the app root the contentView and adding the
  // inspector inside it: `insertRnChild:atIndex:` indexes into `subviews`, so a
  // view the mutation stream did not put there would shift every later Insert
  // by one.
  NSView *container{nil};
  std::unique_ptr<basalt::AppKitTouchDispatcher> touchDispatcher;
  std::unique_ptr<basalt::AppKitFocusManager> focusManager;
  int scaleFactor{1};
};

struct Host {
  // Every window, main one first. Never empty once the app has started.
  std::vector<std::unique_ptr<HostWindow>> windows;

  // The window the app was started in. Shorthand for windows.front(), which is
  // what every part of this host that is not yet per-window uses.
  // An empty window with null fields before there is a real one, rather than
  // dereferencing an empty vector. Several things here run before the window
  // exists -- a repaint request, the spinner timer -- and each one already
  // guards on `window == nullptr`, which is exactly what this keeps true.
  HostWindow &main() {
    static HostWindow none;
    return windows.empty() ? none : *windows.front();
  }

  HostWindow *windowFor(facebook::react::SurfaceId surfaceId) {
    for (const auto &candidate : windows) {
      if (candidate->surfaceId == surfaceId) {
        return candidate.get();
      }
    }
    return nullptr;
  }

  std::shared_ptr<basalt::AppKitMountingManager> mountingManager;
  std::shared_ptr<RunLoopObserverManager> runLoopObserverManager;
  std::shared_ptr<basalt::AppKitAnimationChoreographer> choreographer;
  // The error inspector's own surface root, or nil when it is not showing.
  RnAppKitView *logBoxRoot{nil};
  std::unique_ptr<ReactHost> reactHost;

  CFRunLoopObserverRef runLoopObserver{nullptr};

  std::string bundlePath;
  // Empty means the bundle is a raw Fabric script rather than a React app.
  std::string moduleName;
  // Metro's entry point, without the extension. Only meaningful in dev mode.
  std::string sourcePath;
  bool surfaceStarted{false};
  // Whether this run is a development one, which is the only thing that decides
  // whether Cmd+D opens anything. Kept because the ReactInstanceConfig it came
  // from is not.
  bool devMode{false};
};

Host gHost;

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

// ReactHost's Logger is where console.log and RN's own logging come out.
void logToConsole(const std::string &message, unsigned int logLevel) {
  switch (logLevel) {
    case ReactNativeLogLevelFatal:
    case ReactNativeLogLevelError:
      NSLog(@"[js error] %s", message.c_str());
      break;
    case ReactNativeLogLevelWarning:
      NSLog(@"[js warn] %s", message.c_str());
      break;
    default:
      NSLog(@"[js] %s", message.c_str());
      break;
  }
}

// ---------------------------------------------------------------------------
// Layout constraints
// ---------------------------------------------------------------------------

// Minimum == maximum pins the root to the window's content box, which is what
// a full-window surface wants: the root fills, and flex children divide it.
LayoutConstraints constraintsFor(int width, int height) {
  // Fully qualified: Carbon's MacTypes.h defines a `Size` of its own -- a bare
  // `long` -- and AppKit pulls it in, so an unqualified name here resolves to
  // something that is not a size at all.
  const facebook::react::Size size{static_cast<Float>(width), static_cast<Float>(height)};
  return LayoutConstraints{
      .minimumSize = size,
      .maximumSize = size,
      .layoutDirection = LayoutDirection::LeftToRight,
  };
}

LayoutContext layoutContextFor(int scaleFactor) {
  return LayoutContext{.pointScaleFactor = static_cast<Float>(scaleFactor)};
}

// ---------------------------------------------------------------------------
// Calling into JS
// ---------------------------------------------------------------------------

// Runs on the JS thread. `step` lets the script commit a second, different tree
// so an Update/Remove diff can be seen coming out of Fabric rather than only an
// initial mount.
void callRenderFunction(int step) {
  if (gHost.reactHost == nullptr) {
    return;
  }
  gHost.reactHost->runOnRuntimeScheduler([step](facebook::jsi::Runtime &runtime) {
    try {
      auto value = runtime.global().getProperty(runtime, kRenderFunctionName);
      if (!value.isObject() || !value.getObject(runtime).isFunction(runtime)) {
        NSLog(@"script defines no global %s()", kRenderFunctionName);
        return;
      }
      value.getObject(runtime).getFunction(runtime).call(
          runtime,
          facebook::jsi::Value(static_cast<int>(kSurfaceId)),
          facebook::jsi::Value(step));
    } catch (const facebook::jsi::JSError &error) {
      NSLog(@"%s() threw: %s\n%s",
            kRenderFunctionName,
            error.getMessage().c_str(),
            error.getStack().c_str());
    } catch (const std::exception &error) {
      NSLog(@"%s() failed: %s", kRenderFunctionName, error.what());
    }
  });
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

// Feature flags this platform states explicitly. Anything not listed keeps
// React Native's own default.
class DesktopFeatureFlags : public facebook::react::ReactNativeFeatureFlagsDefaults {
 public:
  bool enableBridgelessArchitecture() override {
    return true;
  }
};

// TurboModules this platform supplies itself. ReactCxxTurboModuleProvider
// consults these before its own, so naming a module it also provides replaces
// it. All three come from core/ and are shared with the GTK host.
facebook::react::TurboModuleProviders makeTurboModuleProviders(
    std::string scriptURL,
    bool devMode,
    std::weak_ptr<basalt::AppKitImageLoader> imageLoader) {
  facebook::react::TurboModuleProviders providers;
  // Whatever the discovered capability packages supply, first: a package naming
  // a module this host also provides is replacing it deliberately, and the same
  // rule already applies to this list against ReactCxxPlatform's own. Answers
  // nullptr for every name when no package supplies one, which is most builds.
  // See core/PackageModules.h.
  providers.emplace_back(basalt::packageTurboModule);
  providers.emplace_back(
      [scriptURL = std::move(scriptURL), devMode, imageLoader = std::move(imageLoader)](
          const std::string &name,
         const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker)
          -> std::shared_ptr<facebook::react::TurboModule> {
        if (name == facebook::react::PlatformConstantsModule::kModuleName) {
          return std::make_shared<basalt::DesktopPlatformConstantsModule>(jsInvoker);
        }
        // `Image.getSize` and `Image.prefetch`. Upstream builds this module
        // with no loader at all and offers no way to supply one, so the answer
        // is to build it here with the loader this host already has -- which
        // is also the one holding the decoded images, so a size asked for
        // something on screen costs nothing. See docs/backlog/upstream.md.
        if (name == facebook::react::ImageLoaderModule::kModuleName) {
          return std::make_shared<facebook::react::ImageLoaderModule>(jsInvoker, imageLoader);
        }
        if (name == basalt::DesktopAppModule::kModuleName) {
          return std::make_shared<basalt::DesktopAppModule>(jsInvoker);
        }
        if (name == basalt::DesktopSafeAreaModule::kModuleName) {
          return std::make_shared<basalt::DesktopSafeAreaModule>(jsInvoker);
        }
        if (name == basalt::DesktopScreensModule::kModuleName) {
          return std::make_shared<basalt::DesktopScreensModule>(jsInvoker);
        }
        if (name == basalt::DesktopAppearanceModule::kModuleName) {
          return std::make_shared<basalt::DesktopAppearanceModule>(jsInvoker);
        }
        if (name == basalt::DesktopBlobModule::kModuleName) {
          return std::make_shared<basalt::DesktopBlobModule>(jsInvoker);
        }
        if (name == basalt::DesktopFileReaderModule::kModuleName) {
          return std::make_shared<basalt::DesktopFileReaderModule>(jsInvoker);
        }
        if (name == basalt::DesktopClipboardModule::kModuleName) {
          return std::make_shared<basalt::DesktopClipboardModule>(jsInvoker);
        }
        if (name == basalt::DesktopVibrationModule::kModuleName) {
          return std::make_shared<basalt::DesktopVibrationModule>(jsInvoker);
        }
        if (name == basalt::DesktopAlertModule::kModuleName) {
          return std::make_shared<basalt::DesktopAlertModule>(jsInvoker);
        }
        if (name == basalt::DesktopLinkingModule::kModuleName) {
          return std::make_shared<basalt::DesktopLinkingModule>(jsInvoker);
        }
        if (name == basalt::DesktopShareModule::kModuleName) {
          return std::make_shared<basalt::DesktopShareModule>(jsInvoker);
        }
        // `Settings`, kept in a file beside the app's identity rather than in
        // this desktop's own settings store; see core/SettingsStore.h.
        if (name == basalt::DesktopSettingsManagerModule::kModuleName) {
          return std::make_shared<basalt::DesktopSettingsManagerModule>(jsInvoker);
        }
        // `ActionSheetIOS`, which is a popup menu here; see core/ActionSheet.h.
        if (name == basalt::DesktopActionSheetModule::kModuleName) {
          return std::make_shared<basalt::DesktopActionSheetModule>(jsInvoker);
        }
        // The native file dialogs, which React Native has no API for and which
        // are the first thing a desktop app reaches for. See core/DialogModule.h.
        if (name == basalt::DesktopDialogModule::kModuleName) {
          return std::make_shared<basalt::DesktopDialogModule>(jsInvoker);
        }
        // The application menu, which on macOS is also what makes Cmd-C reach
        // a text field. See core/MenuModel.h.
        if (name == basalt::DesktopMenuModule::kModuleName) {
          return std::make_shared<basalt::DesktopMenuModule>(jsInvoker);
        }
        // More than one window, which is more than one surface. See
        // core/WindowHost.h.
        if (name == basalt::DesktopWindowsModule::kModuleName) {
          return std::make_shared<basalt::DesktopWindowsModule>(jsInvoker);
        }
        if (name == basalt::DesktopI18nManagerModule::kModuleName) {
          return std::make_shared<basalt::DesktopI18nManagerModule>(jsInvoker);
        }
        if (name == basalt::DesktopAccessibilityManagerModule::kModuleName) {
          return std::make_shared<basalt::DesktopAccessibilityManagerModule>(jsInvoker);
        }
        if (name == basalt::DesktopAccessibilityInfoModule::kModuleName) {
          return std::make_shared<basalt::DesktopAccessibilityInfoModule>(jsInvoker);
        }
        if (name == basalt::DesktopToastModule::kModuleName) {
          return std::make_shared<basalt::DesktopToastModule>(jsInvoker);
        }
        // react-native-gesture-handler's module. Offered whether or not the app
        // has the library: it costs a name comparison, and the alternative is a
        // host that has to be rebuilt to run an app that uses gestures.
        if (name == basalt::DesktopGestureHandlerModule::kModuleName) {
          return std::make_shared<basalt::DesktopGestureHandlerModule>(jsInvoker);
        }
#ifdef BASALT_HAS_SKIA
        // @shopify/react-native-skia, when the build was pointed at an app that
        // has it. Its `install` is what puts Skia's whole JavaScript API on the
        // global object; see appkit/AppKitSkiaModule.h.
        if (name == basalt::AppKitSkiaModule::kModuleName) {
          return std::make_shared<basalt::AppKitSkiaModule>(jsInvoker);
        }
#endif
#ifdef BASALT_HAS_WORKLETS
        // react-native-worklets, when the build was pointed at one. Reanimated
        // is built on it; see core/WorkletsModule.h.
        if (name == basalt::DesktopWorkletsModule::kModuleName) {
          return std::make_shared<basalt::DesktopWorkletsModule>(jsInvoker);
        }
#endif
#ifdef BASALT_HAS_REANIMATED
        if (name == basalt::DesktopReanimatedModule::kModuleName) {
          return std::make_shared<basalt::DesktopReanimatedModule>(jsInvoker);
        }
#endif
        // Only outside dev mode. ReactCxxPlatform provides a real DevSettings
        // when a dev server exists, and this provider is consulted first -- so
        // offering ours unconditionally would shadow the one that works.
        if (!devMode && name == basalt::DesktopDevSettingsModule::kModuleName) {
          return std::make_shared<basalt::DesktopDevSettingsModule>(jsInvoker);
        }
        if (name == basalt::DesktopStatusBarModule::kModuleName) {
          return std::make_shared<basalt::DesktopStatusBarModule>(jsInvoker);
        }
        // The title bar. Reaches the window through the one AppKitTitleBar,
        // which the host attached at startup -- the same arrangement the
        // Windows host has, and for the same reason: a module cannot be handed
        // a window it is created before.
        if (name == basalt::AppKitWindowModule::kModuleName) {
          return std::make_shared<basalt::AppKitWindowModule>(jsInvoker);
        }
        // Only outside dev mode, for the same reason DevSettings is above.
        //
        // This module's scriptURL is what HMRClient registers with Metro as its
        // entry point, and Metro resolves that URL to a *graph*. The URL
        // synthesised here is not the one DevServerHelper actually fetched --
        // that one carries lazy, minify, runModule and app -- so it names a
        // graph nobody built, Metro answers GraphNotFoundError, and Fast
        // Refresh silently never starts. ReactCxxPlatform's own SourceCode
        // module reports the URL the bundle really came from, which is the one
        // whose graph exists.
        if (!devMode && name == basalt::DesktopSourceCodeModule::kModuleName) {
          return std::make_shared<basalt::DesktopSourceCodeModule>(jsInvoker, scriptURL);
        }
        return nullptr;
      });
  return providers;
}

// Whether the window's first responder is one of this project's views, or sits
// inside one.
//
// Inside one matters: a focused `<TextInput>` is an NSTextField the mounting
// manager put *inside* an RnAppKitView, so asking only whether the responder is
// an RnAppKitView would answer no for every text field and let a window-level
// shortcut eat what the field was typing.
static BOOL basaltViewInResponderChain(NSWindow *window) {
  NSResponder *responder = window.firstResponder;
  if (![responder isKindOfClass:[NSView class]]) {
    return NO;
  }
  for (NSView *view = (NSView *)responder; view != nil; view = view.superview) {
    if ([view isKindOfClass:[RnAppKitView class]]) {
      return YES;
    }
  }
  return NO;
}

// Runs against the JavaScript runtime before the bundle is evaluated, which is
// the only moment early enough for what goes in here.
void installBindings(facebook::jsi::Runtime &runtime) {
  // Before Expo's, and before anything else that might look a module up: this is
  // what makes TurboModuleRegistry's first question answerable on a bridgeless
  // runtime. See core/TurboModuleProxy.h.
  basalt::installTurboModuleProxy(runtime);
  basalt::installExpoRuntime(runtime);
}

std::shared_ptr<const ContextContainer> makeContextContainer() {
  auto contextContainer = std::make_shared<ContextContainer>();

  // ReactHost throws without these two. Both are real implementations --
  // curl-backed http from core/HttpClient.cpp, and React Native's own
  // boost::beast websocket client -- and neither is about a toolkit, which is
  // why the macOS host gets them for free.
  contextContainer->insert(facebook::react::HttpClientFactoryKey,
                           facebook::react::getHttpClientFactory());
  contextContainer->insert(facebook::react::WebSocketClientFactoryKey,
                           facebook::react::getWebSocketClientFactory());

  // MessageQueueThreadFactoryKey is deliberately left unset: ReactHost then
  // installs MessageQueueThreadImpl, a real threaded queue.
  return contextContainer;
}

// BASALT_DUMP_TREE: write the view tree to a file on the way out, so an
// automated run can assert on what React actually produced rather than on a
// screenshot. The same idea as BASALT_DUMP_TREE, and the prefixes should
// eventually be one.
// BASALT_DUMP_MENU: write the application menu that is actually installed to a
// file on the way out.
//
// Read back from AppKit rather than from the model that was sent, which is the
// point: it says a description became a real NSMenu with the shortcuts the
// platform attached to its roles -- and it is the only way an automated run can
// see a menu bar at all, since a menu cannot be opened without a person.
void dumpMenuIfRequested() {
  const char *path = getenv("BASALT_DUMP_MENU");
  if (path == nullptr) {
    return;
  }
  const std::string described = basalt::describeApplicationMenu();
  NSString *text = [NSString stringWithUTF8String:described.c_str()];
  NSError *error = nil;
  [text != nil ? text : @"" writeToFile:[NSString stringWithUTF8String:path]
                              atomically:YES
                                encoding:NSUTF8StringEncoding
                                   error:&error];
  if (error != nil) {
    NSLog(@"could not write the menu dump: %@", error.localizedDescription);
  }
}

void dumpTreeIfRequested() {
  const char *path = getenv("BASALT_DUMP_TREE");
  if (path == nullptr || gHost.main().root == nil) {
    return;
  }
  NSError *error = nil;
  NSMutableString *description = [[gHost.main().root describeTree] mutableCopy];
  // Every other window, each under a header naming its surface. Appended rather
  // than merged for the same reason the inspector is: they are separate trees
  // on screen, and nesting one inside another would say something untrue.
  for (const auto &other : gHost.windows) {
    if (other->surfaceId == kSurfaceId || other->root == nil) {
      continue;
    }
    [description appendFormat:@"--- window %d ---\n", (int)other->surfaceId];
    [description appendString:[other->root describeTree]];
  }
  // The error inspector is a second surface with a root of its own, so it is
  // invisible to a dump of the app's. Appended rather than merged, because the
  // two are siblings on screen and nesting one inside the other would say
  // something untrue about the tree.
  if (gHost.logBoxRoot != nil) {
    [description appendString:@"--- LogBox ---\n"];
    [description appendString:[gHost.logBoxRoot describeTree]];
  }
  if (![description writeToFile:[NSString stringWithUTF8String:path]
                     atomically:YES
                       encoding:NSUTF8StringEncoding
                          error:&error]) {
    NSLog(@"could not write %s: %@", path, error);
  } else {
    NSLog(@"wrote view tree to %s", path);
  }
}

// Starts or stops React Native's error inspector.
//
// It is `LogBoxInspectorContainer`, registered by AppRegistry under the name
// "LogBox" exactly as an app registers its own component -- so this is a second
// surface rather than an overlay this host draws, and everything in it is React
// Native's own JavaScript. See core/LogBoxSurface.h.
//
// Started and stopped rather than kept and hidden: a surface that exists is a
// React tree that renders and re-renders, and the inspector is showing for a
// vanishingly small part of a session.
void hideLogBoxSurface() {
  if (gHost.reactHost == nullptr || gHost.logBoxRoot == nil) {
    return;
  }
  gHost.reactHost->stopSurface(kLogBoxSurfaceId);
  [gHost.logBoxRoot removeFromSuperview];
  gHost.mountingManager->destroySurfaceRoot(kLogBoxSurfaceId);
  gHost.logBoxRoot = nil;
}

void showLogBoxSurface(const std::string &appKey) {
  if (gHost.reactHost == nullptr || gHost.main().container == nil || gHost.logBoxRoot != nil) {
    return;
  }

  const NSSize size = gHost.main().container.bounds.size;
  const int width = (int)size.width;
  const int height = (int)size.height;

  gHost.logBoxRoot = gHost.mountingManager->createSurfaceRoot(kLogBoxSurfaceId);
  [gHost.logBoxRoot setRnFrameX:0 y:0 width:width height:height];
  // Added last, so it is above the app. AppKit paints subviews in order and
  // hit-tests them in reverse, which is what makes the inspector take the
  // presses that would otherwise reach the app behind it.
  [gHost.main().container addSubview:gHost.logBoxRoot];

  gHost.reactHost->startSurface(kLogBoxSurfaceId,
                                appKey,
                                folly::dynamic::object(),
                                constraintsFor(width, height),
                                layoutContextFor(gHost.main().scaleFactor));
}

// Closes any modal sheet before the application tries to go away.
//
// Not a testing concern, or not only one: `[NSApp terminate:]` does nothing at
// all while a sheet is up, so an app showing an `Alert.alert()` when a quit
// arrives simply does not quit -- no dialog dismissed, no callback, no exit.
// It looked like a hung automated run, which is how it was found, and it is the
// same hang a person would get from Cmd-Q.
//
// Ending the sheet runs its completion handler, so whatever was waiting on the
// answer is told the dialog closed rather than left holding a promise forever.
void endAnyOpenSheets() {
  for (NSWindow *window in NSApp.windows) {
    // A copy, because endSheet: mutates the array being walked.
    for (NSWindow *sheet in [window.sheets copy]) {
      [window endSheet:sheet];
    }
  }
}

void shutdown() {
  // Before the surface stops, which tears the tree down.
  dumpTreeIfRequested();
  dumpMenuIfRequested();

  if (gHost.choreographer != nullptr) {
    gHost.choreographer->detach();
  }
  // Stop feeding the beat before the manager it points at is released.
  basalt::removeRunLoopObserver(gHost.runLoopObserver);
  gHost.runLoopObserver = nullptr;

  // Every window, not only the app's own -- which is what this said when there
  // was only ever one, and did not say when there stopped being.
  //
  // A dispatcher left alive past `mountingManager.reset()` below would read an
  // event emitter out of a manager that has been freed, for any event AppKit
  // still delivers while the windows come down. Clearing the back pointer in
  // the dispatcher's own destructor does not cover this: the dispatcher is
  // still there, and it is the manager that has gone.
  for (const auto &window : gHost.windows) {
    window->focusManager.reset();
    window->touchDispatcher.reset();
  }
  gHost.logBoxRoot = nil;
  // Before the host goes: work posted to a host on its way out must not run.
  basalt::setRuntimeRunner(nullptr);
  if (gHost.reactHost != nullptr) {
    // Surfaces must stop before the host goes away, or teardown asserts.
    gHost.reactHost->stopAllSurfaces();
    gHost.surfaceStarted = false;
    // Destroying the host joins the JS thread, so it has to happen while the
    // mounting manager it holds is still alive.
    gHost.reactHost.reset();
  }
  gHost.choreographer.reset();
  gHost.runLoopObserverManager.reset();
  // The window records before the manager that made their roots: each holds a
  // borrowed root, and a record outliving the manager is the same hazard.
  gHost.windows.clear();
  gHost.mountingManager.reset();
}

} // namespace

// The window's delegate, and the application's. Resizing has to reach the
// surface or the root keeps the size it started with while the window changes
// around it; quitting has to run shutdown() or the JS thread is dropped rather
// than joined.
@interface RnAppKitHostDelegate : NSObject <NSWindowDelegate, NSApplicationDelegate>
@end

@implementation RnAppKitHostDelegate

// A move changes the bounds `useWindow()` reports and nothing else, so unlike a
// resize there is no surface to reconstrain -- which is exactly why it needs
// its own handler: the resize one would never run, and `center()` would look
// like it had done nothing.
// Closed by the person rather than by the app: its own close button, or the
// window manager. Without this the host keeps a record whose NSWindow is gone
// and the `<Window>` that opened it goes on believing it is open.
//
// Only for a window an app opened: the main window closing ends the process,
// which applicationShouldTerminateAfterLastWindowClosed already arranges.
// Somebody is trying to close a window: its close button, Cmd-W, or the window
// manager. AppKit asks before it acts, which is exactly the question an app with
// unsaved work wants to answer -- so a window that asked to be asked refuses
// here and tells the app instead.
//
// Every window, including the app's own. What this does *not* cover is Cmd-Q:
// terminating goes through applicationShouldTerminate:, which never asks a
// window whether it minds -- that is the handler further down.
- (BOOL)windowShouldClose:(NSWindow *)sender {
  for (const auto &candidate : gHost.windows) {
    if (candidate->window != sender) {
      continue;
    }
    if (basalt::hostWindowCloseIntercepted(candidate->surfaceId)) {
      // What happens next is the app's decision, and if that is "go ahead" it
      // calls close() itself. See core/WindowHost.h for why the answer has to
      // be available before the question is asked.
      basalt::hostWindowCloseRequested(candidate->surfaceId);
      return NO;
    }
    break;
  }
  return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
  for (const auto &candidate : gHost.windows) {
    if (candidate->window != notification.object || candidate->surfaceId == kSurfaceId) {
      continue;
    }
    // Tell JavaScript, then take the window down the same way an app closing it
    // would -- so there is one teardown path rather than two.
    basalt::hostWindowClosed(candidate->surfaceId);
    basalt::closeHostWindow(candidate->surfaceId);
    return;
  }
}

- (void)windowDidMove:(NSNotification *)notification {
  (void)notification;
  basalt::notifyWindowBoundsChanged();
}

- (void)windowDidResize:(NSNotification *)notification {
  if (!gHost.surfaceStarted) {
    return;
  }
  // Which window resized, which is the whole of what more than one changes
  // here. Constraining `kSurfaceId` from every window would lay the *first*
  // window's tree out to the second window's size, and the symptom would be a
  // first window whose content jumped whenever a second one was dragged.
  HostWindow *resized = nullptr;
  for (const auto &candidate : gHost.windows) {
    if (candidate->window == notification.object) {
      resized = candidate.get();
      break;
    }
  }
  if (resized == nullptr) {
    return;
  }

  const NSSize size = resized->window.contentView.bounds.size;
  const int width = (int)size.width;
  const int height = (int)size.height;
  if (width <= 0 || height <= 0) {
    return;
  }
  [resized->root setRnFrameX:0 y:0 width:width height:height];
  gHost.reactHost->setSurfaceConstraints(
      resized->surfaceId, constraintsFor(width, height), layoutContextFor(resized->scaleFactor));

  // A <Modal> is sized from its own shadow-node state rather than from a style,
  // and React Native's C++ platform answers "what size is the screen" with
  // zero. Told here, where the window's size is already being handed to Fabric.
  gHost.mountingManager->setSurfaceSize((float)width, (float)height);
  // The window moved or was resized, which `useWindow()` hands an app as live
  // bounds. Here rather than in a watcher of its own: this is already the one
  // place that learns it.
  basalt::notifyWindowBoundsChanged();

  // The inspector covers the main window, so it resizes with it -- and only
  // with it: an error is about the app rather than about a window.
  if (gHost.logBoxRoot != nil && resized->surfaceId == kSurfaceId) {
    [gHost.logBoxRoot setRnFrameX:0 y:0 width:width height:height];
    gHost.reactHost->setSurfaceConstraints(
        kLogBoxSurfaceId, constraintsFor(width, height), layoutContextFor(gHost.main().scaleFactor));
  }
}

// The backing scale factor changes when a window moves between a Retina display
// and an external one. Yoga rounds to physical pixels, so telling it the wrong
// factor puts half-pixel seams between adjacent views.
- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
  (void)notification;
  if (!gHost.surfaceStarted) {
    return;
  }
  gHost.main().scaleFactor = (int)gHost.main().window.backingScaleFactor;
  const NSSize size = gHost.main().window.contentView.bounds.size;
  gHost.reactHost->setSurfaceConstraints(kSurfaceId,
                                         constraintsFor((int)size.width, (int)size.height),
                                         layoutContextFor(gHost.main().scaleFactor));
}

// Set by the BASALT_QUIT_AFTER_MS timer, read by applicationShouldTerminate:.
// A file-scope bool rather than anything in core: it is about this host's
// automation and means nothing to the platform.
static bool gQuittingForTest = false;

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
  (void)sender;
  return YES;
}

// Somebody is trying to quit: Cmd-Q, the Quit menu item, or a logout. AppKit
// asks the delegate and asks no window, which is why guarding every window is
// not enough -- see core/WindowHost.h.
//
// `NSTerminateCancel` rather than `NSTerminateLater`. Later is the shape this
// looks like it wants, and it obliges the app to call
// `replyToApplicationShouldTerminate:` -- from the JavaScript thread, having
// woken React, decided, and hopped back -- with AppKit holding a modal run
// loop in the meantime. An app that is slow to answer, or that never answers
// because its handler threw, leaves a process that cannot be quit by any
// means short of kill. Cancel is a complete answer now, and the app quits
// itself when it is ready, which is exactly the arrangement the window half
// already uses.
// A monitor plugged in, unplugged, or rearranged -- and also a resolution
// change or the menu bar moving to another screen, which is why the name says
// "screen parameters" rather than "screens".
- (void)applicationDidChangeScreenParameters:(NSNotification *)notification {
  (void)notification;
  basalt::notifyDisplaysChanged();
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
  (void)sender;
  // The automation timer is not refusable. BASALT_QUIT_AFTER_MS quits by
  // calling terminate:, which arrives here -- so a demo that registers a quit
  // handler would refuse the harness, and every scenario that runs it would
  // hang until its own timeout and report something other than what it is
  // testing. The scenario for this feature is exactly such a demo.
  if (gQuittingForTest) {
    return NSTerminateNow;
  }
  if (!basalt::hostQuitIntercepted()) {
    return NSTerminateNow;
  }
  basalt::hostQuitRequested();
  return NSTerminateCancel;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  shutdown();
}

@end

// The application menu an app gets before it has asked for one.
//
// Not a courtesy, and not only about Quit. AppKit gives the main menu every key
// event before the responder chain sees it, so a host with no Edit menu has no
// working Cmd-C: `copy:` never reaches the field editor of whichever text field
// has focus. This host had exactly one Quit item until core/MenuModel.h
// existed, and copy, cut, paste, undo and select-all did nothing in every
// <TextInput> on macOS as a result.
//
// An empty model is the default; see appkit/AppKitMenuBar.mm for what it
// contains and why each item is in it.
static void installMainMenu(void) {
  basalt::setApplicationMenu(basalt::MenuModel{}, nullptr);
}

// Makes a window, a surface root for it, and the input that drives them.
//
// Called for the main window and for every one an app opens afterwards, which
// is the point: the second window is not a special case of the first, it is the
// same function with a different surface id. See core/WindowHost.h.
//
// The window is not shown and the surface is not started here; both are the
// caller's business.
HostWindow *createHostWindow(facebook::react::SurfaceId surfaceId,
                             NSString *title,
                             int width,
                             int height,
                             id<NSWindowDelegate> delegate) {
  // The first window through here is the app's own, and the title bar belongs
  // to it alone: it is a process-wide seam -- one title, one style -- and
  // making it per-window is its own piece of work. See docs/BACKLOG.md.
  const bool isMainWindow = gHost.windows.empty();

  auto owned = std::make_unique<HostWindow>();
  HostWindow *made = owned.get();
  made->surfaceId = surfaceId;

  made->window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, width, height)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  made->window.title = title;
  made->window.delegate = delegate;
  // Released when closed would free the window out from under this record. The
  // host owns every window it makes and closes them through closeHostWindow.
  made->window.releasedWhenClosed = NO;
  if (isMainWindow) {
    // The title bar talks to this window from here on; see AppKitTitleBar.h.
    basalt::titleBar().attach(made->window);
  }
  [made->window center];
  made->scaleFactor = (int)made->window.backingScaleFactor;

  // Fabric emits no Create for a surface root -- the root shadow node is the
  // base of every diff, so it has to exist before the surface starts.
  made->root = gHost.mountingManager->createSurfaceRoot(surfaceId);
  [made->root setRnFrameX:0 y:0 width:width height:height];
  made->container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
  made->container.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [made->container addSubview:made->root];
  made->window.contentView = made->container;

  // Input, per window. Attached to this window's root, which is where its hit
  // testing starts -- a second window with the first one's dispatcher would
  // deliver every press to the wrong tree.
  made->touchDispatcher =
      std::make_unique<basalt::AppKitTouchDispatcher>(gHost.mountingManager.get(), made->root);
  // The keyboard half: Tab reaching a <Pressable>, and Enter activating it.
  made->focusManager =
      std::make_unique<basalt::AppKitFocusManager>(gHost.mountingManager.get(), made->root);

  gHost.windows.push_back(std::move(owned));
  return made;
}

namespace basalt {

// core/WindowHost.h, on macOS.
//
// The surface id is allocated here rather than by JavaScript: it is Fabric's
// number, the mounting manager keys its roots by it, and two windows racing to
// pick one would be two windows sharing a root. Above the ids this host
// reserves -- the app's surface is 1 and the error inspector's is 2.
facebook::react::SurfaceId openHostWindow(const NewWindowOptions &options) {
  if (gHost.reactHost == nullptr || !gHost.surfaceStarted || options.component.empty() ||
      gHost.windows.empty()) {
    return 0;
  }

  @autoreleasepool {
    static facebook::react::SurfaceId nextSurfaceId = kLogBoxSurfaceId + 1;
    const facebook::react::SurfaceId surfaceId = nextSurfaceId++;

    const int width = (int)options.width;
    const int height = (int)options.height;
    NSString *title = [NSString stringWithUTF8String:options.title.c_str()];
    // The same delegate the main window has: it answers windowDidResize: and
    // windowDidMove:, both of which every window needs.
    HostWindow *made = createHostWindow(surfaceId,
                                        title != nil ? title : @"",
                                        width,
                                        height,
                                        (id<NSWindowDelegate>)NSApp.delegate);

    gHost.reactHost->startSurface(surfaceId,
                                  options.component,
                                  options.props,
                                  constraintsFor(width, height),
                                  layoutContextFor(made->scaleFactor));
    [made->window makeKeyAndOrderFront:nil];
    NSLog(@"opened window %d for module %s", (int)surfaceId, options.component.c_str());
    return surfaceId;
  }
}

// Ends the application.
//
// `terminate:` rather than `exit()`: it runs
// `applicationWillTerminate:`, which is where shutdown() is, and it is what
// the Quit menu item would have done had the app not refused it. The refusal
// is the app's own flag, and an app calling this has already cleared it --
// see the hook in src/quitRequest.ts -- so this is not refused a second time.
void quitHost() {
  [NSApp terminate:nil];
}

void closeHostWindow(facebook::react::SurfaceId surfaceId) {
  // The main window is not closed this way: destroying the surface an app is
  // running in is not the same thing as closing its window, and an app that
  // means the second should say so through `close()` on the window itself.
  if (gHost.reactHost == nullptr || surfaceId == kSurfaceId || surfaceId == kLogBoxSurfaceId) {
    return;
  }
  if (gHost.windowFor(surfaceId) == nullptr) {
    return;
  }

  // Stopping a surface unmounts its React tree, which produces one last
  // transaction of Remove and Delete mutations. Those arrive on the main thread
  // afterwards, and the views they name have to still be there when they do.
  gHost.reactHost->stopSurface(surfaceId);

  // So the window goes a round trip later: out to the JavaScript thread, which
  // is where the teardown runs, and back to this one, which is where its
  // mutations are applied. Both queues are ordered, so anything the stop
  // produced is ahead of this.
  gHost.reactHost->runOnRuntimeScheduler([surfaceId](facebook::jsi::Runtime &) {
    basalt::postToUiThread([surfaceId] {
      HostWindow *going = gHost.windowFor(surfaceId);
      if (going == nullptr) {
        return;
      }
      // The dispatchers before the views they hold: both keep a borrowed root.
      going->focusManager.reset();
      going->touchDispatcher.reset();
      going->window.delegate = nil;
      [going->window close];
      gHost.mountingManager->destroySurfaceRoot(surfaceId);

      for (auto it = gHost.windows.begin(); it != gHost.windows.end(); ++it) {
        if (it->get() == going) {
          gHost.windows.erase(it);
          break;
        }
      }
      NSLog(@"closed window %d", (int)surfaceId);
    });
  });
}

std::vector<facebook::react::SurfaceId> hostWindows() {
  std::vector<facebook::react::SurfaceId> open;
  open.reserve(gHost.windows.size());
  for (const auto &candidate : gHost.windows) {
    open.push_back(candidate->surfaceId);
  }
  return open;
}

} // namespace basalt

int main(int argc, const char *argv[]) {

  // What `initialWindowMetrics` answers, before any JavaScript asks. The window
  // is this size until something resizes it, and the provider's own layout
  // corrects it after that. See core/SafeAreaComponent.h.
  basalt::setInitialWindowFrame(kInitialWidth, kInitialHeight);
  // glog, before anything logs through it.
  //
  // Without this every run opens with "WARNING: Logging before
  // InitGoogleLogging() is written to STDERR" -- glog saying it has not been
  // told where to write, in a line that reads like something went wrong on
  // start-up. It goes to stderr either way; this only stops it apologising for
  // it. React Native's C++ logs through glog throughout, so the first message
  // arrives before anything this project writes.
  FLAGS_logtostderr = true;
  google::InitGoogleLogging(argv[0]);
  // Straight after, so a crash between here and the first window still
  // says where. See core/CrashHandler.h.
  basalt::installCrashHandler();

  @autoreleasepool {
    gHost.bundlePath = argc > 1 ? argv[1] : "build/main.jsbundle.js";

  // A URL the desktop launched this with, for `Linking.getInitialURL()`. Any
  // argument after the bundle and the module that carries a scheme: a
  // `.desktop` entry's `%%u`, a registered scheme, a shell association. Matched
  // rather than positional, because a launcher appends it and does not know
  // what came before.
    for (int i = 1; i < argc; i++) {
      const std::string argument(argv[i]);
      if (argument.find("://") != std::string::npos) {
        basalt::setInitialUrl(argument);
        break;
      }
    }
    // Defaults to the raw-Fabric script, because that is what this host can
    // currently render: a React app needs components macOS cannot mount yet.
    gHost.moduleName = argc > 2 ? argv[2] : "";
    const char *sourcePath = getenv("BASALT_DEV_ENTRY");
    gHost.sourcePath = sourcePath != nullptr ? sourcePath : "index";

    // The app's Expo config, if this is an Expo app and it has been bundled.
    // Before anything installs the runtime. See core/ExpoModules.h.
    basalt::loadExpoAppConfigBeside(gHost.bundlePath);

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    installMainMenu();

    RnAppKitHostDelegate *delegate = [[RnAppKitHostDelegate alloc] init];
    NSApp.delegate = delegate;

    // Constructed here, on the main thread: AppKitMountingManager records this
    // thread and asserts that every mutation lands back on it.
    gHost.mountingManager = std::make_shared<basalt::AppKitMountingManager>();

    // The application's own window, which is windows.front() from here on and
    // is what everything not yet per-window means by "the window".
    createHostWindow(kSurfaceId,
                     @"basalt-core — macOS",
                     kInitialWidth,
                     kInitialHeight,
                     delegate);


    gHost.runLoopObserverManager = std::make_shared<RunLoopObserverManager>();
    gHost.choreographer = std::make_shared<basalt::AppKitAnimationChoreographer>();

    // Light or dark, and any change to it. Before ReactHost, so the module can
    // answer the first `Appearance.getColorScheme()` a bundle makes, which for an
    // Expo app is during its first import.
    basalt::startObservingColorScheme();

    // The platform's text scale, which on macOS is 1: appKitTextScale() is
    // where that is explained. Handed over the same way GTK hands over the one
    // it reads from GtkSettings, so core/FontScaling.h is the only thing that
    // decides what `allowFontScaling` means.
    basalt::setSystemFontScale(basalt::appKitTextScale());

    // Before ReactHost, so the beat is being induced from the first event on.
    gHost.runLoopObserver = basalt::installRunLoopObserver(gHost.runLoopObserverManager);

    // Say what this host needs rather than inheriting a default that moves.
    // HermesInstance gives the runtime a microtask queue only when
    // enableBridgelessArchitecture() is true, and React's scheduler enqueues a
    // microtask on its first render. This host is bridgeless -- there is no
    // bridge here to be the alternative -- so it asserts that.
    facebook::react::ReactNativeFeatureFlags::override(
        std::make_unique<DesktopFeatureFlags>());

    ReactInstanceConfig config;
    // The appId is how this host tells Metro which platform it is; see the
    // matching comment in gtk/main.cpp and APP_ID_PREFIX in
    // packages/basalt-core/metro-config.js.
    config.appId = "basalt-macos";
    config.deviceName = "macos";

    // Dev mode changes three things at once: loadScript tries Metro before the
    // on-disk bundle; DevServerHelper exists, which is the only condition under
    // which ReactCxxTurboModuleProvider serves the DevSettings module a __DEV__
    // bundle requires; and ReactHost opens a packager connection whose reload
    // message reloads the instance.
    // What this app calls itself. Nothing on macOS reads it yet -- the bundle's
    // Info.plist is what the system goes by there, and cli/packageApp.js writes
    // that -- but reading it here keeps `basalt::appIdentity()` answering on all
    // three hosts rather than on one. See core/AppIdentity.h.
    basalt::appIdentity(gHost.bundlePath);

    config.enableDevMode = getenv("BASALT_DEV") != nullptr;
    gHost.devMode = config.enableDevMode;
    config.enableInspector = config.enableDevMode;
    if (const char *devHost = getenv("BASALT_DEV_HOST")) {
      config.devServerHost = devHost;
    }
    if (const char *devPort = getenv("BASALT_DEV_PORT")) {
      config.devServerPort = (uint32_t)strtoul(devPort, nullptr, 10);
    }
    // So the http client can tell the bundle request apart from an app's own
    // fetch, and refuse to feed a Metro error page to the JS engine. See
    // core/DevBundle.h.
    if (config.enableDevMode) {
      basalt::setDevServerOrigin(config.devServerHost, config.devServerPort);
    }

    try {
      gHost.reactHost = std::make_unique<ReactHost>(
          config,
          gHost.mountingManager,
          gHost.runLoopObserverManager,
          makeContextContainer(),
          facebook::react::getDefaultOnJsErrorFunc(),
          logToConsole,
          nullptr,
          makeTurboModuleProviders(basalt::scriptURLFor(
              gHost.bundlePath,
              config.enableDevMode,
              config.devServerHost,
              config.devServerPort,
              gHost.sourcePath.empty() ? "index" : gHost.sourcePath,
              "macos"),
              config.enableDevMode,
              gHost.mountingManager->imageLoader()),
          // React Native's error inspector. ReactCxxPlatform implements the
          // LogBox TurboModule itself and only provides it when a host hands
          // over one of these; passing null, as this did, left
          // NativeLogBox.show() a call into nothing. See core/LogBoxSurface.h.
          std::make_shared<basalt::LogBoxSurfaceDelegate>(
              [](const std::string &appKey) { showLogBoxSurface(appKey); },
              []() { hideLogBoxSurface(); }),
          // `useNativeDriver: true`. ReactCommon has a C++ implementation of
          // the whole animated graph, and ReactCxxPlatform provides the module
          // for it -- but only when a host asks, by handing over a provider.
          // Without it every native-driven Animated call throws "Native
          // animated module is not available", which is also what kept
          // LogBox's own spinner from rendering.
          std::make_shared<facebook::react::NativeAnimatedNodesManagerProvider>(),
          installBindings,
          gHost.choreographer);
    } catch (const std::exception &error) {
      NSLog(@"could not construct ReactHost: %s", error.what());
      return 1;
    }

    // How anything in the core reaches the scheduler, which only the host has.
    // Reanimated is the one caller; see core/UIManagerAccess.h.
    basalt::setEventListenerInstaller(
        [](std::shared_ptr<const facebook::react::EventListener> listener) {
          if (gHost.reactHost == nullptr) {
            return;
          }
          gHost.reactHost->runOnScheduler([listener = std::move(listener)](
                                              facebook::react::Scheduler &scheduler) {
            scheduler.addEventListener(listener);
          });
        });

    // And how anything in the core reaches the JavaScript thread. An Expo
    // module is given no call invoker, so this is the only way one can emit an
    // event or settle a promise later; see core/JsRuntimeAccess.h.
    basalt::setRuntimeRunner(
        [](std::function<void(facebook::jsi::Runtime &)> work) {
          if (gHost.reactHost == nullptr) {
            return;
          }
          gHost.reactHost->runOnRuntimeScheduler(std::move(work));
        });

    // `loadScript` falls back to the on-disk bundle whenever the Metro fetch
    // fails, which is right when nothing is listening and wrong when Metro
    // answered with an error: running the last bundle that built, while the
    // developer looks at source that is not what is executing, hides the very
    // thing they need to see. So the fallback is allowed only for the first
    // case, and the second stops here with Metro's own message.
    const bool loaded = gHost.reactHost->loadScript(gHost.bundlePath, gHost.sourcePath);
    if (const auto devError = basalt::devBundleError()) {
      NSLog(@"Metro could not build the bundle (HTTP %ld):\n%s",
            devError->status,
            devError->message.c_str());
      // Through shutdown() rather than straight out: the fallback bundle is
      // already running on the JS thread, and dropping the host without
      // joining it aborts in a destructor instead of exiting.
      shutdown();
      return 1;
    }
    if (!loaded) {
      NSLog(@"could not load script: %s", gHost.bundlePath.c_str());
      shutdown();
      return 1;
    }
    // Where it came from, not where it might have. See core/DevBundle.h.
    if (const auto from = basalt::devBundleFetched()) {
      NSLog(@"loaded script from the packager: %s", from->c_str());
    } else {
      NSLog(@"loaded script: %s", gHost.bundlePath.c_str());
    }

    // The module name decides who drives the surface. Non-empty:
    // SurfaceHandler::start calls AppRegistry.runApplication. Empty: the
    // surface is registered without JS being called at all, leaving it for a
    // script to commit into through nativeFabricUIManager by hand.
    gHost.reactHost->startSurface(kSurfaceId,
                                  gHost.moduleName,
                                  folly::dynamic::object(),
                                  constraintsFor(kInitialWidth, kInitialHeight),
                                  layoutContextFor(gHost.main().scaleFactor));
    gHost.mountingManager->setSurfaceSize((float)kInitialWidth, (float)kInitialHeight);
    gHost.surfaceStarted = true;
    NSLog(@"started surface %d%s%s",
          (int)kSurfaceId,
          gHost.moduleName.empty() ? " (no module; raw Fabric script)" : " for module ",
          gHost.moduleName.c_str());

    [gHost.main().window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    // Prime the bounds cache, which `getBounds()` answers from: without this an
    // app's first render sees a window of no size, and only a later resize
    // corrects it. See core/WindowBoundsCache.cpp.
    basalt::notifyWindowBoundsChanged();

    // Accepting what the desktop drags onto this window. On the surface root
    // rather than per view: AppKit asks the destination under the pointer,
    // and which *app* view that is is core/DragAndDrop.h's question.
    basalt::attachDropTarget(gHost.main().root);

    // And the display list, which `getDisplays()` answers from. Same reason,
    // and the same trap: the first version of this primed from
    // windowDidResize, which is not startup -- an app that never resized its
    // window was told the desktop had no screens.
    basalt::notifyDisplaysChanged();

    // The two keys this host answers before anything else sees them.
    //
    // Escape closes the topmost <Modal> -- or rather, asks the app to. React
    // Native's `onRequestClose` is the hardware back button on Android and the
    // swipe-down on iOS; on a desktop it is Escape.
    //
    // Cmd+D opens React Native's developer menu, which is the shortcut it uses
    // on a simulator and the closest thing a desktop has to a phone's shake.
    //
    // A local event monitor rather than `keyDown:` on a view, because neither
    // is about the focused view and nothing in the window need have focus. The
    // monitor swallows a key only when it actually did something, so both
    // still do whatever they did before everywhere else.
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                          handler:^NSEvent *(NSEvent *event) {
                                            const unichar first =
                                                event.charactersIgnoringModifiers.length > 0
                                                ? [event.charactersIgnoringModifiers
                                                      characterAtIndex:0]
                                                : 0;
                                            const BOOL command =
                                                (event.modifierFlags &
                                                 NSEventModifierFlagCommand) != 0;
                                            if (command && (first == 'd' || first == 'D') &&
                                                gHost.devMode && gHost.reactHost != nullptr) {
                                              basalt::showDevMenu(gHost.reactHost.get());
                                              return nil;
                                            }
                                            if (first == 0x1B &&
                                                gHost.mountingManager != nullptr &&
                                                gHost.mountingManager->requestCloseTopModal()) {
                                              return nil;
                                            }

                                            // An app's declared shortcut, when
                                            // the responder chain will not
                                            // deliver it.
                                            //
                                            // `RnAppKitView keyDown:` is only
                                            // called on the first responder, so
                                            // an app whose shortcut holder is
                                            // the root -- nothing focuses it,
                                            // `focusable` only makes it
                                            // eligible -- never sees a press at
                                            // all. Only when nothing of ours is
                                            // in the chain: with a view focused
                                            // the ordinary path runs and this
                                            // must not run again, or a text
                                            // field would lose keys to a
                                            // window-level claim.
                                            if (basaltViewInResponderChain(event.window)) {
                                              return event;
                                            }
                                            if (gHost.main().focusManager != nullptr &&
                                                gHost.main().focusManager->deliverWindowKey(
                                                    event)) {
                                              return nil;
                                            }
                                            return event;
                                          }];

    // The display link needs a window, which the root now has.
    gHost.choreographer->attachToView(gHost.main().root);

    if (gHost.moduleName.empty()) {
      NSLog(@"--- committing tree 1 from JS ---");
      callRenderFunction(1);
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.0 * NSEC_PER_SEC)),
                     dispatch_get_main_queue(),
                     ^{
                       NSLog(@"--- committing tree 2 from JS ---");
                       callRenderFunction(2);
                     });
    }

    // One clock for every instrument below, rather than one each.
    //
    // They used to start at 1500ms independently, which was invisible for as
    // long as no scenario used two at once -- and wrong the moment one did: a
    // run with both BASALT_TEST_TAP and BASALT_TEST_FOCUS interleaved them
    // instead of running the taps and then the keys. The GTK and Win32 hosts
    // have always threaded a single delay through; this is the third.
    __block int64_t scriptedDelayMs = 1500;

    // BASALT_TEST_TAP: "x,y;x,y" -- synthesise taps a second apart, in
    // surface-root coordinates. Enters where AppKit's mouse handler would, so
    // it exercises hit testing and event delivery but not AppKit itself.
    //
    // It exists because the alternative is CGEvent, which needs accessibility
    // permission an automated run does not have. The GTK host has the same
    // escape hatch for the same reason, and the end-to-end suite on Linux uses
    // xdotool instead where it can.
    //
    // A point may name a window: "x,y@3" taps in the window whose surface is 3
    // rather than in the app's own. Without it there would be no way to reach a
    // second window at all -- each has its own touch dispatcher, which is the
    // whole point of them, and the main window's would happily hit-test a tree
    // that is not on screen.
    //
    // BASALT_TEST_SECONDARY_TAP takes the same spec and clicks the other
    // button. Its own variable rather than a suffix on a point, because the two
    // assert opposite things -- one presses what it lands on and the other must
    // not -- and a scenario that mixed them in one string would be harder to
    // read than to write.
    void (^scheduleTaps)(const char *, basalt::PointerButton) =
        ^(const char *taps, basalt::PointerButton button) {
          NSString *spec = [NSString stringWithUTF8String:taps];
          const BOOL secondary = button != basalt::PointerButton::Primary;
          NSString *name = secondary ? @"BASALT_TEST_SECONDARY_TAP" : @"BASALT_TEST_TAP";
          int64_t delayMs = scriptedDelayMs;
          for (NSString *entry in [spec componentsSeparatedByString:@";"]) {
            NSArray<NSString *> *halves = [entry componentsSeparatedByString:@"@"];
            // Defaults to the app's own window, so every spec written before
            // windows existed still means what it did.
            const facebook::react::SurfaceId surfaceId =
                halves.count > 1 ? (facebook::react::SurfaceId)halves[1].intValue : kSurfaceId;
            NSArray<NSString *> *parts = [halves[0] componentsSeparatedByString:@","];
            if (parts.count != 2) {
              continue;
            }
            const double x = parts[0].doubleValue;
            const double y = parts[1].doubleValue;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delayMs * NSEC_PER_MSEC),
                           dispatch_get_main_queue(),
                           ^{
                             NSLog(@"%@: tapping (%.0f, %.0f) in window %d",
                                   name, x, y, (int)surfaceId);
                             HostWindow *target = gHost.windowFor(surfaceId);
                             if (target == nullptr) {
                               NSLog(@"%@: no window %d", name, (int)surfaceId);
                               return;
                             }
                             if (target->touchDispatcher != nullptr) {
                               target->touchDispatcher->synthesiseTap(x, y, button);
                             }
                           });
            delayMs += 1000;
          }
          scriptedDelayMs = delayMs;
        };

    if (const char *taps = getenv("BASALT_TEST_TAP")) {
      scheduleTaps(taps, basalt::PointerButton::Primary);
    }
    // The other button, which presses nothing and arrives as a pointer event.
    // See core/PointerButtons.h.
    if (const char *taps = getenv("BASALT_TEST_SECONDARY_TAP")) {
      scheduleTaps(taps, basalt::PointerButton::Secondary);
    }

    // BASALT_TEST_HOVER: "x,y;x,y" -- the pointer moving with no button down,
    // a second apart, in surface-root coordinates. The only way to reach the
    // hover path from automation: a real cursor move is CGEvent again, and a
    // posted NSEventTypeMouseMoved does not drive a tracking area. A negative
    // point means the pointer left the surface.
    if (const char *hovers = getenv("BASALT_TEST_HOVER")) {
      NSString *spec = [NSString stringWithUTF8String:hovers];
      int64_t delayMs = scriptedDelayMs;
      for (NSString *point in [spec componentsSeparatedByString:@";"]) {
        NSArray<NSString *> *parts = [point componentsSeparatedByString:@","];
        if (parts.count != 2) {
          continue;
        }
        const double x = parts[0].doubleValue;
        const double y = parts[1].doubleValue;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delayMs * NSEC_PER_MSEC),
                       dispatch_get_main_queue(),
                       ^{
                         NSLog(@"BASALT_TEST_HOVER: hovering (%.0f, %.0f)", x, y);
                         if (gHost.main().touchDispatcher != nullptr) {
                           gHost.main().touchDispatcher->synthesiseHover(x, y);
                         }
                       });
        delayMs += 1000;
      }
      scriptedDelayMs = delayMs;
    }

    // BASALT_TEST_CLOSE_WINDOW: the surface id of a window to close the way a
    // person would -- its own close button, not the app asking. The two take
    // different paths through the host and only one of them can leave a record
    // whose window is gone, which is why it is worth being able to drive.
    if (const char *closing = getenv("BASALT_TEST_CLOSE_WINDOW")) {
      const auto surfaceId = (facebook::react::SurfaceId)atoi(closing);
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, scriptedDelayMs * NSEC_PER_MSEC),
                     dispatch_get_main_queue(),
                     ^{
                       NSLog(@"BASALT_TEST_CLOSE_WINDOW: closing window %d", (int)surfaceId);
                       HostWindow *target = gHost.windowFor(surfaceId);
                       if (target != nullptr) {
                         // `performClose:`, not `close`: this is what a close
                         // button sends, so windowWillClose: runs exactly as it
                         // would for a person.
                         [target->window performClose:nil];
                       }
                     });
      scriptedDelayMs += 1000;
    }

    // BASALT_TEST_DROP: "x,y:path" -- a file dropped at that point, reported
    // the way the toolkit would report it. Entered below AppKit for the reason
    // the GTK host gives: a real drag needs a source outside this process.
    if (const char *drop = getenv("BASALT_TEST_DROP")) {
      const std::string spec(drop);
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, scriptedDelayMs * NSEC_PER_MSEC),
                     dispatch_get_main_queue(),
                     ^{
                       const size_t comma = spec.find(',');
                       const size_t colon = spec.find(':', comma == std::string::npos ? 0 : comma);
                       if (comma == std::string::npos || colon == std::string::npos) {
                         NSLog(@"BASALT_TEST_DROP: expected x,y:path");
                         return;
                       }
                       const double x = std::stod(spec.substr(0, comma));
                       const double y = std::stod(spec.substr(comma + 1, colon - comma - 1));
                       const std::string path = spec.substr(colon + 1);

                       const facebook::react::Tag found = basalt::dropTargetAt(
                           gHost.main().root, x, y, basalt::DropAcceptsFiles);
                       NSLog(@"BASALT_TEST_DROP: %s at %g,%g onto tag %d",
                             path.c_str(), x, y, (int)found);
                       if (found == 0) {
                         return;
                       }
                       basalt::DropEvent event;
                       event.tag = found;
                       event.phase = basalt::DropPhase::Drop;
                       event.x = x;
                       event.y = y;
                       event.payload.files.push_back(path);
                       basalt::reportDrop(event);
                     });
      scriptedDelayMs += 1000;
    }

    // BASALT_TEST_QUIT: ask the application to quit the way Cmd-Q does, so a
    // quit an app refuses can be driven from a script. Not the same thing as
    // BASALT_QUIT_AFTER_MS, which is the harness ending the process and is
    // deliberately not refusable -- this one goes through
    // applicationShouldTerminate: and is.
    //
    // A count rather than a flag, because one ask cannot show the interesting
    // half: an app that refuses is proved by the process still being here,
    // and an app that then agrees is proved by it going. Asks are a second
    // apart, like every other scripted instrument.
    if (const char *asks = getenv("BASALT_TEST_QUIT")) {
      const long times = std::max(1L, strtol(asks, nullptr, 10));
      for (long ask = 0; ask < times; ask++) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, scriptedDelayMs * NSEC_PER_MSEC),
                       dispatch_get_main_queue(),
                       ^{
                         NSLog(@"BASALT_TEST_QUIT: asking the application to quit");
                         [NSApp terminate:nil];
                       });
        scriptedDelayMs += 1000;
      }
    }

    // BASALT_TEST_FOCUS: keyboard actions separated by ';' -- `tab`,
    // `shift-tab`, `activate`, `escape` and `devmenu`, each fired a second
    // apart.
    //
    // The same reason the other instruments exist, one step further out. A real
    // Tab needs a window the window server considers key, which an automated
    // run does not reliably have. This enters at AppKitFocusManager, so it
    // exercises AppKit's own key-view loop, the focus and blur events and the
    // click dispatch, and skips only the delivery of the keystroke itself.
    // BASALT_TEST_KEY: a declared shortcut, pressed. Enters after the NSEvent
    // translation rather than before it -- see core/TestKeys.h for what that
    // does and does not prove.
    for (const basalt::KeyCombination &pressed : basalt::scriptedKeyPresses()) {
      const basalt::KeyCombination combination = pressed;
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, scriptedDelayMs * NSEC_PER_MSEC),
                     dispatch_get_main_queue(),
                     ^{
                       NSLog(@"BASALT_TEST_KEY: %s", combination.key.c_str());
                       if (gHost.main().focusManager != nullptr) {
                         gHost.main().focusManager->deliverTestKey(combination);
                       }
                     });
      scriptedDelayMs += 400;
    }

    if (const char *focus = getenv("BASALT_TEST_FOCUS")) {
      NSString *spec = [NSString stringWithUTF8String:focus];
      int64_t delayMs = scriptedDelayMs;
      for (NSString *raw in [spec componentsSeparatedByString:@";"]) {
        NSString *action = [raw stringByTrimmingCharactersInSet:
                                    [NSCharacterSet whitespaceCharacterSet]];
        if (action.length == 0) {
          continue;
        }
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delayMs * NSEC_PER_MSEC),
                       dispatch_get_main_queue(),
                       ^{
                         NSLog(@"BASALT_TEST_FOCUS: %@", action);
                         if (gHost.main().focusManager == nullptr &&
                             ![action isEqualToString:@"escape"] &&
                             ![action isEqualToString:@"devmenu"]) {
                           return;
                         }
                         if ([action isEqualToString:@"tab"]) {
                           gHost.main().focusManager->moveFocus(true);
                         } else if ([action isEqualToString:@"shift-tab"]) {
                           gHost.main().focusManager->moveFocus(false);
                         } else if ([action isEqualToString:@"activate"]) {
                           gHost.main().focusManager->activateFocused();
                         } else if ([action isEqualToString:@"devmenu"]) {
                           // Also not a focus action. Same instrument for the
                           // same reason: a key that needs nothing focused to
                           // arrive. What it opens is answered by
                           // BASALT_TEST_MENU; see core/TestDialog.h.
                           basalt::showDevMenu(gHost.reactHost.get());
                         } else if ([action isEqualToString:@"escape"]) {
                           // Not a focus action, and here anyway: this is the
                           // instrument for "a key was pressed and nothing on
                           // the window has to be focused for it to arrive",
                           // which is exactly what Escape closing a <Modal> is.
                           gHost.mountingManager->requestCloseTopModal();
                         } else {
                           NSLog(@"BASALT_TEST_FOCUS: unknown action \"%@\"", action);
                         }
                       });
        delayMs += 1000;
      }
      scriptedDelayMs = delayMs;
    }

    // BASALT_TEST_DRAG: "x1,y1,x2,y2" -- one press, twenty moves and a release,
    // for the gestures a tap cannot reach. See synthesiseDrag.
    if (const char *drag = getenv("BASALT_TEST_DRAG")) {
      NSArray<NSString *> *parts =
          [[NSString stringWithUTF8String:drag] componentsSeparatedByString:@","];
      if (parts.count == 4) {
        const double fromX = parts[0].doubleValue;
        const double fromY = parts[1].doubleValue;
        const double toX = parts[2].doubleValue;
        const double toY = parts[3].doubleValue;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)1500 * NSEC_PER_MSEC),
                       dispatch_get_main_queue(),
                       ^{
                         NSLog(@"BASALT_TEST_DRAG: (%.0f, %.0f) -> (%.0f, %.0f)",
                               fromX,
                               fromY,
                               toX,
                               toY);
                         if (gHost.main().touchDispatcher != nullptr) {
                           gHost.main().touchDispatcher->synthesiseDrag(fromX, fromY, toX, toY, 20);
                         }
                       });
      }
    }

    // BASALT_TEST_CLICK: the same "x,y;x,y", but as real NSEvents posted to the
    // window rather than as calls into the dispatcher.
    //
    // The difference from BASALT_TEST_TAP is the whole point: a synthesised tap
    // proves hit testing and delivery to JavaScript, and proves nothing about
    // whether AppKit routes a click to these views at all. This posts events
    // through the application's own queue, which exercises NSView's hit
    // testing, the responder chain and mouseDown:/mouseUp:.
    //
    // In-process, so it needs none of the accessibility permission CGEvent
    // would -- which is what makes it usable in automation where a real click
    // is not.
    if (const char *clicks = getenv("BASALT_TEST_CLICK")) {
      NSString *spec = [NSString stringWithUTF8String:clicks];
      int64_t delayMs = scriptedDelayMs;
      for (NSString *point in [spec componentsSeparatedByString:@";"]) {
        NSArray<NSString *> *parts = [point componentsSeparatedByString:@","];
        if (parts.count != 2) {
          continue;
        }
        const CGFloat x = parts[0].doubleValue;
        const CGFloat y = parts[1].doubleValue;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delayMs * NSEC_PER_MSEC),
                       dispatch_get_main_queue(),
                       ^{
                         NSLog(@"BASALT_TEST_CLICK: clicking (%.0f, %.0f)", x, y);
                         // Through the root, which converts out of the flipped
                         // top-left space every coordinate here is in and into
                         // the window's bottom-left one.
                         const NSPoint inWindow = [gHost.main().root convertPoint:NSMakePoint(x, y)
                                                                    toView:nil];
                         for (NSEventType type :
                              {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
                           NSEvent *event =
                               [NSEvent mouseEventWithType:type
                                                  location:inWindow
                                             modifierFlags:0
                                                 timestamp:NSProcessInfo.processInfo.systemUptime
                                              windowNumber:gHost.main().window.windowNumber
                                                   context:nil
                                               eventNumber:0
                                                clickCount:1
                                                  pressure:type == NSEventTypeLeftMouseDown ? 1 : 0];
                           [NSApp postEvent:event atStart:NO];
                         }
                       });
        delayMs += 1000;
      }
      scriptedDelayMs = delayMs;
    }

    // BASALT_TEST_SCROLL: "x,y,lines;x,y,lines" -- scroll wheel notches over a
    // point, a second apart, in surface-root coordinates. Positive scrolls
    // down, as a contentOffset does.
    //
    // The event is a real NSEvent, built the way a wheel builds one, and it is
    // handed to the view this project's own hit test finds. So it proves the
    // whole path below AppKit -- scrollWheel:, the handler lookup, the offset,
    // the clamp, the state write and onScroll -- and does not prove that AppKit
    // routes a wheel to that view, which is ordinary responder-chain behaviour
    // that `[super scrollWheel:]` takes part in.
    if (const char *scrolls = getenv("BASALT_TEST_SCROLL")) {
      NSString *spec = [NSString stringWithUTF8String:scrolls];
      int64_t delayMs = scriptedDelayMs;
      for (NSString *step in [spec componentsSeparatedByString:@";"]) {
        NSArray<NSString *> *parts = [step componentsSeparatedByString:@","];
        if (parts.count != 3) {
          continue;
        }
        const CGFloat x = parts[0].doubleValue;
        const CGFloat y = parts[1].doubleValue;
        const int32_t lines = (int32_t)parts[2].intValue;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delayMs * NSEC_PER_MSEC),
                       dispatch_get_main_queue(),
                       ^{
                         RnAppKitView *target = RnAppKitHitTest(gHost.main().root, x, y);
                         if (target == nil) {
                           NSLog(@"BASALT_TEST_SCROLL: nothing at (%.0f, %.0f)", x, y);
                           return;
                         }
                         NSLog(@"BASALT_TEST_SCROLL: %d lines over tag %ld",
                               lines, (long)target.rnTag);
                         // Negative because AppKit's positive Y is a scroll up,
                         // and this is spelled the way contentOffset reads.
                         CGEventRef wheel = CGEventCreateScrollWheelEvent(
                             nullptr, kCGScrollEventUnitLine, 1, -lines);
                         if (wheel == nullptr) {
                           return;
                         }
                         NSEvent *event = [NSEvent eventWithCGEvent:wheel];
                         CFRelease(wheel);
                         if (event != nil) {
                           [target scrollWheel:event];
                         }
                       });
        delayMs += 1000;
      }
      scriptedDelayMs = delayMs;
    }

    // BASALT_TEST_TYPE: text to insert into whatever field has focus, for the
    // same reason BASALT_TEST_TAP exists -- synthesising a real key event means
    // CGEvent and accessibility permission an automated run does not have.
    //
    // It inserts through the field editor, so it skips the key-down path and
    // the input method and exercises everything above them: the delegate, the
    // event emitter, React's re-render, and the controlled value coming back
    // down. The GTK host's version enters through GtkEditable for the same
    // reason, and the Linux end-to-end suite types with xdotool where it can.
    if (const char *text = getenv("BASALT_TEST_TYPE")) {
      NSString *typed = [NSString stringWithUTF8String:text];
      const char *afterMs = getenv("BASALT_TEST_TYPE_AFTER_MS");
      const int64_t delayMs = afterMs != nullptr ? strtoll(afterMs, nullptr, 10) : 2500;
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delayMs * NSEC_PER_MSEC),
                     dispatch_get_main_queue(),
                     ^{
                       NSResponder *responder = gHost.main().window.firstResponder;
                       if (![responder isKindOfClass:[NSText class]]) {
                         NSLog(@"BASALT_TEST_TYPE: no text field has focus");
                         return;
                       }
                       NSLog(@"BASALT_TEST_TYPE: typing \"%@\"", typed);
                       NSText *editor = (NSText *)responder;
                       [editor insertText:typed];
                     });
    }

    // What the schedule above came to, so BASALT_QUIT_WHEN_SETTLED waits for the
    // last of it rather than only for the first mount. The typing is the one
    // instrument here that does not fold into scriptedDelayMs -- it has its own
    // default and its own override -- so the total is the later of the two.
    // See core/TestSettle.h.
    {
      int64_t scriptedEndsIn = scriptedDelayMs;
      if (getenv("BASALT_TEST_TYPE") != nullptr) {
        const char *typeAfter = getenv("BASALT_TEST_TYPE_AFTER_MS");
        const int64_t typeAt = typeAfter != nullptr ? strtoll(typeAfter, nullptr, 10) : 2500;
        scriptedEndsIn = std::max(scriptedEndsIn, typeAt);
      }
      basalt::noteScriptedInputEndsInMs(scriptedEndsIn);
    }

    // BASALT_SNAPSHOT: render what is actually on screen to a PNG on the way
    // out. The tree dump above says what was mounted; this says what it looks
    // like, and the two fail differently -- a correct tree can still paint
    // nothing if the layer or the window is wrong.
    if (const char *snapshot = getenv("BASALT_SNAPSHOT")) {
      NSString *path = [NSString stringWithUTF8String:snapshot];
      // BASALT_SNAPSHOT_AFTER_MS: when to take it. A snapshot is only worth
      // anything if it lands after whatever is being tested, and the taps above
      // start at 1500ms, so the default would catch the app before its first
      // press.
      const char *after = getenv("BASALT_SNAPSHOT_AFTER_MS");
      const int64_t delayMs = after != nullptr ? strtoll(after, nullptr, 10) : 1000;
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delayMs * NSEC_PER_MSEC),
                     dispatch_get_main_queue(),
                     ^{
                       if (RnAppKitWriteSnapshot(gHost.main().root, path)) {
                         NSLog(@"wrote a snapshot to %@", path);
                       } else {
                         NSLog(@"could not write a snapshot to %@", path);
                       }
                     });
    }

    // Same clean exit on SIGINT or SIGTERM, so Ctrl-C and `kill` shut the
    // runtime down instead of dropping it -- the GTK host has had this since
    // phase 32 and this one never did. The default disposition kills the
    // process where it stands, skipping applicationWillTerminate and so
    // stopAllSurfaces, the teardown ordering and the tree dump.
    //
    // A dispatch source is the safe form, and the counterpart of GTK's
    // g_unix_signal_add: the handler does not run in signal context, it wakes
    // the main queue and runs there, so ordinary Cocoa calls are allowed. The
    // signal must be ignored first, because the dispatch source observes the
    // signal rather than replacing its disposition -- without this the process
    // still dies before the block runs.
    static NSMutableArray *sSignalSources = [NSMutableArray array];
    for (int signo : {SIGINT, SIGTERM}) {
      signal(signo, SIG_IGN);
      dispatch_source_t source =
          dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, (uintptr_t)signo, 0,
                                 dispatch_get_main_queue());
      dispatch_source_set_event_handler(source, ^{
        NSLog(@"signal received; quitting");
        [NSApp terminate:nil];
      });
      dispatch_resume(source);
      // Held in a static, because under ARC a dispatch_source_t is an object
      // like any other: let it go out of scope at the end of this iteration and
      // it is released and torn down before it can ever fire, which looks
      // exactly like the signal not being caught at all.
      [sSignalSources addObject:source];
    }

    // BASALT_QUIT_AFTER_MS: quitting on a timer is the only way to exercise the
    // shutdown path in automation. The window cannot be closed from a script,
    // and killing the process skips applicationWillTerminate entirely, so
    // stopAllSurfaces and the teardown ordering below it would never run.
    if (const char *quitAfter = getenv("BASALT_QUIT_AFTER_MS")) {
      const long ms = strtol(quitAfter, nullptr, 10);
      if (ms > 0) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)ms * NSEC_PER_MSEC),
                       dispatch_get_main_queue(),
                       ^{
                         NSLog(@"BASALT_QUIT_AFTER_MS elapsed; quitting");
                         endAnyOpenSheets();
                         gQuittingForTest = true;
                         [NSApp terminate:nil];
                       });
      }
    }

    // BASALT_TEST_QUIT_FILE: the same quit, at a moment the harness picks
    // rather than on a budget fixed before launch. `terminate:` either way, so
    // applicationWillTerminate runs and the view tree is dumped -- which is the
    // whole reason for asking rather than killing. See core/TestQuitFile.h.
    //
    // gQuittingForTest for the reason the timed quit sets it: this is the
    // harness ending the run, and an app that intercepts its own quit would
    // otherwise refuse it and hang. See applicationShouldTerminate:.
    //
    // Two instruments, one timer, because they want the same thing at different
    // moments: the quit file is the harness deciding, BASALT_QUIT_WHEN_SETTLED is
    // the app noticing it has nothing left to do. See core/TestSettle.h.
    if (basalt::testQuitFilePath().has_value() || basalt::quitWhenSettledMs().has_value()) {
      [NSTimer scheduledTimerWithTimeInterval:(double)basalt::kTestQuitFilePollMs / 1000.0
                                     repeats:YES
                                       block:^(NSTimer *timer) {
                                         const char *why = nullptr;
                                         if (basalt::testQuitFileAppeared()) {
                                           why = basalt::kTestQuitFileMessage;
                                         } else if (basalt::hasSettled()) {
                                           why = basalt::kQuitWhenSettledMessage;
                                         } else {
                                           return;
                                         }
                                         [timer invalidate];
                                         NSLog(@"%s", why);
                                         endAnyOpenSheets();
                                         gQuittingForTest = true;
                                         [NSApp terminate:nil];
                                       }];
    }

    [NSApp run];
  }
  return 0;
}
