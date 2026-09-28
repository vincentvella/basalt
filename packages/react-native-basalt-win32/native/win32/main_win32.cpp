// react-native-basalt — the Windows host process.
//
// Constructs a ReactHost, loads a script into Hermes, and runs one surface in
// an HWND. Everything on screen from here on is produced by Fabric: the
// mutation stream arrives through Win32MountingManager exactly as it does on
// iOS and Android, and nothing hand-builds a ShadowViewMutation.
//
// The counterpart of gtk/main_gtk.cpp and appkit/main_appkit.mm, and
// deliberately the same shape. The pieces a host has to supply, and where each
// comes from:
//
//   IMountingManager         Win32MountingManager         (this repo)
//   RunLoopObserverManager   ReactCxxPlatform             (event beat)
//   the beat itself          Win32RunLoopObserver         (this repo, the loop)
//   AnimationChoreographer   Win32AnimationChoreographer  (this repo, a timer)
//   ContextContainer         http + websocket client factories, below
//   ComponentRegistryFactory ComponentRegistryWin32       (via the mounting manager)
//   FontRegistry             FontRegistryDirectWrite      (this repo, link-time seam)
//   input                    Win32TouchDispatcher         (this repo, hostProc)
//
// Threading: this thread owns the window and the message loop. ReactHost spins
// up its own JS thread, and every mount is marshalled back here by
// Win32MountingManager through postToUiThread. Nothing below touches a view off
// this thread.
//
// What this host can render is what Win32MountingManager can mount: <View>,
// <Text>, <Image>, <ScrollView> and <TextInput> -- the same five as the other
// two desktops. `js/demo.js` -- a surface driven straight through
// nativeFabricUIManager, with no React and no react-native JavaScript -- is
// still the bundle this runs by default, because it is what an argumentless run
// has always meant here; an ordinary React app runs from a bundle and a module
// name.
//
// One thing below is not like the other hosts, and it is <TextInput>. Its peer
// is a real EDIT control, which is a child *window*, so the host has to place
// it, forward its notifications, and answer its colour questions. See
// win32/Win32TextInput.h.

#include "Win32AnimationChoreographer.h"
#include "AppIdentity.h"
#include "AppPaths.h"
#include "DevMenu.h"
#include "DialogModule.h"
#include "MenuModel.h"
#include "MenuModule.h"
#include "WindowsModule.h"
#include "TestKeys.h"
#include "TestQuitFile.h"
#include "TestSettle.h"
#include "Win32MountingManager.h"

#include <react/io/ImageLoaderModule.h>
#include "Win32MenuBar.h"
#include "Win32Packaging.h"
#include "PointerButtons.h"
#include "WindowControl.h"
#include "WindowHost.h"
#include "Win32RunLoopObserver.h"
#include "Win32Snapshot.h"
#include "Win32Strings.h"
#include "LogBoxSurface.h"

#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>
#include "Win32Focus.h"
#include "Win32TouchDispatcher.h"
#include "Win32UiThread.h"
#include "DragAndDrop.h"
#include "Win32DropTarget.h"
#include "Win32TitleBar.h"
#include "Win32WindowModule.h"
#include "RnWin32View.h"

#include "AppearanceModule.h"
#include "BlobModule.h"
#include "ColorScheme.h"
#include "CoreModules.h"
#include "DevBundle.h"
#include "ExpoModules.h"
#include "ExpoRuntime.h"
#include "TurboModuleProxy.h"
#include "GestureHandlerModule.h"
#include "PlatformConstantsModule.h"
#include "PlatformServices.h"
#include "ReanimatedModule.h"
#include "SourceCodeModule.h"
#include "StatusBarModule.h"
#include "JsRuntimeAccess.h"
#include "PackageModules.h"
#include "UIManagerAccess.h"
#include "WorkletsModule.h"

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

#include <windows.h>
// GET_X_LPARAM. Its own header, and not pulled in by WIN32_LEAN_AND_MEAN.
#include <windowsx.h>

#include <d2d1.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using facebook::react::ContextContainer;
using facebook::react::Float;
using facebook::react::LayoutConstraints;
using facebook::react::LayoutContext;
using facebook::react::LayoutDirection;
using facebook::react::ReactHost;
using facebook::react::ReactInstanceConfig;
using facebook::react::RunLoopObserverManager;
using facebook::react::SurfaceId;
using basalt::win32::RnWin32View;

namespace {

// In Fabric a SurfaceId *is* the root shadow node's tag, which is what lets the
// surface root live in the mounting manager's registry like any other view.
constexpr SurfaceId kSurfaceId = 1;

// React Native's error inspector, which is a surface of its own -- registered
// by AppRegistry under the name "LogBox", exactly as an app registers its own
// component. Started over the app's when NativeLogBox.show() asks; see
// core/LogBoxSurface.h.
constexpr SurfaceId kLogBoxSurfaceId = 2;

constexpr int kInitialWidth = 900;
constexpr int kInitialHeight = 700;

// The entry point for the *scriptless* mode, where the bundle is a hand-written
// script talking to nativeFabricUIManager directly rather than a React app.
// See js/demo.js. Only used when no module name is given.
constexpr const char *kRenderFunctionName = "basaltRender";

constexpr UINT_PTR kSecondTreeTimer = 100;

// BASALT_QUIT_AFTER_MS's timer. A constant rather than the literal 101 it used
// to be written as at its one call site: the spinner timer below was given 101
// too, and `SetTimer` with an id that is already in use *replaces* the timer
// rather than failing. An app with an <ActivityIndicator> in it therefore
// silently cancelled its own shutdown, and the only symptom was a host that
// never exited.
constexpr UINT_PTR kQuitAfterTimer = 101;

// One window, and the surface in it.
//
// A window is a surface: a surface is what has a size, a layout context and a
// root shadow node, and two windows sharing one would be two windows sharing a
// layout. So the id a window is known by *is* its surface id. See
// core/WindowHost.h.
//
// The render target is here rather than on Host because a
// ID2D1HwndRenderTarget belongs to one HWND -- painting a second window through
// the first one's would draw into the first one.
struct HostWindow {
  facebook::react::SurfaceId surfaceId{0};
  HWND window{nullptr};
  RnWin32View *root{nullptr};
  // Whether this press has already been offered as a drag out; see
  // WM_MOUSEMOVE.
  bool dragAsked{false};
  ComPtr<ID2D1HwndRenderTarget> target;
  std::unique_ptr<basalt::Win32TouchDispatcher> touchDispatcher;
  std::unique_ptr<basalt::Win32FocusManager> focusManager;
  // The app is closing this window, so WM_DESTROY is not news.
  //
  // Both closes end in `DestroyWindow` and so both raise WM_DESTROY, and the
  // handler there is the one that tells JavaScript a window closed *itself* and
  // tears the record down. Without this, an app closing a window ran that too:
  // a second `stopSurface`, a `basaltWindowClosed` nobody closed, and a second
  // deferred teardown that destroyed an already-destroyed root -- which is an
  // access violation at exit rather than a warning.
  //
  // AppKit has the same hazard and answers it the same way, by nilling the
  // delegate before `close`. Windows has no delegate to nil.
  bool closingFromApp{false};
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

  // Which window a message is for. `hostProc` is one procedure for every window
  // this host makes, so every message starts by asking this.
  HostWindow *windowFor(HWND hwnd) {
    for (const auto &candidate : windows) {
      if (candidate->window == hwnd) {
        return candidate.get();
      }
    }
    return nullptr;
  }

  ComPtr<ID2D1Factory> d2dFactory;

  std::shared_ptr<basalt::Win32MountingManager> mountingManager;
  std::shared_ptr<RunLoopObserverManager> runLoopObserverManager;
  std::shared_ptr<basalt::Win32AnimationChoreographer> choreographer;
  std::unique_ptr<ReactHost> reactHost;
  // The error inspector's own surface root, or null when it is not showing.
  // Painted after the app's and hit-tested before it, which is the whole of
  // what "on top" means on a platform where a view is not a window.
  RnWin32View *logBoxRoot{nullptr};

  std::string bundlePath;
  // Empty means the bundle is a raw Fabric script rather than a React app.
  std::string moduleName;
  // Metro's entry point, without the extension. Only meaningful in dev mode.
  std::string sourcePath;
  bool surfaceStarted{false};
  // Whether this run is a development one, which is the only thing that decides
  // whether Ctrl+D opens anything. Kept because the ReactInstanceConfig it came
  // from is not.
  bool devMode{false};
  int scaleFactor{1};
  // Whether TrackMouseEvent is armed. Windows sends WM_MOUSELEAVE once and then
  // forgets, so it has to be re-armed on every move; the flag keeps that to one
  // call per entry rather than one per motion message.
  bool trackingMouseLeave{false};
};

Host gHost;

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

// ReactHost's Logger is where console.log and RN's own logging come out.
// stderr rather than OutputDebugString: this is a console subsystem binary, and
// a host whose logs are only visible in a debugger is a host nobody can run
// from a terminal.
void logToConsole(const std::string &message, unsigned int logLevel) {
  const char *prefix = "[js]";
  switch (logLevel) {
    case ReactNativeLogLevelFatal:
    case ReactNativeLogLevelError:
      prefix = "[js error]";
      break;
    case ReactNativeLogLevelWarning:
      prefix = "[js warn]";
      break;
    default:
      break;
  }
  std::fprintf(stderr, "%s %s\n", prefix, message.c_str());
  std::fflush(stderr);
}

// ---------------------------------------------------------------------------
// Layout constraints
// ---------------------------------------------------------------------------

// Minimum == maximum pins the root to the window's client box, which is what a
// full-window surface wants: the root fills, and flex children divide it.
LayoutConstraints constraintsFor(int width, int height) {
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
// Painting
// ---------------------------------------------------------------------------

// Per window: an ID2D1HwndRenderTarget belongs to one HWND, so each window has
// its own and a second window cannot borrow the first's.
bool ensureTarget(HostWindow *made) {
  if (made == nullptr) {
    return false;
  }
  if (made->target) {
    return true;
  }
  if (!gHost.d2dFactory || made->window == nullptr) {
    return false;
  }
  RECT client{};
  GetClientRect(made->window, &client);
  const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(client.right - client.left),
                                       static_cast<UINT32>(client.bottom - client.top));
  if (size.width == 0 || size.height == 0) {
    return false;
  }
  return SUCCEEDED(gHost.d2dFactory->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(made->window, size),
      &made->target));
}

// Asked for after every mounted transaction. Windows repaints on demand rather
// than continuously, so without this a mutation changes the view tree and
// nothing on screen moves until the window happens to be invalidated by
// something else -- which reads as "mounting is broken" and is not.
void requestRepaint() {
  // Every window. This asked for the app's own and only the app's own, which
  // was the whole truth when there was only ever one -- and afterwards meant a
  // second window whose content changed and did not redraw until Windows
  // happened to invalidate it for some other reason.
  //
  // All of them rather than the one the transaction was for, because this is
  // called from the mount path with no surface in hand. Invalidating a window
  // that did not change costs one WM_PAINT and is what the main window has
  // always done for a transaction in the error inspector.
  for (const auto &window : gHost.windows) {
    if (window->window != nullptr) {
      InvalidateRect(window->window, nullptr, FALSE);
    }
  }
}

// After every transaction, and after every scroll. Both move views, and a
// <TextInput>'s peer is a child window that does not move with one: nothing in
// the view tree owns an HWND, so its placement is recomputed from the tree
// rather than following it. GTK and AppKit need no equivalent, because there a
// text field is a widget inside the widget that is the view.
// Whether the window has to keep repainting because a spinner is turning, and
// the timer that makes it.
//
// An <ActivityIndicator> is painted here rather than mounted -- Windows has no
// spinner control; see RnWin32View.h -- so nothing invalidates the window
// between mounts, and without this a spinner is a still picture. GtkSpinner and
// NSProgressIndicator each drive their own redraw, which is why neither other
// host needs an equivalent.
//
// Started and stopped rather than left running, for the reason
// Win32AnimationChoreographer.h gives about its own timer: a timer that runs
// whether or not anything is animating wakes the process sixty times a second
// forever, which matters more on a laptop than the frame interval does.
constexpr UINT_PTR kSpinnerTimer = 102;

// BASALT_TEST_QUIT_FILE's poll. Repeating, unlike kQuitAfterTimer's one shot:
// it looks for the file until it finds it. See core/TestQuitFile.h.
constexpr UINT_PTR kQuitFileTimer = 103;
bool gSpinnerTimerRunning = false;

void updateSpinnerTimer() {
  if (gHost.main().window == nullptr || gHost.main().root == nullptr) {
    return;
  }
  const bool wanted = gHost.main().root->hasAnimatingSpinner() ||
      (gHost.logBoxRoot != nullptr && gHost.logBoxRoot->hasAnimatingSpinner());
  if (wanted == gSpinnerTimerRunning) {
    return;
  }
  gSpinnerTimerRunning = wanted;
  if (wanted) {
    SetTimer(gHost.main().window, kSpinnerTimer, 16, nullptr);
  } else {
    KillTimer(gHost.main().window, kSpinnerTimer);
  }
}

void syncPeersAndRepaint() {
  if (gHost.mountingManager != nullptr) {
    gHost.mountingManager->syncTextInputBounds(gHost.main().root);
  }
  updateSpinnerTimer();
  requestRepaint();
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
        std::fprintf(stderr, "script defines no global %s()\n", kRenderFunctionName);
        return;
      }
      value.getObject(runtime).getFunction(runtime).call(
          runtime,
          facebook::jsi::Value(static_cast<int>(kSurfaceId)),
          facebook::jsi::Value(step));
    } catch (const facebook::jsi::JSError &error) {
      std::fprintf(stderr,
                   "%s() threw: %s\n%s\n",
                   kRenderFunctionName,
                   error.getMessage().c_str(),
                   error.getStack().c_str());
    } catch (const std::exception &error) {
      std::fprintf(stderr, "%s() failed: %s\n", kRenderFunctionName, error.what());
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
// it. Every one comes from core/ and is shared with the other two hosts --
// which is the whole argument of phase 17 arriving as a list of things this
// file did not have to write.
facebook::react::TurboModuleProviders makeTurboModuleProviders(
    std::string scriptURL,
    bool devMode,
    std::weak_ptr<basalt::win32::Win32ImageLoader> imageLoader) {
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
        // `Image.getSize` and `Image.prefetch`. Upstream builds this module
        // with no loader at all and offers no way to supply one, so the answer
        // is to build it here with the loader this host already has -- which
        // is also the one holding the decoded images, so a size asked for
        // something on screen costs nothing. See docs/backlog/upstream.md.
        if (name == facebook::react::ImageLoaderModule::kModuleName) {
          return std::make_shared<facebook::react::ImageLoaderModule>(jsInvoker, imageLoader);
        }
        if (name == facebook::react::PlatformConstantsModule::kModuleName) {
          return std::make_shared<basalt::DesktopPlatformConstantsModule>(jsInvoker);
        }
        if (name == basalt::DesktopAppModule::kModuleName) {
          return std::make_shared<basalt::DesktopAppModule>(jsInvoker);
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
#ifdef BASALT_HAS_WORKLETS
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
        // The window's title bar. This host's own rather than core's, because it
        // needs the window; see win32/Win32WindowModule.h.
        if (name == basalt::Win32WindowModule::kModuleName) {
          return std::make_shared<basalt::Win32WindowModule>(jsInvoker);
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
  // why this host gets them for free.
  contextContainer->insert(facebook::react::HttpClientFactoryKey,
                           facebook::react::getHttpClientFactory());
  contextContainer->insert(facebook::react::WebSocketClientFactoryKey,
                           facebook::react::getWebSocketClientFactory());

  // MessageQueueThreadFactoryKey is deliberately left unset: ReactHost then
  // installs MessageQueueThreadImpl, a real threaded queue.
  return contextContainer;
}

// BASALT_DUMP_MENU: write the application menu that is actually installed to a
// file on the way out.
//
// Read back from the platform rather than from the model that was sent, which
// is the point: it says a description became a real menu -- and it is the only
// way an automated run can see a menu bar at all, since a menu cannot be opened
// without a person. Written even when it is empty, because "this platform has
// no menu bar" is exactly what a test on Linux is asserting.
void dumpMenuIfRequested() {
  const char *path = std::getenv("BASALT_DUMP_MENU");
  if (path == nullptr) {
    return;
  }
  const std::string described = basalt::describeApplicationMenu();
  std::FILE *file = std::fopen(path, "wb");
  if (file == nullptr) {
    std::fprintf(stderr, "could not write the menu dump to %s\n", path);
    return;
  }
  std::fwrite(described.data(), 1, described.size(), file);
  std::fclose(file);
}

// BASALT_DUMP_TREE: write the view tree to a file on the way out, so an
// automated run can assert on what React actually produced rather than on a
// screenshot.
void dumpTreeIfRequested() {
  const char *path = std::getenv("BASALT_DUMP_TREE");
  if (path == nullptr || gHost.main().root == nullptr) {
    return;
  }
  if (FILE *file = std::fopen(path, "wb")) {
    std::string described = gHost.main().root->describeTree();
    // Every other window, each under a header naming its surface. Appended
    // rather than merged for the same reason the inspector is: they are
    // separate trees on screen, and nesting one inside another would say
    // something untrue.
    for (const auto &other : gHost.windows) {
      if (other->surfaceId == kSurfaceId || other->root == nullptr) {
        continue;
      }
      described += "--- window " + std::to_string(static_cast<int>(other->surfaceId)) + " ---\n";
      described += other->root->describeTree();
    }
    // The error inspector is a second surface with a root of its own, so it is
    // invisible to a dump of the app's. Appended rather than merged, because
    // the two are siblings on screen and nesting one inside the other would say
    // something untrue about the tree. Same separator the other two hosts use.
    if (gHost.logBoxRoot != nullptr) {
      described += "--- LogBox ---\n";
      described += gHost.logBoxRoot->describeTree();
    }
    std::fwrite(described.data(), 1, described.size(), file);
    std::fclose(file);
    std::fprintf(stderr, "wrote view tree to %s\n", path);
  } else {
    std::fprintf(stderr, "could not write %s\n", path);
  }
}

// BASALT_SNAPSHOT: render the surface to a PNG on the way out.
//
// The tree dump says which mutations arrived; it says nothing about whether any
// of them reached the screen, and on Windows those are genuinely separate --
// painting only happens when something invalidates. This is offscreen rather
// than a screen grab, so it works on a machine with no one watching and needs
// no window to be visible.
void snapshotIfRequested() {
  const char *path = std::getenv("BASALT_SNAPSHOT");
  if (path == nullptr || gHost.main().root == nullptr) {
    return;
  }
  if (basalt::win32::writeSnapshot(*gHost.main().root, path)) {
    std::fprintf(stderr, "wrote snapshot to %s\n", path);
  } else {
    std::fprintf(stderr, "could not write %s\n", path);
  }
}

// ---------------------------------------------------------------------------
// Scripted input
// ---------------------------------------------------------------------------

// BASALT_TEST_TAP and BASALT_TEST_DRAG, the same two escape hatches both other
// hosts have and for the same reason: the input path is otherwise untestable in
// automation. Synthesising a real click on Windows means SendInput, which moves
// the actual cursor and so cannot run beside anything else on the machine --
// the same objection CGEvent raises on macOS.
//
// They enter at the dispatcher rather than at the window procedure, so what
// they prove is hit testing, emitter lookup and delivery through the event
// beat, and what they leave unproven is that Windows routes WM_LBUTTONDOWN to
// `hostProc` at all. A person clicking the window is still the only check on
// that half, on all three platforms.
struct ScriptedInput {
  enum class Kind { Tap, Hover, Drag, Wheel, Type, Focus, Close, Quit, Drop, Key };

  Kind kind{Kind::Tap};
  double fromX{0};
  double fromY{0};
  double toX{0};
  double toY{0};
  // Wheel only: notches, positive being a turn towards the user, which scrolls
  // content down. The sign is the one BASALT_TEST_SCROLL takes on the other
  // hosts, not the one WM_MOUSEWHEEL uses.
  double lines{0};
  // Which window this is about. A tap may name one -- "x,y@3" -- because each
  // window has its own touch dispatcher, and the app's would happily hit-test a
  // tree that is not on screen; a Close names the window to close. Defaults to
  // the app's own, so every spec written before windows existed still means
  // what it did.
  facebook::react::SurfaceId surfaceId{kSurfaceId};
  // Tap only: which button. A secondary tap presses nothing and arrives as a
  // pointer event, which is the whole thing BASALT_TEST_SECONDARY_TAP exists to
  // drive. See core/PointerButtons.h.
  basalt::PointerButton button{basalt::PointerButton::Primary};
  // Type only. Default-initialised explicitly, so that the three kinds that do
  // not carry text can leave it out of a designated initialiser.
  std::string text{};
  // Key only: the combination to press. See core/TestKeys.h.
  basalt::KeyCombination pressed{};
};

// Fired from timers keyed by index, so the vector has to outlive the loop.
std::vector<ScriptedInput> gScriptedInput;

constexpr UINT_PTR kScriptedInputTimerBase = 200;

// Every timer id this host uses is distinct.
//
// `SetTimer` with an id that is already in use replaces the timer rather than
// failing, so a collision is silent and its symptom is whatever the timer it
// replaced was for. That happened: the spinner's repaint timer was given the
// same id as BASALT_QUIT_AFTER_MS's, and an app with an <ActivityIndicator> in
// it cancelled its own shutdown and hung. A static_assert cannot check the
// scripted range against the rest, so the base is held above everything else.
static_assert(kSecondTreeTimer != kQuitAfterTimer && kSecondTreeTimer != kSpinnerTimer &&
                  kSecondTreeTimer != kQuitFileTimer && kQuitAfterTimer != kSpinnerTimer &&
                  kQuitAfterTimer != kQuitFileTimer && kSpinnerTimer != kQuitFileTimer,
              "two timers share an id, and SetTimer would silently replace one with the other");
static_assert(kScriptedInputTimerBase > kSecondTreeTimer &&
                  kScriptedInputTimerBase > kQuitAfterTimer &&
                  kScriptedInputTimerBase > kSpinnerTimer &&
                  kScriptedInputTimerBase > kQuitFileTimer,
              "the scripted-input ids count up from the base and must start above the rest");

// The numbers in "10,20,30" -- however many there are, which is what lets one
// parser read both a tap's pair and a drag's four.
std::vector<double> parseNumbers(const std::string &spec) {
  std::vector<double> numbers;
  size_t start = 0;
  while (start <= spec.size()) {
    const size_t comma = spec.find(',', start);
    const std::string piece =
        spec.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!piece.empty()) {
      numbers.push_back(std::strtod(piece.c_str(), nullptr));
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return numbers;
}

void CALLBACK fireScriptedInput(HWND hwnd, UINT, UINT_PTR id, DWORD) {
  KillTimer(hwnd, id);
  const size_t index = static_cast<size_t>(id - kScriptedInputTimerBase);
  if (index >= gScriptedInput.size()) {
    return;
  }
  const ScriptedInput &action = gScriptedInput[index];
  switch (action.kind) {
    case ScriptedInput::Kind::Tap: {
      const bool secondary = action.button != basalt::PointerButton::Primary;
      const char *name = secondary ? "BASALT_TEST_SECONDARY_TAP" : "BASALT_TEST_TAP";
      std::fprintf(stderr,
                   "%s: tapping (%.0f, %.0f) in window %d\n",
                   name,
                   action.fromX,
                   action.fromY,
                   static_cast<int>(action.surfaceId));
      HostWindow *target = gHost.windowFor(action.surfaceId);
      if (target == nullptr) {
        std::fprintf(stderr, "%s: no window %d\n", name, static_cast<int>(action.surfaceId));
        break;
      }
      if (target->touchDispatcher != nullptr) {
        target->touchDispatcher->synthesiseTap(action.fromX, action.fromY, action.button);
      }
      // A real click on a <TextInput> never reaches the touch dispatcher: the
      // peer is a child window, so USER32 routes the click to it and the
      // control focuses itself. That is the one thing a synthesised tap cannot
      // reproduce, so it is done here instead -- and only here, because the
      // real path needs none of it.
      //
      // The primary button only: a right-click on a field does not focus it on
      // any desktop, it asks for a menu.
      if (!secondary && gHost.mountingManager != nullptr &&
          gHost.mountingManager->focusTextInputAt(target->root, action.fromX, action.fromY)) {
        std::fprintf(stderr, "BASALT_TEST_TAP: focused the field there\n");
      }
      break;
    }

    case ScriptedInput::Kind::Hover:
      std::fprintf(stderr,
                   "BASALT_TEST_HOVER: hovering (%.0f, %.0f)\n",
                   action.fromX,
                   action.fromY);
      if (gHost.main().touchDispatcher != nullptr) {
        gHost.main().touchDispatcher->synthesiseHover(action.fromX, action.fromY);
      }
      break;

    case ScriptedInput::Kind::Drag:
      std::fprintf(stderr,
                   "BASALT_TEST_DRAG: (%.0f, %.0f) -> (%.0f, %.0f)\n",
                   action.fromX,
                   action.fromY,
                   action.toX,
                   action.toY);
      gHost.main().touchDispatcher->synthesiseDrag(
          action.fromX, action.fromY, action.toX, action.toY, 20);
      break;

    case ScriptedInput::Kind::Wheel: {
      // A real WM_MOUSEWHEEL through the real window procedure, unlike the tap
      // and the drag above -- because the two things most likely to be wrong
      // about a wheel on Windows are the ones a dispatcher-level injection
      // would skip. Its coordinates are in screen space and its sign is
      // inverted against a contentOffset, and either mistake produces a wheel
      // that scrolls the wrong thing or the wrong way rather than one that does
      // nothing. So the point is converted back out to screen coordinates here
      // for `hostProc` to convert in again, and the notches are negated for it
      // to negate back.
      //
      // Sent rather than posted: a WM_TIMER callback is already on the UI
      // thread, so this reaches hostProc synchronously and stays ordered with
      // the actions around it.
      POINT screen{static_cast<LONG>(action.fromX), static_cast<LONG>(action.fromY)};
      ClientToScreen(hwnd, &screen);

      const int delta = static_cast<int>(-action.lines * WHEEL_DELTA);
      std::fprintf(stderr,
                   "BASALT_TEST_SCROLL: %.0f lines at (%.0f, %.0f)\n",
                   action.lines,
                   action.fromX,
                   action.fromY);
      SendMessage(hwnd,
                  WM_MOUSEWHEEL,
                  MAKEWPARAM(0, delta),
                  MAKELPARAM(static_cast<WORD>(screen.x), static_cast<WORD>(screen.y)));
      break;
    }

    case ScriptedInput::Kind::Key: {
      std::fprintf(stderr, "BASALT_TEST_KEY: %s\n", action.pressed.key.c_str());
      if (gHost.main().focusManager != nullptr) {
        gHost.main().focusManager->deliverTestKey(action.pressed);
      }
      break;
    }

    case ScriptedInput::Kind::Focus: {
      std::fprintf(stderr, "BASALT_TEST_FOCUS: %s\n", action.text.c_str());
      // Not a focus action, and here anyway: this is the instrument for "a key
      // was pressed and nothing on the window has to be focused for it to
      // arrive", which is exactly what Escape closing a <Modal> is.
      if (action.text == "devmenu") {
        // Also not a focus action. Same instrument for the same reason: a key
        // that needs nothing focused to arrive. What it opens is answered by
        // BASALT_TEST_MENU; see core/TestDialog.h.
        basalt::showDevMenu(gHost.reactHost.get());
        break;
      }
      if (action.text == "escape") {
        if (gHost.mountingManager != nullptr) {
          gHost.mountingManager->requestCloseTopModal();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
      }
      if (gHost.main().focusManager != nullptr) {
        if (action.text == "tab") {
          gHost.main().focusManager->moveFocus(true);
        } else if (action.text == "shift-tab") {
          gHost.main().focusManager->moveFocus(false);
        } else if (action.text == "activate") {
          gHost.main().focusManager->activateFocused();
        } else {
          std::fprintf(stderr, "BASALT_TEST_FOCUS: unknown action\n");
        }
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      break;
    }

    case ScriptedInput::Kind::Type: {
      // Real WM_CHARs into whichever <TextInput> has focus, so what this skips
      // is the keyboard driver and nothing above it: the EDIT's own handling,
      // EN_CHANGE, the emitter, the event beat, React's re-render and the
      // controlled value coming back down all run exactly as they would.
      //
      // Focus is a precondition rather than something this arranges, which is
      // why a BASALT_TEST_TAP on the field is scheduled before it -- the same
      // pairing both other hosts use.
      const bool typed = gHost.mountingManager != nullptr &&
          gHost.mountingManager->typeIntoFocusedTextInput(action.text);
      std::fprintf(stderr,
                   "BASALT_TEST_TYPE: \"%s\"%s\n",
                   action.text.c_str(),
                   typed ? "" : " -- no field has focus");
      break;
    }

    case ScriptedInput::Kind::Drop: {
      // A file dropped at a point, reported the way OLE would report it.
      // Entered below the toolkit for the reason the other hosts give: a real
      // drag needs a source outside this process. `fromX`/`fromY` carry the
      // point and `text` the path, reusing the fields the other kinds use.
      basalt::DragPayload payload;
      payload.files.push_back(action.text);

      const facebook::react::Tag found = basalt::dropTargetAt(
          gHost.windows.empty() ? nullptr : gHost.main().root,
          action.fromX,
          action.fromY,
          basalt::DropAcceptsFiles);
      std::fprintf(stderr,
                   "BASALT_TEST_DROP: %s at %g,%g onto tag %d\n",
                   action.text.c_str(),
                   action.fromX,
                   action.fromY,
                   static_cast<int>(found));
      if (found == 0) {
        break;
      }
      basalt::DropEvent event;
      event.tag = found;
      event.phase = basalt::DropPhase::Drop;
      event.x = action.fromX;
      event.y = action.fromY;
      event.payload = payload;
      basalt::reportDrop(event);
      break;
    }

    case ScriptedInput::Kind::Quit: {
      // The session ending, which is what "quit" means here: Windows has no
      // application-level quit gesture either -- Alt+F4 is a window close,
      // which is WM_CLOSE and useCloseRequest's business. So this sends the
      // message a logout sends, and the handler for it is the whole of this
      // host's half.
      std::fprintf(stderr, "BASALT_TEST_QUIT: ending the session\n");
      HWND main = gHost.windows.empty() ? nullptr : gHost.main().window;
      if (main != nullptr) {
        SendMessage(main, WM_QUERYENDSESSION, 0, ENDSESSION_CLOSEAPP);
      }
      break;
    }

    case ScriptedInput::Kind::Close: {
      // The window manager's close, not the app's: WM_CLOSE is what the caption
      // button sends, so the host's own handling runs exactly as it would for a
      // person. Which is the whole point -- an app closing a window and a
      // person closing one take different paths, and only one of them can leave
      // the host holding a record whose HWND is gone.
      HostWindow *closing = gHost.windowFor(action.surfaceId);
      std::fprintf(stderr,
                   "BASALT_TEST_CLOSE_WINDOW: %d%s\n",
                   static_cast<int>(action.surfaceId),
                   closing != nullptr ? "" : " -- no such window");
      if (closing != nullptr && closing->window != nullptr) {
        PostMessage(closing->window, WM_CLOSE, 0, 0);
      }
      break;
    }
  }
}

// Queues one action a second, starting a second and a half in -- late enough
// that the first tree has been committed and mounted. Returns the delay after
// the last one, so a drag can be scheduled behind the taps.
UINT scheduleScriptedInput(const ScriptedInput &action, UINT delayMs) {
  gScriptedInput.push_back(action);
  SetTimer(gHost.main().window,
           kScriptedInputTimerBase + gScriptedInput.size() - 1,
           delayMs,
           fireScriptedInput);
  return delayMs + 1000;
}

// "x,y" pairs separated by ';' -- the spelling BASALT_TEST_TAP and
// BASALT_TEST_HOVER share, since a press and a hover are both just a point.
UINT scheduleTestPoints(const char *spec,
                        UINT delayMs,
                        ScriptedInput::Kind kind,
                        basalt::PointerButton button = basalt::PointerButton::Primary) {
  const std::string all(spec);
  size_t start = 0;
  while (start <= all.size()) {
    const size_t semicolon = all.find(';', start);
    const std::string point =
        all.substr(start, semicolon == std::string::npos ? std::string::npos : semicolon - start);
    // "x,y@3" names the window; without an `@` it is the app's own.
    facebook::react::SurfaceId surfaceId = kSurfaceId;
    std::string coordinates = point;
    if (const size_t at = point.find('@'); at != std::string::npos) {
      surfaceId = static_cast<facebook::react::SurfaceId>(std::atoi(point.c_str() + at + 1));
      coordinates = point.substr(0, at);
    }
    const std::vector<double> numbers = parseNumbers(coordinates);
    if (numbers.size() == 2) {
      delayMs = scheduleScriptedInput(ScriptedInput{.kind = kind,
                                                    .fromX = numbers[0],
                                                    .fromY = numbers[1],
                                                    .surfaceId = surfaceId,
                                                    .button = button},
                                      delayMs);
    }
    if (semicolon == std::string::npos) {
      break;
    }
    start = semicolon + 1;
  }
  return delayMs;
}

// BASALT_TEST_DRAG: "x1,y1,x2,y2" -- one press, twenty moves and a release, for
// the gestures a tap cannot reach. A pan is defined by the movement between
// press and release, so a tap can never exercise one.
UINT scheduleTestDrag(const char *spec, UINT delayMs) {
  const std::vector<double> numbers = parseNumbers(std::string(spec));
  if (numbers.size() != 4) {
    return delayMs;
  }
  return scheduleScriptedInput(ScriptedInput{.kind = ScriptedInput::Kind::Drag,
                                             .fromX = numbers[0],
                                             .fromY = numbers[1],
                                             .toX = numbers[2],
                                             .toY = numbers[3]},
                               delayMs);
}

// BASALT_TEST_SCROLL: "x,y,lines" triples separated by ';'. Positive lines
// scroll down, as a contentOffset does -- the same spelling the macOS host
// takes, so one variable drives a comparison across both.
UINT scheduleTestScrolls(const char *spec, UINT delayMs) {
  const std::string all(spec);
  size_t start = 0;
  while (start <= all.size()) {
    const size_t semicolon = all.find(';', start);
    const std::string step =
        all.substr(start, semicolon == std::string::npos ? std::string::npos : semicolon - start);
    const std::vector<double> numbers = parseNumbers(step);
    if (numbers.size() == 3) {
      delayMs = scheduleScriptedInput(ScriptedInput{.kind = ScriptedInput::Kind::Wheel,
                                                    .fromX = numbers[0],
                                                    .fromY = numbers[1],
                                                    .lines = numbers[2]},
                                      delayMs);
    }
    if (semicolon == std::string::npos) {
      break;
    }
    start = semicolon + 1;
  }
  return delayMs;
}

// The two escape hatches, run once, before anything is torn down.
//
// "Before anything" has to include the *window*, which is why this is not
// simply the first thing `shutdown` does. Destroying the host window destroys
// every child window with it, and a <TextInput>'s peer is one -- so a tree
// dumped after that reports every field as empty and unfocused, which reads as
// a broken text field and is a dead HWND being asked a question. WM_CLOSE is
// where nothing has been destroyed yet.
//
// Idempotent, because both callers are real: a window closed by the user or by
// BASALT_QUIT_AFTER_MS arrives through WM_CLOSE, and a run that fails before
// the window exists only reaches `shutdown`.
void captureBeforeTeardown() {
  static bool captured = false;
  if (captured) {
    return;
  }
  captured = true;
  dumpTreeIfRequested();
  dumpMenuIfRequested();
  snapshotIfRequested();
}

void shutdown() {
  // Before the surface stops, which tears the tree down -- and before the
  // window goes, which takes every text field's peer with it.
  captureBeforeTeardown();

  if (gHost.choreographer != nullptr) {
    gHost.choreographer->detach();
  }
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

  // Every window, not only the app's own -- which is what this said when there
  // was only ever one, and did not say when there stopped being.
  //
  // It matters most for the Direct2D target. A record left for `gHost`'s own
  // destructor is released at static destruction, which is after `main` has
  // returned and after `CoUninitialize`, and releasing a COM interface once COM
  // has been torn down is undefined rather than merely late. A second window
  // still open at quit is exactly when that happens.
  //
  // The dispatchers go before the manager they hold a raw pointer into. Nothing
  // can reach them by now -- the message loop has already returned -- but the
  // order is the part that stays true if that ever stops being so.
  for (const auto &window : gHost.windows) {
    window->focusManager.reset();
    window->touchDispatcher.reset();
    window->target.Reset();
  }
  // And the roots, which the manager made and which name nothing after it goes.
  gHost.windows.clear();
  gHost.mountingManager.reset();
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

// Starts or stops React Native's error inspector.
//
// It is `LogBoxInspectorContainer`, registered by AppRegistry under the name
// "LogBox" exactly as an app registers its own component -- so this is a second
// surface rather than an overlay this host draws, and everything in it is React
// Native's own JavaScript. See core/LogBoxSurface.h.
//
// Input is retargeted rather than layered. On the other two desktops a view is
// a widget and the toolkit routes a press to whatever is on top; here there is
// nothing to route, so the dispatchers are pointed at the inspector's root
// while it is up and back at the app's when it comes down.
void hideLogBoxSurface() {
  if (gHost.reactHost == nullptr || gHost.logBoxRoot == nullptr) {
    return;
  }
  gHost.reactHost->stopSurface(kLogBoxSurfaceId);
  gHost.mountingManager->destroySurfaceRoot(kLogBoxSurfaceId);
  gHost.logBoxRoot = nullptr;
  if (gHost.main().touchDispatcher != nullptr) {
    gHost.main().touchDispatcher->setSurfaceRoot(gHost.main().root);
  }
  if (gHost.main().focusManager != nullptr) {
    gHost.main().focusManager->setSurfaceRoot(gHost.main().root);
  }
  if (gHost.main().window != nullptr) {
    InvalidateRect(gHost.main().window, nullptr, FALSE);
  }
}

void showLogBoxSurface(const std::string &appKey) {
  if (gHost.reactHost == nullptr || gHost.main().root == nullptr || gHost.logBoxRoot != nullptr) {
    return;
  }

  RECT client{};
  GetClientRect(gHost.main().window, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width <= 0 || height <= 0) {
    return;
  }

  gHost.logBoxRoot = gHost.mountingManager->createSurfaceRoot(kLogBoxSurfaceId);
  gHost.logBoxRoot->setFrame(0, 0, static_cast<float>(width), static_cast<float>(height));
  if (gHost.main().touchDispatcher != nullptr) {
    gHost.main().touchDispatcher->setSurfaceRoot(gHost.logBoxRoot);
  }
  if (gHost.main().focusManager != nullptr) {
    gHost.main().focusManager->setSurfaceRoot(gHost.logBoxRoot);
  }

  gHost.reactHost->startSurface(kLogBoxSurfaceId,
                                appKey,
                                folly::dynamic::object(),
                                constraintsFor(width, height),
                                layoutContextFor(gHost.scaleFactor));
  InvalidateRect(gHost.main().window, nullptr, FALSE);
}

LRESULT CALLBACK hostProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  // Which window this is for. One procedure serves every window this host
  // makes, so a message that reached for `the` window would paint, hit-test and
  // resize the app's own however many are open -- which is exactly the bug
  // multiple windows would otherwise introduce, and it would look like a second
  // window that renders nothing and a first one that flickers.
  //
  // Null before the main window is in the list, which is every message
  // CreateWindowEx sends before it returns. Those are the ones Windows sends to
  // set a window up, and DefWindowProc is the right answer to all of them.
  HostWindow *self = gHost.windowFor(hwnd);

  switch (message) {
    // The title bar. Each of these is answered only while an app has asked for
    // a hidden one, and falls through to Windows otherwise; see
    // win32/Win32TitleBar.h.
    case WM_NCCALCSIZE: {
      LRESULT result = 0;
      if (basalt::titleBar().handleNcCalcSize(wparam, lparam, result)) {
        return result;
      }
      break;
    }

    case WM_NCHITTEST: {
      LRESULT result = 0;
      if (basalt::titleBar().handleNcHitTest(lparam, gHost.main().root, result)) {
        return result;
      }
      break;
    }

    case WM_NCMOUSEMOVE:
    case WM_NCMOUSELEAVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCLBUTTONUP: {
      LRESULT result = 0;
      if (basalt::titleBar().handleNcMouse(message, wparam, result)) {
        return result;
      }
      break;
    }

    // An inactive window's caption buttons are drawn dimmer, as Windows' own are.
    case WM_ACTIVATE:
      basalt::titleBar().handleActivate(LOWORD(wparam) != WA_INACTIVE);
      break;

    // Light or dark, changed in Settings while the app runs. Windows announces
    // it with this message and this string, and nothing on this host listened
    // for it -- so neither the title bar nor Appearance ever changed.
    case WM_SETTINGCHANGE:
      if (lparam != 0 &&
          std::wcscmp(reinterpret_cast<const wchar_t *>(lparam), L"ImmersiveColorSet") == 0) {
        basalt::notifyColorSchemeChanged();
      }
      break;

    // A move changes the bounds `useWindow()` reports and nothing else, so
    // unlike WM_SIZE there is no surface to reconstrain -- which is exactly why
    // it needs its own case: WM_SIZE would never arrive, and `center()` would
    // look like it had done nothing.
    case WM_MOVE:
      basalt::notifyWindowBoundsChanged();
      return 0;

    case WM_SIZE: {
      if (self == nullptr) {
        break;
      }
      const int width = LOWORD(lparam);
      const int height = HIWORD(lparam);
      if (self->target) {
        self->target->Resize(D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)));
      }
      // The surface has to be told, or the root keeps the size it started with
      // while the window changes around it. This window's surface: constraining
      // the app's from every window would lay the first window's tree out to the
      // second window's size.
      if (gHost.surfaceStarted && width > 0 && height > 0) {
        self->root->setFrame(0, 0, static_cast<float>(width), static_cast<float>(height));
        gHost.reactHost->setSurfaceConstraints(
            self->surfaceId, constraintsFor(width, height), layoutContextFor(gHost.scaleFactor));
        // A <Modal> is sized from its own shadow-node state rather than from a
        // style, and React Native's C++ platform answers "what size is the
        // screen" with zero. Told here, where the window's size is already
        // being handed to Fabric.
        gHost.mountingManager->setSurfaceSize(static_cast<float>(width), static_cast<float>(height));
        // The window moved or was resized, which `useWindow()` hands an app as live
        // bounds. Here rather than in a watcher of its own: this is already the one
        // place that learns it.
        basalt::notifyWindowBoundsChanged();
        // The inspector covers the main window, so it resizes with it -- and
        // only with it: an error is about the app rather than about a window.
        if (gHost.logBoxRoot != nullptr && self->surfaceId == kSurfaceId) {
          gHost.logBoxRoot->setFrame(0, 0, static_cast<float>(width), static_cast<float>(height));
          gHost.reactHost->setSurfaceConstraints(
              kLogBoxSurfaceId, constraintsFor(width, height), layoutContextFor(gHost.scaleFactor));
        }
      }
      // A maximise moves a hidden title bar's caption, and nothing else says so.
      basalt::titleBar().refreshMetrics();
      return 0;
    }

    case WM_DPICHANGED: {
      // Yoga rounds to physical pixels, so telling it the wrong factor puts
      // half-pixel seams between adjacent views. The high word is the new DPI;
      // 96 is one.
      gHost.scaleFactor = static_cast<int>(HIWORD(wparam) / 96);
      if (gHost.scaleFactor < 1) {
        gHost.scaleFactor = 1;
      }
      // Windows also hands back the rectangle the window should move to, and
      // ignoring it leaves the window the wrong size on the new monitor.
      if (const auto *suggested = reinterpret_cast<const RECT *>(lparam)) {
        SetWindowPos(hwnd,
                     nullptr,
                     suggested->left,
                     suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      // The caption is sized in device-independent pixels.
      basalt::titleBar().refreshMetrics();
      return 0;
    }

    // Mouse coordinates arrive in client-area pixels, which are the surface
    // root's own coordinates: WM_SIZE sizes the root to the client rectangle,
    // so the two spaces are the same one and nothing has to be converted.
    // Each button is its own message on Windows, which is why there are six of
    // these rather than one with a parameter. Only the primary one captures or
    // presses anything; see core/PointerButtons.h.
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
      if (self == nullptr || self->touchDispatcher == nullptr) {
        break;
      }
      // A new press: the drag question is open again.
      self->dragAsked = false;
      const basalt::PointerButton button = message == WM_LBUTTONDOWN
          ? basalt::PointerButton::Primary
          : (message == WM_RBUTTONDOWN ? basalt::PointerButton::Secondary
                                       : basalt::PointerButton::Middle);
      // Capture, or a drag that leaves the window stops being reported and the
      // release never arrives -- which leaves the responder system believing a
      // finger is still down and swallows every press after it. AppKit and GTK
      // route a drag back to the view that took the press for free; here it has
      // to be asked for.
      //
      // Only for the button that can drag. A secondary click produces a pointer
      // event and is over; capturing the mouse for it would take the pointer
      // away from everything else for no reason.
      if (basalt::isPressButton(button)) {
        SetCapture(hwnd);
      }
      self->touchDispatcher->dispatchTouchStart(
          GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), button);
      return 0;
    }

    case WM_MOUSEMOVE:
      basalt::titleBar().clearHover();
      if (self != nullptr && self->touchDispatcher != nullptr) {
        // The touch model has no place for motion with no button down, and the
        // dispatcher drops it; the check here is only to keep an idle mouse
        // from walking the view tree sixty times a second.
        if (self->touchDispatcher->isDown()) {
          // A drag out of the window, if the press began on a marked view.
          // Asked once per press and here rather than on the button going
          // down: a press is not a drag until it moves.
          //
          // DoDragDrop runs its own modal loop until the drop, so the touch
          // that started this never ends -- it is cancelled rather than left
          // hanging, the same arrangement the AppKit host uses.
          if (!self->dragAsked) {
            self->dragAsked = true;
            if (basalt::beginDragIfMarked(self->root, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam))) {
              self->touchDispatcher->dispatchTouchCancel();
              return 0;
            }
          }
          self->touchDispatcher->dispatchTouchMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        } else {
          // Hover, which is a different question about the same message. Views
          // that listen for none of it cost a hit test and nothing more; see
          // core/HoverTracker.h.
          self->touchDispatcher->dispatchHover(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        }
      }
      // Without this the cursor leaving the window is silent, and whatever it
      // was over stays hovered for good. GTK's motion controller and AppKit's
      // tracking area both report an exit on their own; Windows has to be asked,
      // once per entry, and disarms itself when it fires.
      if (!gHost.trackingMouseLeave) {
        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = hwnd;
        gHost.trackingMouseLeave = TrackMouseEvent(&tracking) != FALSE;
      }
      return 0;

    // Tab, Enter and space. A <TextInput>'s peer is a real child window and
    // takes its keys directly, so anything reaching here is meant for the
    // painted views -- which have no window and so no focus of Windows' own.
    case WM_KEYDOWN:
      // Ctrl+D opens React Native's developer menu, which is the shortcut it
      // uses on a simulator and the closest thing a desktop has to a phone's
      // shake. Nothing in the window need have focus for it, which is why it is
      // answered here rather than by the focus manager.
      if (wparam == 'D' && (GetKeyState(VK_CONTROL) & 0x8000) != 0 && gHost.devMode &&
          gHost.reactHost != nullptr) {
        basalt::showDevMenu(gHost.reactHost.get());
        return 0;
      }
      // An app's own declared shortcuts, before Escape and before Tab: an app
      // that binds Escape means it. After Ctrl+D above, deliberately -- the
      // developer menu is not an app's to take, and an app that bound Ctrl+D
      // would otherwise make its own host undebuggable.
      if (gHost.main().focusManager != nullptr &&
          gHost.main().focusManager->handleDeclaredKey(static_cast<unsigned int>(wparam))) {
        return 0;
      }
      // Escape closes the topmost <Modal> -- or rather, asks the app to. React
      // Native's `onRequestClose` is the hardware back button on Android and
      // the swipe-down on iOS; on a desktop it is Escape, and a modal the app
      // does not close in response stays up, which is deliberate.
      if (wparam == VK_ESCAPE && gHost.mountingManager != nullptr &&
          gHost.mountingManager->requestCloseTopModal()) {
        return 0;
      }
      if (gHost.main().focusManager != nullptr &&
          gHost.main().focusManager->handleKeyDown(static_cast<unsigned int>(wparam))) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      break;

    case WM_MOUSELEAVE:
      gHost.trackingMouseLeave = false;
      basalt::titleBar().clearHover();
      if (gHost.main().touchDispatcher != nullptr) {
        gHost.main().touchDispatcher->dispatchHoverLeave();
      }
      return 0;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
      // `self`, not `gHost.main()`. This reached for the app's own window even
      // when the message was for another one, so a release in a second window
      // ended a touch in the first -- and left the second believing a finger
      // was still down, which swallows every press after it.
      if (self == nullptr || self->touchDispatcher == nullptr) {
        break;
      }
      const basalt::PointerButton button = message == WM_LBUTTONUP
          ? basalt::PointerButton::Primary
          : (message == WM_RBUTTONUP ? basalt::PointerButton::Secondary
                                     : basalt::PointerButton::Middle);
      // The end first: ReleaseCapture sends WM_CAPTURECHANGED synchronously,
      // and that is a cancel. Releasing first would turn every ordinary click
      // into a cancelled touch, which is a press that never fires.
      self->touchDispatcher->dispatchTouchEnd(
          GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), button);
      if (basalt::isPressButton(button) && GetCapture() == hwnd) {
        ReleaseCapture();
      }
      return 0;
    }

    // The wheel. Unlike every other mouse message, its coordinates are in
    // *screen* space -- the message is sent to the focused window rather than
    // to the one under the pointer, so a client-relative position would be
    // meaningless. Forgetting ScreenToClient gives a wheel that scrolls the
    // wrong list, or nothing, depending on where the window is on the desktop.
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
      if (gHost.mountingManager == nullptr || gHost.main().root == nullptr) {
        break;
      }
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd, &point);

      const double notches =
          static_cast<double>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<double>(WHEEL_DELTA);
      const double step = notches * basalt::Win32ScrollViewManager::kWheelStepPixels;

      // Both axes are inverted against React Native's, and for different
      // reasons. A positive WM_MOUSEWHEEL delta is a turn away from the user,
      // which moves content down and so *reduces* contentOffset; a positive
      // WM_MOUSEHWHEEL delta is a tilt to the right, which is the direction
      // contentOffset already grows in. Getting either wrong gives a list that
      // scrolls backwards, which is the kind of bug nobody reports as a bug.
      const double dx = message == WM_MOUSEHWHEEL ? step : 0.0;
      const double dy = message == WM_MOUSEWHEEL ? -step : 0.0;

      if (gHost.mountingManager->scrollAt(gHost.main().root, point.x, point.y, dx, dy)) {
        // Nothing else asks for this: a scroll changes no view's frame, so no
        // transaction is mounted and setOnDidMount never fires.
        syncPeersAndRepaint();
        return 0;
      }
      break;
    }

    // A <TextInput>'s peer is a real EDIT control, and an EDIT reports to its
    // parent rather than to itself. EN_CHANGE, EN_SETFOCUS and EN_KILLFOCUS all
    // arrive here, which is why the mounting manager has a method for them at
    // all: on GTK and AppKit the widget is the view and the toolkit routes this
    // without a host in the middle.
    case WM_COMMAND:
      // The application menu, first: its items arrive here with a null lParam,
      // which is how a menu command is told from a control's notification. See
      // win32/Win32MenuBar.h.
      if (lparam == 0 && HIWORD(wparam) == 0 &&
          basalt::win32::handleMenuCommand(static_cast<unsigned int>(LOWORD(wparam)))) {
        return 0;
      }
      // A field taking focus is the other half of the focus rule: the peer is a
      // real window and holds real Win32 focus, so whichever painted view was
      // wearing the ring has to give it up. Only one of the two kinds of focus
      // can be true at a time.
      if (HIWORD(wparam) == EN_SETFOCUS && gHost.main().focusManager != nullptr) {
        gHost.main().focusManager->textInputTookFocus();
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      if (gHost.mountingManager != nullptr &&
          gHost.mountingManager->handleControlCommand(wparam, lparam)) {
        return 0;
      }
      break;

    // And an EDIT asks its parent what colours to use. Without an answer it
    // draws in the system's, which have nothing to do with the `style` the
    // component was given -- on a dark field that is dark text on dark.
    // WM_CTLCOLORSTATIC as well, because a read-only EDIT asks with that one.
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
      if (gHost.mountingManager != nullptr) {
        if (HBRUSH brush = gHost.mountingManager->controlColor(
                reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam))) {
          return reinterpret_cast<LRESULT>(brush);
        }
      }
      break;

    case WM_CAPTURECHANGED:
      // Capture taken away by something else -- a modal dialog, Alt+Tab, the
      // debugger. The release will never arrive, so the touch has to be
      // cancelled here or the responder system waits for a finger that is gone.
      if (gHost.main().touchDispatcher != nullptr) {
        gHost.main().touchDispatcher->dispatchTouchCancel();
      }
      return 0;

    case WM_TIMER:
      if (wparam == kSpinnerTimer) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (wparam == kSecondTreeTimer) {
        KillTimer(hwnd, kSecondTreeTimer);
        std::fprintf(stderr, "--- committing tree 2 from JS ---\n");
        callRenderFunction(2);
      }
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT paint{};
      BeginPaint(hwnd, &paint);
      // Into this window's own target: an ID2D1HwndRenderTarget belongs to one
      // HWND, and painting a second window through the first one's would draw
      // into the first.
      if (self != nullptr && ensureTarget(self) && self->root != nullptr) {
        self->target->BeginDraw();
        self->target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        self->target->SetTransform(D2D1::Matrix3x2F::Identity());
        self->root->paint(self->target.Get());
        // The error inspector, over the app, in the main window. A second
        // surface rather than anything this host draws; see
        // core/LogBoxSurface.h.
        if (gHost.logBoxRoot != nullptr && self->surfaceId == kSurfaceId) {
          self->target->SetTransform(D2D1::Matrix3x2F::Identity());
          gHost.logBoxRoot->paint(self->target.Get());
        }
        // A hidden title bar's caption buttons, over the app's own content.
        // The main window's: the title bar is a process-wide seam.
        if (self->surfaceId == kSurfaceId) {
          basalt::titleBar().paintButtons(self->target.Get());
        }
        // A lost device is reported here and nowhere else. Dropping the target
        // is the whole recovery: the next WM_PAINT rebuilds it.
        if (self->target->EndDraw() == D2DERR_RECREATE_TARGET) {
          self->target.Reset();
        }
      }
      EndPaint(hwnd, &paint);
      return 0;
    }

    // How big this window may be. Windows asks rather than being told, which is
    // why the limits live in core and are read here: there is nowhere to have
    // passed them in. See core/WindowControl.h.
    //
    // The numbers are the *frame*, and an app's are its content -- so each is
    // adjusted outwards by whatever the border and caption cost, the same
    // correction `setWindowSize` makes in the other direction.
    case WM_GETMINMAXINFO: {
      // The app's own window only. The limits are one set of numbers for one
      // window, because `useWindow()` is about the window an app is in --
      // answering this for every window would make Windows the one host where a
      // limit set for one applied to all of them. Per-window geometry is its
      // own piece of work and is in docs/BACKLOG.md.
      if (self != nullptr && self->surfaceId != kSurfaceId) {
        break;
      }
      const basalt::WindowSizeLimits limits = basalt::windowSizeLimits();
      if (limits.minWidth <= 0.0 && limits.minHeight <= 0.0 && limits.maxWidth <= 0.0 &&
          limits.maxHeight <= 0.0) {
        break;
      }
      RECT frame{0, 0, 0, 0};
      AdjustWindowRectEx(&frame,
                         static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE)),
                         FALSE,
                         static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE)));
      const LONG extraWidth = frame.right - frame.left;
      const LONG extraHeight = frame.bottom - frame.top;

      auto *bounds = reinterpret_cast<MINMAXINFO *>(lparam);
      if (limits.minWidth > 0.0) {
        bounds->ptMinTrackSize.x = static_cast<LONG>(limits.minWidth) + extraWidth;
      }
      if (limits.minHeight > 0.0) {
        bounds->ptMinTrackSize.y = static_cast<LONG>(limits.minHeight) + extraHeight;
      }
      if (limits.maxWidth > 0.0) {
        bounds->ptMaxTrackSize.x = static_cast<LONG>(limits.maxWidth) + extraWidth;
      }
      if (limits.maxHeight > 0.0) {
        bounds->ptMaxTrackSize.y = static_cast<LONG>(limits.maxHeight) + extraHeight;
      }
      return 0;
    }

    // The session is ending -- logout, shutdown, reboot -- which is the only
    // thing on Windows that corresponds to macOS's Cmd-Q. There is no
    // application-level quit gesture to intercept: Alt+F4 is a window close,
    // which arrives as WM_CLOSE below and is useCloseRequest's business.
    //
    // FALSE refuses the session end, which is what an app with unsaved work
    // wants and is the whole of this host's half. Windows may end the session
    // anyway -- a forced shutdown does not take no for an answer, and nothing
    // here can change that -- so what this buys is the ordinary case, which is
    // the one an app can act on.
    // A monitor plugged in, unplugged, or a resolution change. Not handled
    // -- DefWindowProc still gets it -- because this is a notification and
    // nothing here is answering it.
    case WM_DISPLAYCHANGE:
      basalt::notifyDisplaysChanged();
      break;

    case WM_QUERYENDSESSION:
      if (basalt::hostQuitIntercepted()) {
        basalt::hostQuitRequested();
        return FALSE;
      }
      return TRUE;

    // Before DestroyWindow, which takes every <TextInput>'s peer with it. See
    // captureBeforeTeardown.
    //
    // The app's own window only. A second window closing is not teardown, and
    // capturing there would be worse than pointless: `captureBeforeTeardown`
    // latches, so the dump taken as the window went would be the one the run
    // reported -- showing the window still open, because it is, and silencing
    // the dump at shutdown that was meant to show it gone.
    case WM_CLOSE:
      // Somebody is trying to close this window: its caption button, Alt+F4, or
      // the system menu. WM_CLOSE is the attempt rather than the act -- nothing
      // is destroyed until DestroyWindow below -- which is what makes it the
      // place a window that asked to be asked first can refuse.
      //
      // Every window, including the app's own: an app that wants to ask "are
      // you sure" wants to ask hardest about the one it is running in. What
      // happens next is its decision, and if that is "go ahead" it calls
      // close() itself.
      if (self != nullptr && basalt::hostWindowCloseIntercepted(self->surfaceId)) {
        basalt::hostWindowCloseRequested(self->surfaceId);
        return 0;
      }
      if (self == nullptr || self->surfaceId == kSurfaceId) {
        captureBeforeTeardown();
      }
      DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      // Only the app's own window ends the process. A second window closing is
      // an app closing a window, and quitting on it would mean a preview panel
      // taking the whole application with it.
      if (self == nullptr || self->surfaceId == kSurfaceId) {
        PostQuitMessage(0);
        return 0;
      }
      // The app is already taking this window down; `closeHostWindow` is
      // mid-teardown and everything below is its job. See `closingFromApp`.
      if (self->closingFromApp) {
        return 0;
      }
      // Closed by the person rather than by the app: its own close button, or
      // Alt+F4. Without this the host keeps a record whose HWND is gone -- a
      // dangling handle rather than a stale flag -- and the `<Window>` that
      // opened it goes on believing it is open.
      //
      // The window is already being destroyed, so this cannot go through
      // closeHostWindow's teardown; what it does instead is stop the surface
      // and drop the record, which is the same work in the order this message
      // leaves available.
      {
        const facebook::react::SurfaceId closing = self->surfaceId;
        basalt::hostWindowClosed(closing);
        if (gHost.reactHost != nullptr) {
          gHost.reactHost->stopSurface(closing);
        }
        self->focusManager.reset();
        self->touchDispatcher.reset();
        self->target.Reset();
        self->window = nullptr;
        // The root outlives this message: the unmount mutations the stop
        // produces are still to come, and they name views in it. Dropped from
        // a round trip later, which is ordered behind them.
        if (gHost.reactHost != nullptr) {
          gHost.reactHost->runOnRuntimeScheduler([closing](facebook::jsi::Runtime &) {
            basalt::postToUiThread([closing] {
              HostWindow *going = gHost.windowFor(closing);
              if (going == nullptr) {
                return;
              }
              gHost.mountingManager->destroySurfaceRoot(closing);
              for (auto it = gHost.windows.begin(); it != gHost.windows.end(); ++it) {
                if (it->get() == going) {
                  gHost.windows.erase(it);
                  break;
                }
              }
            });
          });
        }
      }
      return 0;

    default:
      break;
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
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
                             const wchar_t *title,
                             int width,
                             int height) {
  const HINSTANCE instance = GetModuleHandle(nullptr);

  // Registered once for the process. A second call with the same name fails
  // with ERROR_CLASS_ALREADY_EXISTS, which is not an error here -- the class is
  // exactly what a second window wants.
  static bool classRegistered = false;
  if (!classRegistered) {
    WNDCLASSEX windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = hostProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"BasaltHost";
    if (RegisterClassEx(&windowClass) == 0) {
      return nullptr;
    }
    classRegistered = true;
  }

  // WS_CLIPCHILDREN so that the Direct2D paint excludes any <TextInput> peer.
  // Without it the whole client area is painted and every EDIT child then
  // repaints itself on top, which is a visible flicker on every mount.
  constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

  RECT wanted{0, 0, width, height};
  AdjustWindowRect(&wanted, kWindowStyle, FALSE);

  auto owned = std::make_unique<HostWindow>();
  HostWindow *made = owned.get();
  made->surfaceId = surfaceId;

  // The root before the window: hostProc answers messages CreateWindowEx sends
  // before it returns, and it finds this record by HWND -- which is not set
  // yet, so those messages go to DefWindowProc. Pushing the record afterwards
  // is what keeps that true rather than half-true.
  made->root = gHost.mountingManager->createSurfaceRoot(surfaceId);
  made->root->setFrame(0, 0, static_cast<float>(width), static_cast<float>(height));

  made->window = CreateWindowEx(0,
                                L"BasaltHost",
                                title,
                                kWindowStyle,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                wanted.right - wanted.left,
                                wanted.bottom - wanted.top,
                                nullptr,
                                nullptr,
                                instance,
                                nullptr);
  if (made->window == nullptr) {
    gHost.mountingManager->destroySurfaceRoot(surfaceId);
    return nullptr;
  }

  // Input, per window. The dispatcher holds this window's root, not a surface,
  // so it outlives every transaction mounted into it -- and a second window
  // with the first one's would deliver every press to the wrong tree.
  made->touchDispatcher =
      std::make_unique<basalt::Win32TouchDispatcher>(gHost.mountingManager.get(), made->root);
  // The keyboard half: Tab reaching a <Pressable>, and Enter activating it.
  // All of it is this project's -- a React Native view is not a window here, so
  // there is nothing for Windows to focus. See Win32Focus.h.
  made->focusManager =
      std::make_unique<basalt::Win32FocusManager>(gHost.mountingManager.get(), made->root);

  gHost.windows.push_back(std::move(owned));
  return made;
}

bool createWindow() {
  return createHostWindow(kSurfaceId, L"react-native-basalt \x2014 Windows", kInitialWidth,
                          kInitialHeight) != nullptr;
}

} // namespace

namespace basalt {

// core/WindowHost.h, on Windows.
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

  static facebook::react::SurfaceId nextSurfaceId = kLogBoxSurfaceId + 1;
  const facebook::react::SurfaceId surfaceId = nextSurfaceId++;

  const int width = static_cast<int>(options.width);
  const int height = static_cast<int>(options.height);
  const std::wstring title = basalt::win32::widen(options.title);
  HostWindow *made = createHostWindow(surfaceId, title.c_str(), width, height);
  if (made == nullptr) {
    return 0;
  }

  gHost.reactHost->startSurface(surfaceId,
                                options.component,
                                options.props,
                                constraintsFor(width, height),
                                layoutContextFor(gHost.scaleFactor));
  ShowWindow(made->window, SW_SHOW);
  UpdateWindow(made->window);
  std::fprintf(stderr,
               "opened window %d for module %s\n",
               static_cast<int>(surfaceId),
               options.component.c_str());
  return surfaceId;
}

// Ends the application. `PostQuitMessage` rather than `DestroyWindow` on
// each: it stops the message loop whatever is open, which is what a person
// choosing Quit means, and the ordinary teardown follows it.
void quitHost() {
  PostQuitMessage(0);
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
  // transaction of Remove and Delete mutations. Those arrive on the UI thread
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
      // The target before the window it belongs to, and the window before the
      // root the mutations named. DestroyWindow takes any <TextInput> peer
      // parented to it with it, which is why nothing here frees one by hand.
      going->target.Reset();
      // Before DestroyWindow, because DestroyWindow sends WM_DESTROY on this
      // thread and synchronously: the handler runs before the call returns, and
      // has to know this is the app's own doing.
      going->closingFromApp = true;
      // Null when WM_DESTROY got here first, which is a window the person
      // closed rather than the app.
      if (going->window != nullptr) {
        DestroyWindow(going->window);
      }
      gHost.mountingManager->destroySurfaceRoot(surfaceId);

      for (auto it = gHost.windows.begin(); it != gHost.windows.end(); ++it) {
        if (it->get() == going) {
          gHost.windows.erase(it);
          break;
        }
      }
      std::fprintf(stderr, "closed window %d\n", static_cast<int>(surfaceId));
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

int main(int argc, char **argv) {
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

  gHost.bundlePath = argc > 1 ? argv[1] : "build/main.jsbundle.js";
  // Defaults to the raw-Fabric script, because a React screen needs
  // <ScrollView> and <TextInput> that Windows does not mount yet and would
  // render with holes in it. A React app that stays inside <View>, <Text>,
  // <Image> and <Pressable> -- js/press.js, say -- runs from here.
  gHost.moduleName = argc > 2 ? argv[2] : "";
  // Metro's entry, as the GTK and AppKit hosts read it: BASALT_DEV_ENTRY, which
  // is what `run-windows` sets, and "index" otherwise. A third argument still
  // wins for anyone passing one. This used to read the argument alone, which
  // nothing passes, so sourcePath was empty in every development run --
  // DevServerHelper then builds no URL at all ("Failed to download JS bundle
  // from Url: ."), and loadScript quietly fell back to the on-disk bundle: the
  // window opened, and no edit ever reached it.
  if (argc > 3) {
    gHost.sourcePath = argv[3];
  } else if (const char *entry = std::getenv("BASALT_DEV_ENTRY")) {
    gHost.sourcePath = entry;
  } else {
    gHost.sourcePath = "index";
  }

  // A URL the desktop launched this with, for `Linking.getInitialURL()`. Any
  // argument after the bundle and the module that carries a scheme: a shell
  // association passes one, and it does not know what came before. `://` rather
  // than a bare colon, so a Windows path like `C:\bundle.js` is not mistaken
  // for one.
  for (int i = 1; i < argc; i++) {
    const std::string argument(argv[i]);
    if (argument.find("://") != std::string::npos) {
      basalt::setInitialUrl(argument);
      break;
    }
  }

  // Per-monitor DPI, declared in code rather than in a manifest so that running
  // the binary straight out of the build directory behaves the same as running
  // an installed one. Without it Windows scales the window's bitmap and every
  // line goes soft on a high-DPI display.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // COM, on the thread that will outlive every other one.
  //
  // WIC decodes an <Image> on a worker, and the IWICBitmap it produces is
  // released here when the view holding it is deleted. A release in a thread
  // with no apartment at all is what that used to be, and the process exited
  // 0xC0000005 every time an app rendered an image. ShellExecute wants an
  // initialised thread too, and that is how core/PlatformServices opens a URL.
  //
  // Apartment-threaded because this thread owns a window and pumps messages,
  // which is what an STA is for.
  // OleInitialize rather than CoInitializeEx: it initialises the same
  // apartment and additionally the OLE machinery RegisterDragDrop needs. A
  // thread that only called CoInitializeEx can open a URL and cannot accept a
  // drop, and the failure is a DRAGDROP_E_NOTREGISTERED nobody looks for.
  OleInitialize(nullptr);

  // Before anything posts: this is what makes postToUiThread marshal rather
  // than run inline, and the mounting manager depends on it. See
  // win32/Win32UiThread.h.
  basalt::installUiThread();

  // Before any window exists, which is the part that matters: the shell reads
  // a process's AppUserModelID when its first window is shown and does not
  // notice a later change. What the id buys is a notification Windows will
  // attribute to this application rather than refuse. Needs COM, which is
  // already initialised above. See win32/Win32Packaging.h.
  basalt::win32::applyPackaging(basalt::appIdentity(gHost.bundlePath));

  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               gHost.d2dFactory.GetAddressOf()))) {
    std::fprintf(stderr, "could not create a Direct2D factory\n");
    return 1;
  }
  // Before the window, which is the ordering multiple windows imposes: a window
  // makes a surface root, and only the mounting manager can. Constructed on
  // this thread, which it records and asserts every mutation lands back on.
  gHost.mountingManager = std::make_shared<basalt::Win32MountingManager>();

  if (!createWindow()) {
    std::fprintf(stderr, "could not create the window\n");
    return 1;
  }

  // The app's Expo config, if it is an Expo app and has been bundled. Before the
  // runtime is installed, as the other two hosts load it -- this one never did,
  // so Constants.expoConfig was null on Windows. It also names the window.
  basalt::loadExpoAppConfigBeside(gHost.bundlePath);
  {
    const std::string appName = basalt::expoAppName();
    basalt::titleBar().setDefaultTitle(
        !appName.empty() ? appName
                         : (!gHost.moduleName.empty() ? gHost.moduleName : "react-native-basalt"));
  }
  // Dark or light to match the app, and again whenever that changes -- in
  // Settings, or through Appearance.setColorScheme on the JavaScript thread, so
  // the change is marshalled here from wherever it was announced.
  basalt::titleBar().setDarkMode(basalt::effectiveColorScheme() == basalt::ColorScheme::Dark);
  basalt::titleBar().attach(gHost.main().window);
  basalt::addColorSchemeObserver([](basalt::ColorScheme scheme) {
    basalt::postToUiThread(
        [scheme] { basalt::titleBar().setDarkMode(scheme == basalt::ColorScheme::Dark); });
  });

  UINT dpi = GetDpiForWindow(gHost.main().window);
  gHost.scaleFactor = dpi >= 96 ? static_cast<int>(dpi / 96) : 1;

  // Bridgeless. React Native's C++ host is the bridgeless one, and there is no
  // bridge here to be the alternative -- so it asserts that.
  facebook::react::ReactNativeFeatureFlags::override(
      std::make_unique<DesktopFeatureFlags>());

  gHost.runLoopObserverManager = std::make_shared<RunLoopObserverManager>();
  gHost.choreographer = std::make_shared<basalt::Win32AnimationChoreographer>();

  // Every mounted transaction has to reach the screen. Windows repaints on
  // demand, so this is the difference between mutations arriving and anything
  // being visible.
  gHost.mountingManager->setOnDidMount(syncPeersAndRepaint);

  // Prime the bounds cache, which `getBounds()` answers from: without this an
  // app's first render sees a window of no size, and only a later resize
  // corrects it. See core/WindowBoundsCache.cpp.
  basalt::notifyWindowBoundsChanged();

  // And the display list, for the same reason and off the same seam: an app
  // asking which screens exist before anything has been plugged or unplugged
  // should not be told there are none.
  basalt::notifyDisplaysChanged();

  // Accepting what the desktop drags onto this window. Per window rather than
  // per view: OLE registers a target against an HWND, and which *app* view is
  // under the pointer is core/DragAndDrop.h's question.
  basalt::attachDropTarget(gHost.main().window, gHost.main().root);

  // Every <TextInput>'s EDIT peer is a child of this window. Set before the
  // first transaction, because a field that mounts without one gets no control
  // and no second chance -- `update` only creates on first sight.
  gHost.mountingManager->setHostWindow(gHost.main().window);

  // Light or dark, and any change to it. Before ReactHost, so the module can
  // answer from the first query.
  basalt::startObservingColorScheme();

  ReactInstanceConfig config;
  // The appId is how this host tells Metro which platform it is; see
  // APP_ID_PREFIX in packages/react-native-basalt/metro-config.js.
  config.appId = "basalt-windows";
  config.deviceName = "windows";

  // Dev mode changes three things at once: loadScript tries Metro before the
  // on-disk bundle; DevServerHelper exists, which is the only condition under
  // which ReactCxxTurboModuleProvider serves the DevSettings module a __DEV__
  // bundle requires; and ReactHost opens a packager connection whose reload
  // message reloads the instance.
  config.enableDevMode = std::getenv("BASALT_DEV") != nullptr;
  gHost.devMode = config.enableDevMode;
  config.enableInspector = config.enableDevMode;
  if (const char *devHost = std::getenv("BASALT_DEV_HOST")) {
    config.devServerHost = devHost;
  }
  if (const char *devPort = std::getenv("BASALT_DEV_PORT")) {
    config.devServerPort = static_cast<uint32_t>(std::strtoul(devPort, nullptr, 10));
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
        makeTurboModuleProviders(
            basalt::scriptURLFor(gHost.bundlePath,
                                 config.enableDevMode,
                                 config.devServerHost,
                                 config.devServerPort,
                                 gHost.sourcePath.empty() ? "index" : gHost.sourcePath,
                                 "windows"),
            config.enableDevMode,
            gHost.mountingManager->imageLoader()),
        // React Native's error inspector. ReactCxxPlatform implements the
        // LogBox TurboModule itself and only provides it when a host hands over
        // one of these; passing null, as this did, left NativeLogBox.show() a
        // call into nothing. See core/LogBoxSurface.h.
        std::make_shared<basalt::LogBoxSurfaceDelegate>(
            [](const std::string &appKey) { showLogBoxSurface(appKey); },
            []() { hideLogBoxSurface(); }),
        // `useNativeDriver: true`. ReactCommon has a C++ implementation of the
        // whole animated graph and ReactCxxPlatform provides the module for it,
        // but only when a host asks by handing over a provider. Without it every
        // native-driven Animated call throws "Native animated module is not
        // available".
        std::make_shared<facebook::react::NativeAnimatedNodesManagerProvider>(),
        installBindings,
        gHost.choreographer);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "could not construct ReactHost: %s\n", error.what());
    return 1;
  }

  // How anything in the core reaches the scheduler, which only the host has.
  // Reanimated is the one caller; see core/UIManagerAccess.h.
  basalt::setEventListenerInstaller(
      [](std::shared_ptr<const facebook::react::EventListener> listener) {
        if (gHost.reactHost == nullptr) {
          return;
        }
        gHost.reactHost->runOnScheduler(
            [listener = std::move(listener)](facebook::react::Scheduler &scheduler) {
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
  // thing they need to see. So the fallback is allowed only for the first case,
  // and the second stops here with Metro's own message.
  const bool loaded = gHost.reactHost->loadScript(gHost.bundlePath, gHost.sourcePath);
  if (const auto devError = basalt::devBundleError()) {
    std::fprintf(stderr,
                 "Metro could not build the bundle (HTTP %ld):\n%s\n",
                 static_cast<long>(devError->status),
                 devError->message.c_str());
    // Through shutdown() rather than straight out: the fallback bundle is
    // already running on the JS thread, and dropping the host without joining
    // it aborts in a destructor instead of exiting.
    shutdown();
    return 1;
  }
  if (!loaded) {
    std::fprintf(stderr, "could not load script: %s\n", gHost.bundlePath.c_str());
    shutdown();
    return 1;
  }
  // Where it came from, not where it might have. See core/DevBundle.h.
  if (const auto from = basalt::devBundleFetched()) {
    std::fprintf(stderr, "loaded script from the packager: %s\n", from->c_str());
  } else {
    std::fprintf(stderr, "loaded script: %s\n", gHost.bundlePath.c_str());
  }

  // The module name decides who drives the surface. Non-empty:
  // SurfaceHandler::start calls AppRegistry.runApplication. Empty: the surface
  // is registered without JS being called at all, leaving it for a script to
  // commit into through nativeFabricUIManager by hand.
  gHost.reactHost->startSurface(kSurfaceId,
                                gHost.moduleName,
                                folly::dynamic::object(),
                                constraintsFor(kInitialWidth, kInitialHeight),
                                layoutContextFor(gHost.scaleFactor));
  gHost.surfaceStarted = true;
  std::fprintf(stderr,
               "started surface %d%s%s\n",
               static_cast<int>(kSurfaceId),
               gHost.moduleName.empty() ? " (no module; raw Fabric script)" : " for module ",
               gHost.moduleName.c_str());

  ShowWindow(gHost.main().window, SW_SHOWNORMAL);
  UpdateWindow(gHost.main().window);

  if (gHost.moduleName.empty()) {
    std::fprintf(stderr, "--- committing tree 1 from JS ---\n");
    callRenderFunction(1);
    SetTimer(gHost.main().window, kSecondTreeTimer, 2000, nullptr);
  }

  UINT scriptedDelayMs = 1500;
  if (const char *taps = std::getenv("BASALT_TEST_TAP")) {
    scriptedDelayMs = scheduleTestPoints(taps, scriptedDelayMs, ScriptedInput::Kind::Tap);
  }
  // BASALT_TEST_SECONDARY_TAP: the other button, which presses nothing and
  // arrives as a pointer event. See core/PointerButtons.h.
  if (const char *taps = std::getenv("BASALT_TEST_SECONDARY_TAP")) {
    scriptedDelayMs = scheduleTestPoints(
        taps, scriptedDelayMs, ScriptedInput::Kind::Tap, basalt::PointerButton::Secondary);
  }
  // BASALT_TEST_HOVER: the pointer moving with no button down. A negative point
  // means it left the window, which is what WM_MOUSELEAVE reports.
  if (const char *hovers = std::getenv("BASALT_TEST_HOVER")) {
    scriptedDelayMs = scheduleTestPoints(hovers, scriptedDelayMs, ScriptedInput::Kind::Hover);
  }
  if (const char *drag = std::getenv("BASALT_TEST_DRAG")) {
    scriptedDelayMs = scheduleTestDrag(drag, scriptedDelayMs);
  }
  if (const char *scrolls = std::getenv("BASALT_TEST_SCROLL")) {
    scriptedDelayMs = scheduleTestScrolls(scrolls, scriptedDelayMs);
  }
  // BASALT_TEST_CLOSE_WINDOW: the surface id of a window to close the way a
  // person would -- its own close button, not the app asking. The two take
  // different paths through the host and only one of them can leave a record
  // whose window is gone, which is why it is worth being able to drive.
  if (const char *closing = std::getenv("BASALT_TEST_CLOSE_WINDOW")) {
    scriptedDelayMs = scheduleScriptedInput(
        ScriptedInput{.kind = ScriptedInput::Kind::Close,
                      .surfaceId = static_cast<facebook::react::SurfaceId>(std::atoi(closing))},
        scriptedDelayMs);
  }

  // BASALT_TEST_DROP: "x,y:path" -- a file dropped at that point. On all three
  // hosts, because a scenario that uses an instrument two of them have is a
  // scenario that fails on the third for a reason that has nothing to do with
  // what it tests. Learned twice: BASALT_TEST_QUIT did the same.
  if (const char *drop = std::getenv("BASALT_TEST_DROP")) {
    const std::string spec(drop);
    const size_t comma = spec.find(',');
    const size_t colon = comma == std::string::npos ? std::string::npos : spec.find(':', comma);
    if (comma == std::string::npos || colon == std::string::npos) {
      std::fprintf(stderr, "BASALT_TEST_DROP: expected x,y:path\n");
    } else {
      scriptedDelayMs = scheduleScriptedInput(
          ScriptedInput{.kind = ScriptedInput::Kind::Drop,
                        .fromX = std::strtod(spec.substr(0, comma).c_str(), nullptr),
                        .fromY = std::strtod(spec.substr(comma + 1, colon - comma - 1).c_str(),
                                             nullptr),
                        .text = spec.substr(colon + 1)},
          scriptedDelayMs);
    }
  }

  // BASALT_TEST_QUIT: ask the application to quit the way a person does. A
  // count rather than a flag, for the reason the other hosts give: one ask
  // cannot show both a refusal and the agreement after it.
  if (const char *asks = std::getenv("BASALT_TEST_QUIT")) {
    const int times = std::max(1, std::atoi(asks));
    for (int ask = 0; ask < times; ask++) {
      scriptedDelayMs = scheduleScriptedInput(
          ScriptedInput{.kind = ScriptedInput::Kind::Quit}, scriptedDelayMs);
    }
  }

  // BASALT_TEST_FOCUS: keyboard actions separated by ';' -- `tab`, `shift-tab`,
  // `activate`, `escape` and `devmenu`. The same reason the other instruments exist: a
  // real Tab needs a window the system considers focused, which an automated
  // run does not reliably have.
  // BASALT_TEST_KEY: a declared shortcut, pressed. The step this host was missing
  // -- the delivery and the WM_KEYDOWN hook were here and nothing scheduled
  // anything, so the scenario's own "did the instrument run at all" guard failed
  // rather than an assertion about keys. Which is what that guard is for.
  for (const basalt::KeyCombination &pressed : basalt::scriptedKeyPresses()) {
    scriptedDelayMs = scheduleScriptedInput(
        ScriptedInput{.kind = ScriptedInput::Kind::Key, .pressed = pressed}, scriptedDelayMs);
  }

  if (const char *focus = std::getenv("BASALT_TEST_FOCUS")) {
    const std::string all(focus);
    size_t start = 0;
    while (start <= all.size()) {
      const size_t semicolon = all.find(';', start);
      const std::string action =
          all.substr(start, semicolon == std::string::npos ? std::string::npos : semicolon - start);
      if (!action.empty()) {
        scriptedDelayMs = scheduleScriptedInput(
            ScriptedInput{.kind = ScriptedInput::Kind::Focus, .text = action}, scriptedDelayMs);
      }
      if (semicolon == std::string::npos) {
        break;
      }
      start = semicolon + 1;
    }
  }

  // BASALT_TEST_TYPE: text for whichever field has focus. Last, so that a tap
  // scheduled above has already put focus somewhere.
  if (const char *text = std::getenv("BASALT_TEST_TYPE")) {
    scriptedDelayMs = scheduleScriptedInput(
        ScriptedInput{.kind = ScriptedInput::Kind::Type, .text = text}, scriptedDelayMs);
  }

  // What the schedule above came to, so BASALT_QUIT_WHEN_SETTLED waits for the
  // last of it rather than only for the first mount. Said once, after everything
  // that moves the total. See core/TestSettle.h.
  basalt::noteScriptedInputEndsInMs(scriptedDelayMs);

  // BASALT_QUIT_AFTER_MS, so an automated run terminates without anyone
  // clicking anything. The same escape hatch both other hosts have.
  if (const char *quitAfter = std::getenv("BASALT_QUIT_AFTER_MS")) {
    const UINT delay = static_cast<UINT>(std::strtoul(quitAfter, nullptr, 10));
    SetTimer(
        gHost.main().window, kQuitAfterTimer, delay, [](HWND hwnd, UINT, UINT_PTR id, DWORD) {
          KillTimer(hwnd, id);
          // The same line the other two hosts log, so a person reading three
          // transcripts side by side sees the same shutdown in each. This
          // host was silent about it, which is how a scenario came to assert
          // on a string only two of them print.
          std::fprintf(stderr, "BASALT_QUIT_AFTER_MS elapsed; quitting\n");
          // What WM_CLOSE does, without going through WM_CLOSE. This is the
          // harness ending the run, not a person closing a window, and an app
          // that intercepts its own close would otherwise refuse it -- a test
          // that asks "does refusing work" by never exiting.
          captureBeforeTeardown();
          DestroyWindow(hwnd);
        });
  }

  // BASALT_TEST_QUIT_FILE: the same shutdown, at a moment the harness picks
  // rather than on a budget fixed before launch. captureBeforeTeardown and
  // DestroyWindow either way, so the widget tree is dumped -- which is the whole
  // reason this instrument exists. TerminateProcess, which is what
  // Popen.terminate() is on this platform, reaches neither, so before this the
  // scenarios that read the tree could not be run here at all.
  //
  // See core/TestQuitFile.h.
  //
  // Two instruments, one timer, because they want the same thing at different
  // moments: the quit file is the harness deciding, BASALT_QUIT_WHEN_SETTLED is
  // the app noticing it has nothing left to do. One timer is also one id, and
  // SetTimer replaces silently on a collision. See core/TestSettle.h.
  if (basalt::testQuitFilePath().has_value() || basalt::quitWhenSettledMs().has_value()) {
    SetTimer(gHost.main().window,
             kQuitFileTimer,
             basalt::kTestQuitFilePollMs,
             [](HWND hwnd, UINT, UINT_PTR id, DWORD) {
               const char *why = nullptr;
               if (basalt::testQuitFileAppeared()) {
                 why = basalt::kTestQuitFileMessage;
               } else if (basalt::hasSettled()) {
                 why = basalt::kQuitWhenSettledMessage;
               } else {
                 return;
               }
               KillTimer(hwnd, id);
               std::fprintf(stderr, "%s\n", why);
               captureBeforeTeardown();
               DestroyWindow(hwnd);
             });
  }

  // Not a plain GetMessage loop: the event beat has to be induced each time the
  // queue empties, and Win32 has no "about to wait" hook to install one into.
  // See win32/Win32RunLoopObserver.h.
  const int exitCode = basalt::runMessageLoopWithBeat(gHost.runLoopObserverManager);

  // Before the window goes: OLE holds a reference to the drop target, and a
  // revoked target is one it can release. Leaving it registered would leave
  // OLE pointing at a destroyed HWND.
  basalt::detachDropTarget(gHost.windows.empty() ? nullptr : gHost.main().window);

  shutdown();
  // After shutdown, which is where the last WIC bitmap is released.
  // OleUninitialize rather than CoUninitialize, to match the OleInitialize
  // above: it undoes both halves.
  OleUninitialize();
  return exitCode;
}

// What is missing is now two components rather than the whole of input:
// <ScrollView> and <TextInput>. Both other hosts have them, and an ordinary
// React screen renders with holes in it without them, which is why this host
// still defaults to the raw-Fabric script rather than to a React app.
//
// Keyboard input is no longer among them. A <TextInput>'s peer is a real child
// window and takes its own keys; everything else is reached by Tab through
// Win32Focus.h, which owns the whole chain because a React Native view here is
// not a window and so has no focus of Windows' own.
