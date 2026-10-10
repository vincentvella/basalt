// basalt-core — the host process.
//
// Constructs a ReactHost, loads a script into Hermes, and runs one surface in
// a GtkWindow. Everything on screen from here on is produced by Fabric: the
// mutation stream arrives through GtkMountingManager exactly as it does on
// iOS and Android, and nothing hand-builds a ShadowViewMutation.
//
// The pieces a host has to supply, and where each one comes from:
//
//   IMountingManager        GtkMountingManager  (this repo)
//   RunLoopObserverManager  ReactCxxPlatform    (event beat)
//   AnimationChoreographer  GtkAnimationChoreographer (this repo, frame clock)
//   ContextContainer        http + websocket client factories, below
//   ComponentRegistryFactory ComponentRegistry (via the mounting manager)
//
// Threading: GTK owns this thread. ReactHost spins up its own JS thread and
// every mount is marshalled back here by GtkMountingManager. Nothing below
// touches a widget off the main thread.

#include "CrashHandler.h"
#include "GtkAnimationChoreographer.h"
#include "AppIdentity.h"
#include "AppPaths.h"
#include "DevMenu.h"
#include "WindowControl.h"
#include "WindowHost.h"
#include "DialogModule.h"
#include "MenuModel.h"
#include "MenuModule.h"
#include "WindowsModule.h"
#include "TestKeys.h"
#include "TestQuitFile.h"
#include "TestSettle.h"
#include "GtkMountingManager.h"

#include <react/io/ImageLoaderModule.h>
#include "GtkRunLoopObserver.h"
#include "GtkFocus.h"
#include "FontScaling.h"
#include "PangoTextLayout.h"
#include "LogBoxSurface.h"

#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>
#include "GtkTouchDispatcher.h"
#include "RnView.h"

#include <glog/logging.h>
#include <jsi/jsi.h>
#include <logger/react_native_log.h>
#include <react/http/IHttpClient.h>
#include <react/http/IWebSocketClient.h>
#include <react/logging/DefaultOnJsErrorHandler.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/runtime/ReactHost.h>
#include <react/runtime/ReactInstanceConfig.h>
#include <react/utils/ContextContainer.h>
#include <react/utils/RunLoopObserverManager.h>

#include <gtk/gtk.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include "AppearanceModule.h"
#include "BlobModule.h"
#include "CoreModules.h"
#include "ColorScheme.h"
#include "DevBundle.h"
#include "DragAndDrop.h"
#include "GtkDropTarget.h"
#include "GtkTitleBar.h"
// Which parts of an app-drawn header drag the window; see the gesture below.
#include "GtkTitleBarLayout.h"
#include "GtkWindowModule.h"

#ifdef BASALT_HAS_SKIA
#include "GtkSkiaModule.h"
#endif
#include "ExpoModules.h"
#include "GestureHandlerModule.h"
#include "SafeAreaComponent.h"
#include "ScreensComponent.h"
#include "ReanimatedModule.h"
#include "JsRuntimeAccess.h"
#include "PackageModules.h"
#include "UIManagerAccess.h"
#include "WorkletsModule.h"
#include "ExpoRuntime.h"
#include "TurboModuleProxy.h"
#include "PlatformConstantsModule.h"
#include "SourceCodeModule.h"
#include "StatusBarModule.h"

#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDefaults.h>

#include <glib-unix.h>

#include <csignal>
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
using facebook::react::Size;
using facebook::react::SurfaceId;

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
// See e2e/demo.js. Only used when no module name is given.
constexpr const char *kRenderFunctionName = "basaltRender";

// ---------------------------------------------------------------------------
// Host state
// ---------------------------------------------------------------------------

// One window, and the surface in it.
//
// A window is a surface: a surface is what has a size, a layout context and a
// root shadow node, and two windows sharing one would be two windows sharing a
// layout. So the id a window is known by *is* its surface id, and there is no
// second numbering to keep in step. See core/WindowHost.h.
//
// Everything here used to be a field on Host, because there was only ever one.
// What is still on Host is what is genuinely per-process -- the ReactHost, the
// mounting manager, the run loop -- and what this project has not yet made
// per-window: the title bar and the error inspector are the main window's.
struct HostWindow {
  facebook::react::SurfaceId surfaceId{0};
  GtkWindow *window{nullptr};
  RnView *root{nullptr};
  // The surface root sits in this, with room above it for the window controls
  // the host draws when the title bar is hidden.
  GtkWidget *overlay{nullptr};
  std::unique_ptr<basalt::GtkTouchDispatcher> touchDispatcher;
  std::unique_ptr<basalt::GtkFocusManager> focusManager;
  int scaleFactor{1};
  // Back to the host, for the GTK callbacks that take one pointer.
  struct Host *host{nullptr};
};

struct Host {
  // Every window, main one first. Never empty after onActivate.
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

  // The GtkApplication, so a window can be made after startup. A window must
  // belong to one, or GTK will not manage it.
  GtkApplication *application{nullptr};
  // What `gtk_application_inhibit` handed back while an app was holding the
  // session open, or 0. See the "query-end" handler.
  guint quitInhibitCookie{0};

  std::shared_ptr<basalt::GtkMountingManager> mountingManager;
  std::shared_ptr<RunLoopObserverManager> runLoopObserverManager;
  std::shared_ptr<basalt::GtkAnimationChoreographer> choreographer;
  // The error inspector's own root when it is showing. It goes above the main
  // window's controls, which is what a modal box should do -- and it is the
  // main window's alone: an error is about the app rather than about a window.
  RnView *logBoxRoot{nullptr};
  std::unique_ptr<ReactHost> reactHost;

  // Drives RunLoopObserverManager::onRender, without which no event an emitter
  // produces ever reaches JavaScript.
  GSource *runLoopObserver{nullptr};

  std::string bundlePath;
  // Empty means the bundle is a raw Fabric script rather than a React app; see
  // startSurface below for what that changes.
  std::string moduleName;
  // Metro's entry point, without the extension. Only meaningful in dev mode:
  // DevServerHelper builds "http://host:port/<sourcePath>.bundle?..." from it,
  // and returns an empty URL if it is unset, which silently falls back to the
  // on-disk bundle.
  std::string sourcePath;
  bool surfaceStarted{false};
  // Whether this run is a development one, which is the only thing that
  // decides whether Ctrl+D opens anything. Kept because the ReactInstanceConfig
  // it came from is not.
  bool devMode{false};
  // Read by main() after the loop ends. GApplication has no exit status to set
  // any more, and a startup that failed has to be tellable from one that ran.
  int exitStatus{0};
};

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

// ReactHost's Logger is where console.log and RN's own logging come out.
void logToGlib(const std::string &message, unsigned int logLevel) {
  switch (logLevel) {
    case ReactNativeLogLevelFatal:
    case ReactNativeLogLevelError:
      g_warning("[js] %s", message.c_str());
      break;
    case ReactNativeLogLevelWarning:
      g_message("[js warn] %s", message.c_str());
      break;
    default:
      g_message("[js] %s", message.c_str());
      break;
  }
}

// ---------------------------------------------------------------------------
// Layout constraints
// ---------------------------------------------------------------------------

// Minimum == maximum pins the root to the window's content box, which is what
// a full-window surface wants: the root fills, and flex children divide it.
// A surface that sized itself to its content would leave minimumSize at zero.
LayoutConstraints constraintsFor(int width, int height) {
  const Size size{.width = static_cast<Float>(width), .height = static_cast<Float>(height)};
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
void callRenderFunction(Host *host, int step) {
  if (host->reactHost == nullptr) {
    return;
  }
  host->reactHost->runOnRuntimeScheduler([step](facebook::jsi::Runtime &runtime) {
    try {
      auto value = runtime.global().getProperty(runtime, kRenderFunctionName);
      if (!value.isObject() || !value.getObject(runtime).isFunction(runtime)) {
        g_warning("script defines no global %s()", kRenderFunctionName);
        return;
      }
      value.getObject(runtime).getFunction(runtime).call(
          runtime,
          facebook::jsi::Value(static_cast<int>(kSurfaceId)),
          facebook::jsi::Value(step));
    } catch (const facebook::jsi::JSError &error) {
      g_warning("%s() threw: %s\n%s",
                kRenderFunctionName,
                error.getMessage().c_str(),
                error.getStack().c_str());
    } catch (const std::exception &error) {
      g_warning("%s() failed: %s", kRenderFunctionName, error.what());
    }
  });
}

// Quitting on a timer is the only way to exercise the shutdown path in
// automation: the window cannot be closed from a script, and killing the
// process skips GApplication::shutdown entirely, so stopAllSurfaces and the
// teardown ordering below it would never run under test. Opt in with
// BASALT_QUIT_AFTER_MS.
gboolean quitAfterTimeout(gpointer data) {
  auto *app = static_cast<GApplication *>(data);
  g_message("BASALT_QUIT_AFTER_MS elapsed; quitting");
  g_application_quit(app);
  return G_SOURCE_REMOVE;
}

// The same quit, when the harness asks for it rather than on a budget fixed
// before the process started. `g_application_quit` either way, so `onShutdown`
// runs and the widget tree is dumped -- which is the whole point of asking
// rather than killing. See core/TestQuitFile.h.
gboolean quitWhenTestFileAppears(gpointer data) {
  // Two instruments, one poll, because they want the same thing at different
  // moments: BASALT_TEST_QUIT_FILE is the harness deciding, and
  // BASALT_QUIT_WHEN_SETTLED is the app noticing it has nothing left to do. A
  // second timer would be a second id to collide and a second place to forget
  // the shutdown ordering.
  const char *why = nullptr;
  if (basalt::testQuitFileAppeared()) {
    why = basalt::kTestQuitFileMessage;
  } else if (basalt::hasSettled()) {
    why = basalt::kQuitWhenSettledMessage;
  } else {
    return G_SOURCE_CONTINUE;
  }
  auto *app = static_cast<GApplication *>(data);
  g_message("%s", why);
  g_application_quit(app);
  return G_SOURCE_REMOVE;
}

// Same clean exit, on SIGINT or SIGTERM, so Ctrl-C and `kill` shut the runtime
// down instead of dropping it. The default disposition would kill the process
// where it stands, skipping GApplication::shutdown and everything under it --
// stopAllSurfaces, the teardown ordering, and the widget tree dump.
//
// g_unix_signal_add is the safe form: it does not run in the signal handler,
// it wakes the main loop and dispatches from there, so ordinary GLib calls are
// allowed here.
gboolean quitOnSignal(gpointer data) {
  auto *app = static_cast<GApplication *>(data);
  g_message("signal received; quitting");
  g_application_quit(app);
  return G_SOURCE_REMOVE;
}

// BASALT_TEST_TAP: "x,y" pairs separated by ';', each fired a second apart.
// See GtkTouchDispatcher::synthesiseTap for why this exists.
//
// A point may name a window: "x,y@3" taps in the window whose surface is 3
// rather than in the app's own. Without it there would be no way to reach a
// second window at all -- each has its own touch dispatcher, which is the whole
// point of them, and the main window's would happily hit-test a tree that is
// not on screen.
//
// BASALT_TEST_SECONDARY_TAP takes the same spec and clicks the other button.
// Its own variable rather than a suffix on a point, because the two assert
// opposite things -- one presses what it lands on and the other must not -- and
// a scenario that mixed them in one string would be harder to read than to
// write.
struct PendingTap {
  Host *host;
  double x;
  double y;
  facebook::react::SurfaceId surfaceId;
  basalt::PointerButton button;
};

gboolean fireTestTap(gpointer data) {
  std::unique_ptr<PendingTap> tap{static_cast<PendingTap *>(data)};
  const bool secondary = tap->button != basalt::PointerButton::Primary;
  g_message("%s: tapping (%.0f, %.0f) in window %d",
            secondary ? "BASALT_TEST_SECONDARY_TAP" : "BASALT_TEST_TAP",
            tap->x,
            tap->y,
            static_cast<int>(tap->surfaceId));
  HostWindow *target = tap->host->windowFor(tap->surfaceId);
  if (target == nullptr) {
    g_warning("BASALT_TEST_TAP: no window %d", static_cast<int>(tap->surfaceId));
    return G_SOURCE_REMOVE;
  }
  if (target->touchDispatcher != nullptr) {
    target->touchDispatcher->synthesiseTap(tap->x, tap->y, tap->button);
  }
  return G_SOURCE_REMOVE;
}

// Splits "x,y@surface" into its point and its window. The window defaults to
// the app's own, so every spec written before windows existed still means what
// it did.
void parseTestPoint(const char *text,
                    double *x,
                    double *y,
                    facebook::react::SurfaceId *surfaceId) {
  *surfaceId = kSurfaceId;
  char **halves = g_strsplit(text, "@", 2);
  if (halves[1] != nullptr) {
    *surfaceId = static_cast<facebook::react::SurfaceId>(g_ascii_strtoll(halves[1], nullptr, 10));
  }
  char **parts = g_strsplit(halves[0], ",", 2);
  *x = parts[0] != nullptr ? g_ascii_strtod(parts[0], nullptr) : 0.0;
  *y = parts[1] != nullptr ? g_ascii_strtod(parts[1], nullptr) : 0.0;
  const bool complete = parts[0] != nullptr && parts[1] != nullptr;
  g_strfreev(parts);
  g_strfreev(halves);
  if (!complete) {
    *x = -1.0;
    *y = -1.0;
  }
}

// Returns the delay after the last tap, so typing can be scheduled behind it.
guint scheduleTestTaps(Host *host,
                       const char *spec,
                       guint delayMs,
                       basalt::PointerButton button) {
  char **points = g_strsplit(spec, ";", -1);
  for (char **point = points; *point != nullptr; ++point) {
    if (**point == '\0') {
      continue;
    }
    double x = 0.0;
    double y = 0.0;
    facebook::react::SurfaceId surfaceId = kSurfaceId;
    parseTestPoint(*point, &x, &y, &surfaceId);
    if (x >= 0.0 && y >= 0.0) {
      g_timeout_add(delayMs, fireTestTap, new PendingTap{host, x, y, surfaceId, button});
      delayMs += 1000;
    }
  }
  g_strfreev(points);
  return delayMs;
}

// BASALT_TEST_HOVER: "x,y" points separated by ';', each fired a second apart.
// The pointer moving with no button down, which is the only way to reach the
// hover path from automation: a real cursor move needs a seat with input
// devices, and the whole point of these instruments is a run that has none. A
// negative point means the pointer left the surface.
//
// Reuses PendingTap: the payload is the same, and a second struct with the same
// three fields would be two things to keep in step.
gboolean fireTestHover(gpointer data) {
  std::unique_ptr<PendingTap> hover{static_cast<PendingTap *>(data)};
  g_message("BASALT_TEST_HOVER: hovering (%.0f, %.0f)", hover->x, hover->y);
  if (hover->host->main().touchDispatcher != nullptr) {
    hover->host->main().touchDispatcher->synthesiseHover(hover->x, hover->y);
  }
  return G_SOURCE_REMOVE;
}

// Returns the delay after the last point.
guint scheduleTestHovers(Host *host, const char *spec, guint delayMs) {
  char **points = g_strsplit(spec, ";", -1);
  for (char **point = points; *point != nullptr; ++point) {
    char **parts = g_strsplit(*point, ",", 2);
    if (parts[0] != nullptr && parts[1] != nullptr) {
      g_timeout_add(delayMs,
                    fireTestHover,
                    new PendingTap{host,
                                   g_ascii_strtod(parts[0], nullptr),
                                   g_ascii_strtod(parts[1], nullptr),
                                   // Hover has no per-window form: the pointer
                                   // is one thing, and a second window's hover
                                   // is a feature nothing has asked for yet.
                                   kSurfaceId,
                                   // Nor a button: a hovering pointer presses
                                   // nothing, and this field is only read by
                                   // the tap path.
                                   basalt::PointerButton::Primary});
      delayMs += 1000;
    }
    g_strfreev(parts);
  }
  g_strfreev(points);
  return delayMs;
}

// BASALT_TEST_FOCUS: keyboard actions separated by ';' -- `tab`, `shift-tab`,
// `activate`, `escape` and `devmenu`, each fired a second apart.
//
// The same reason the other instruments exist, one step further out. A real Tab
// needs a window the display server considers focused, and an automated run on
// a headless compositor has no such thing: the key event is delivered to
// nothing. This enters at GtkFocusManager, so it exercises GTK's own focus
// chain, the focus and blur events and the click dispatch, and skips only the
// delivery of the keystroke itself.
struct PendingFocus {
  Host *host;
  std::string action;
};

gboolean fireTestFocus(gpointer data) {
  std::unique_ptr<PendingFocus> pending{static_cast<PendingFocus *>(data)};
  g_message("BASALT_TEST_FOCUS: %s", pending->action.c_str());
  if (pending->host->main().focusManager == nullptr) {
    return G_SOURCE_REMOVE;
  }
  if (pending->action == "tab") {
    pending->host->main().focusManager->moveFocus(true);
  } else if (pending->action == "shift-tab") {
    pending->host->main().focusManager->moveFocus(false);
  } else if (pending->action == "activate") {
    pending->host->main().focusManager->activateFocused();
  } else if (pending->action == "devmenu") {
    // Also not a focus action. Same instrument for the same reason: a key that
    // needs nothing focused to arrive. What it opens is answered by
    // BASALT_TEST_MENU; see core/TestDialog.h.
    basalt::showDevMenu(pending->host->reactHost.get());
  } else if (pending->action == "escape") {
    // Not a focus action, and here anyway: this is the instrument for "a key
    // was pressed and nothing on the window has to be focused for it to
    // arrive", which is exactly what Escape closing a <Modal> is.
    pending->host->mountingManager->requestCloseTopModal();
  } else {
    g_warning("BASALT_TEST_FOCUS: unknown action \"%s\"", pending->action.c_str());
  }
  return G_SOURCE_REMOVE;
}

guint scheduleTestFocus(Host *host, const char *spec, guint delayMs) {
  char **actions = g_strsplit(spec, ";", -1);
  for (char **action = actions; *action != nullptr; ++action) {
    if (**action == '\0') {
      continue;
    }
    g_timeout_add(delayMs, fireTestFocus, new PendingFocus{host, *action});
    delayMs += 1000;
  }
  g_strfreev(actions);
  return delayMs;
}

// BASALT_TEST_SCROLL: "x,y,lines" triples separated by ';' -- a wheel over a
// point, a second apart, in surface-root coordinates. Positive lines scroll
// down, which is the direction `contentOffset` reads.
//
// The AppKit and Win32 hosts have had this since their scroll views did; this
// is the third, and it arrived with <RefreshControl>, whose gesture is a wheel
// that keeps asking to go up after the list has already reached its top.
struct PendingScroll {
  Host *host;
  double x;
  double y;
  double lines;
};

gboolean fireTestScroll(gpointer data) {
  std::unique_ptr<PendingScroll> pending{static_cast<PendingScroll *>(data)};
  Host *host = pending->host;
  if (host->mountingManager == nullptr || host->main().root == nullptr) {
    return G_SOURCE_REMOVE;
  }
  g_message("BASALT_TEST_SCROLL: %g lines at (%g, %g)", pending->lines, pending->x, pending->y);
  // Notches into pixels with the same constant a real wheel uses, so the
  // instrument and the hardware move a list by the same distance.
  host->mountingManager->scrollViews().scrollAt(
      host->main().root, pending->x, pending->y, 0.0, pending->lines * basalt::GtkScrollViewManager::kWheelStepPixels);
  return G_SOURCE_REMOVE;
}

guint scheduleTestScrolls(Host *host, const char *spec, guint delayMs) {
  char **steps = g_strsplit(spec, ";", -1);
  for (char **step = steps; *step != nullptr; ++step) {
    char **parts = g_strsplit(*step, ",", -1);
    if (g_strv_length(parts) == 3) {
      g_timeout_add(delayMs,
                    fireTestScroll,
                    new PendingScroll{host,
                                      g_ascii_strtod(parts[0], nullptr),
                                      g_ascii_strtod(parts[1], nullptr),
                                      g_ascii_strtod(parts[2], nullptr)});
      delayMs += 1000;
    }
    g_strfreev(parts);
  }
  g_strfreev(steps);
  return delayMs;
}

// BASALT_TEST_DRAG: "x1,y1,x2,y2" -- one press, twenty moves and a release, for
// the gestures a tap cannot reach. See GtkTouchDispatcher::synthesiseDrag.
struct PendingDrag {
  Host *host;
  double fromX;
  double fromY;
  double toX;
  double toY;
};

gboolean fireTestDrag(gpointer data) {
  std::unique_ptr<PendingDrag> drag{static_cast<PendingDrag *>(data)};
  g_message("BASALT_TEST_DRAG: (%.0f, %.0f) -> (%.0f, %.0f)",
            drag->fromX,
            drag->fromY,
            drag->toX,
            drag->toY);
  if (drag->host->main().touchDispatcher != nullptr) {
    drag->host->main().touchDispatcher->synthesiseDrag(
        drag->fromX, drag->fromY, drag->toX, drag->toY, 20);
  }
  return G_SOURCE_REMOVE;
}

void scheduleTestDrag(Host *host, const char *spec, guint delayMs) {
  char **parts = g_strsplit(spec, ",", 4);
  if (g_strv_length(parts) == 4) {
    g_timeout_add(delayMs,
                  fireTestDrag,
                  new PendingDrag{host,
                                  g_ascii_strtod(parts[0], nullptr),
                                  g_ascii_strtod(parts[1], nullptr),
                                  g_ascii_strtod(parts[2], nullptr),
                                  g_ascii_strtod(parts[3], nullptr)});
  }
  g_strfreev(parts);
}

// BASALT_TEST_TYPE: text to insert into whatever field has focus, for the
// same reason BASALT_TEST_TAP exists -- macOS cannot synthesise a real key
// event without accessibility permission an automated run does not have.
//
// It inserts through GtkEditable rather than through GDK, so it skips the key
// controller and the input method and exercises everything above them: the
// changed signal, the event emitter, React's re-render, and the controlled
// value coming back down. On Linux the end-to-end suite types with xdotool
// instead, which does go through GDK.
struct PendingType {
  Host *host;
  std::string text;
};

// BASALT_TEST_KEY's, for the same reason: a timeout carries one pointer and this
// needs the host as well as the press.
struct PendingKey {
  Host *host;
  basalt::KeyCombination pressed;
};

gboolean fireTestKey(gpointer data) {
  auto *pending = static_cast<PendingKey *>(data);
  g_message("BASALT_TEST_KEY: %s", pending->pressed.key.c_str());
  if (pending->host->main().focusManager != nullptr) {
    pending->host->main().focusManager->deliverTestKey(pending->pressed);
  }
  delete pending;
  return G_SOURCE_REMOVE;
}

gboolean fireTestType(gpointer data) {
  std::unique_ptr<PendingType> pending{static_cast<PendingType *>(data)};
  g_message("BASALT_TEST_TYPE: typing \"%s\"", pending->text.c_str());

  GtkWidget *focus = pending->host->main().window != nullptr
      ? gtk_window_get_focus(pending->host->main().window)
      : nullptr;
  if (focus == nullptr || !GTK_IS_TEXT(focus)) {
    g_warning("BASALT_TEST_TYPE: no text field has focus");
    return G_SOURCE_REMOVE;
  }

  int position = -1;
  gtk_editable_insert_text(GTK_EDITABLE(focus),
                           pending->text.c_str(),
                           static_cast<int>(pending->text.size()),
                           &position);
  return G_SOURCE_REMOVE;
}

gboolean commitSecondTree(gpointer data) {
  auto *host = static_cast<Host *>(data);
  g_message("--- committing tree 2 from JS ---");
  callRenderFunction(host, 2);
  return G_SOURCE_REMOVE;
}

// Ctrl+D: React Native's developer menu.
//
// The shortcut is React Native's own on a simulator, which is the closest
// thing a desktop has to a phone's shake. It does nothing in a release run --
// there is no dev server to reload from and no debugger to open -- so the key
// is left to travel on rather than swallowed.
gboolean onDevMenuKey(GtkEventControllerKey * /*controller*/,
                      guint keyval,
                      guint /*keycode*/,
                      GdkModifierType state,
                      gpointer data) {
  auto *host = static_cast<Host *>(data);
  if (!host->devMode || host->reactHost == nullptr) {
    return GDK_EVENT_PROPAGATE;
  }
  if ((keyval != GDK_KEY_d && keyval != GDK_KEY_D) ||
      (state & GDK_CONTROL_MASK) == 0) {
    return GDK_EVENT_PROPAGATE;
  }
  basalt::showDevMenu(host->reactHost.get());
  return GDK_EVENT_STOP;
}

// ---------------------------------------------------------------------------
// Window size -> surface constraints
// ---------------------------------------------------------------------------

// Per window: `data` is the HostWindow whose root resized, not the Host.
//
// Which is the whole of what multiple windows changes here. Constraining
// `kSurfaceId` from every window would lay the *first* window's tree out to the
// second window's size, and the symptom would be a first window whose content
// jumped whenever a second one was dragged.
void onRootResized(RnView * /*view*/, int width, int height, gpointer data) {
  auto *made = static_cast<HostWindow *>(data);
  Host *host = made->host;
  if (width <= 0 || height <= 0 || !host->surfaceStarted) {
    return;
  }
  g_debug("surface %d constraints -> %dx%d", static_cast<int>(made->surfaceId), width, height);
  host->reactHost->setSurfaceConstraints(
      made->surfaceId, constraintsFor(width, height), layoutContextFor(made->scaleFactor));

  // A <Modal> is sized from its own shadow-node state rather than from a style,
  // and React Native's C++ platform answers "what size is the screen" with
  // zero. Told here, where the window's size is already being handed to Fabric.
  host->mountingManager->setSurfaceSize(static_cast<float>(width), static_cast<float>(height));
  // The window moved or was resized, which `useWindow()` hands an app as live
  // bounds. Here rather than in a watcher of its own: this is already the one
  // place that learns it.
  basalt::notifyWindowBoundsChanged();

  // The inspector covers the main window, so it resizes with it -- and only
  // with it: an error is about the app rather than about a window, and it is
  // shown in the one the app started in.
  if (host->logBoxRoot != nullptr && made->surfaceId == kSurfaceId) {
    rn_view_set_frame(host->logBoxRoot, 0, 0, static_cast<float>(width), static_cast<float>(height));
    host->reactHost->setSurfaceConstraints(
        kLogBoxSurfaceId, constraintsFor(width, height), layoutContextFor(made->scaleFactor));
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
void hideLogBoxSurface(Host *host) {
  if (host->reactHost == nullptr || host->logBoxRoot == nullptr) {
    return;
  }
  host->reactHost->stopSurface(kLogBoxSurfaceId);
  gtk_overlay_remove_overlay(GTK_OVERLAY(host->main().overlay), GTK_WIDGET(host->logBoxRoot));
  host->mountingManager->destroySurfaceRoot(kLogBoxSurfaceId);
  host->logBoxRoot = nullptr;
}

void showLogBoxSurface(Host *host, const std::string &appKey) {
  if (host->reactHost == nullptr || host->main().overlay == nullptr || host->logBoxRoot != nullptr) {
    return;
  }

  const int width = gtk_widget_get_width(GTK_WIDGET(host->main().root));
  const int height = gtk_widget_get_height(GTK_WIDGET(host->main().root));
  if (width <= 0 || height <= 0) {
    return;
  }

  host->logBoxRoot = host->mountingManager->createSurfaceRoot(kLogBoxSurfaceId);
  rn_view_set_frame(host->logBoxRoot, 0, 0, static_cast<float>(width), static_cast<float>(height));
  // Added after the window controls, so it is above them: an error box that a
  // close button sits on top of is one the user can close by accident.
  gtk_widget_set_halign(GTK_WIDGET(host->logBoxRoot), GTK_ALIGN_FILL);
  gtk_widget_set_valign(GTK_WIDGET(host->logBoxRoot), GTK_ALIGN_FILL);
  gtk_overlay_add_overlay(GTK_OVERLAY(host->main().overlay), GTK_WIDGET(host->logBoxRoot));

  host->reactHost->startSurface(kLogBoxSurfaceId,
                                appKey,
                                folly::dynamic::object(),
                                constraintsFor(width, height),
                                layoutContextFor(host->main().scaleFactor));
}

// The frame clock only exists once a widget is realised, so the choreographer
// cannot be attached at construction time.
void onRootMapped(GtkWidget *widget, gpointer data) {
  auto *host = static_cast<Host *>(data);
  if (host->choreographer != nullptr) {
    host->choreographer->attachToWidget(widget);
  }
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

// Feature flags this platform states explicitly. Anything not listed here keeps
// React Native's own default, which is what the base class provides.
class LinuxFeatureFlags : public facebook::react::ReactNativeFeatureFlagsDefaults {
 public:
  bool enableBridgelessArchitecture() override {
    return true;
  }
};

// TurboModules this platform supplies itself. ReactCxxTurboModuleProvider
// consults these before its own, so naming a module it also provides replaces
// it. Today that is only PlatformConstants; this is also the seam an Expo port
// would use.
facebook::react::TurboModuleProviders makeTurboModuleProviders(
    std::string scriptURL,
    bool devMode,
    std::weak_ptr<basalt::GtkImageLoader> imageLoader) {
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
#ifdef BASALT_HAS_SKIA
        // Skia, when the build was pointed at an app that has it. Its
        // `install` is what puts Skia's whole JavaScript API on the global
        // object, and an app that imports Skia does not start without it;
        // see gtk/GtkSkiaModule.h.
        if (name == basalt::GtkSkiaModule::kModuleName) {
          return std::make_shared<basalt::GtkSkiaModule>(jsInvoker);
        }
#endif
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
        // The title bar. Reaches the window through the one GtkTitleBar, which
        // the host attached at startup -- a module cannot be handed a window it
        // is created before.
        if (name == basalt::GtkWindowModule::kModuleName) {
          return std::make_shared<basalt::GtkWindowModule>(jsInvoker);
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

  // ReactHost throws without these two. ReactCxxPlatform's own implementations
  // are real ones -- curl-backed http and a websocket client -- so a host that
  // is not a test harness should use them rather than stubs. The DevTools
  // variants default to the same pair when left unset.
  contextContainer->insert(facebook::react::HttpClientFactoryKey,
                           facebook::react::getHttpClientFactory());
  contextContainer->insert(facebook::react::WebSocketClientFactoryKey,
                           facebook::react::getWebSocketClientFactory());

  // MessageQueueThreadFactoryKey is deliberately left unset: ReactHost then
  // installs MessageQueueThreadImpl, a real threaded queue, which is what we
  // want. Fantom overrides it only to make test execution deterministic.
  return contextContainer;
}

// Makes a window, a surface root for it, and the input that drives them.
//
// Called for the main window and for every one an app opens afterwards, which
// is the point: the second window is not a special case of the first, it is the
// same function with a different surface id. See core/WindowHost.h.
//
// The window is not shown and the surface is not started here. The main window
// waits for the bundle to load and an app's window waits for its component to
// be named, and both of those are the caller's business.
HostWindow *createHostWindow(Host *host,
                             GtkApplication *app,
                             facebook::react::SurfaceId surfaceId,
                             const char *title,
                             int width,
                             int height) {
  // The first window through here is the app's own, and a few things belong to
  // it alone rather than to every window: the title bar, the frame clock the
  // animation choreographer runs on, and the error inspector.
  const bool isMainWindow = host->windows.empty();

  auto owned = std::make_unique<HostWindow>();
  HostWindow *made = owned.get();
  made->surfaceId = surfaceId;
  made->host = host;

  made->window = GTK_WINDOW(gtk_application_window_new(app));
  gtk_window_set_title(made->window, title);
  gtk_window_set_default_size(made->window, width, height);
  made->scaleFactor = gtk_widget_get_scale_factor(GTK_WIDGET(made->window));

  // Fabric emits no Create for a surface root -- the root shadow node is the
  // base of every diff, so it has to exist before the surface starts.
  made->root = host->mountingManager->createSurfaceRoot(surfaceId);

  // The surface root, with room above it for the window controls the host
  // draws when the title bar is hidden. GTK takes the whole titlebar away with
  // the decorations -- buttons included -- where Windows and macOS keep
  // drawing theirs over the app's content, so on this desktop the host has to
  // put them back. An overlay is how: the root fills the window and the
  // controls sit over its top corner.
  made->overlay = gtk_overlay_new();
  gtk_overlay_set_child(GTK_OVERLAY(made->overlay), GTK_WIDGET(made->root));

  GtkWidget *controls = gtk_window_controls_new(GTK_PACK_END);
  gtk_widget_set_halign(controls, GTK_ALIGN_END);
  gtk_widget_set_valign(controls, GTK_ALIGN_START);
  // Hidden until an app asks for the hidden style; see GtkTitleBar.h.
  gtk_widget_set_visible(controls, FALSE);
  gtk_overlay_add_overlay(GTK_OVERLAY(made->overlay), controls);

  gtk_window_set_child(made->window, made->overlay);
  // The title bar is the main window's. It is a process-wide seam -- one title,
  // one style -- and making it per-window is its own piece of work; see
  // docs/BACKLOG.md.
  if (isMainWindow) {
    basalt::titleBar().attach(made->window, controls);

    // A press inside a region the app marked with <TitleBar.DragRegion> hands
    // the window to the window manager's own move loop.
    //
    // This is the host's job rather than a method JavaScript calls, because
    // gdk_toplevel_begin_move needs the device, button and timestamp of the
    // press it is taking over, and by the time a JS handler could run those
    // are gone. See GtkTitleBar::startDrag, which says so at more length.
    //
    // Windows needs none of this: it answers HTCAPTION from WM_NCHITTEST and
    // the system does the rest. GTK has no equivalent hook, so the gesture is
    // the equivalent -- and it asks the same question of the same nativeIDs,
    // which is what keeps the two desktops agreeing about what drags.
    GtkGesture *dragRegions = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dragRegions), GDK_BUTTON_PRIMARY);
    // Capture, so this is asked before the app's own views handle the press.
    // In the bubble phase a <Pressable> inside a drag region would have taken
    // it already, and the window would never move.
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(dragRegions),
                                               GTK_PHASE_CAPTURE);
    g_signal_connect(
        dragRegions,
        "pressed",
        G_CALLBACK(+[](GtkGestureClick *gesture,
                       int /*nPress*/,
                       double x,
                       double y,
                       gpointer data) {
          auto *self = static_cast<HostWindow *>(data);
          if (self == nullptr || self->root == nullptr || self->window == nullptr) {
            return;
          }
          // Only the hidden style. With GTK's own decorations the titlebar is
          // a real widget above the app, dragging it already works, and an
          // app's marked region is just a view.
          if (basalt::titleBar().metrics().style != basalt::TitleBarStyle::Hidden) {
            return;
          }
          if (!basalt::isTitleBarDragRegionAt(self->root, x, y)) {
            return;
          }

          GtkEventController *const controller = GTK_EVENT_CONTROLLER(gesture);
          GdkDevice *const device = gtk_event_controller_get_current_event_device(controller);
          const guint32 timestamp = gtk_event_controller_get_current_event_time(controller);
          const int button =
              static_cast<int>(gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)));

          // The gesture reports the root's coordinates; GDK wants the
          // surface's. They differ by the root's offset within the window and
          // again by the surface transform, which is where a compositor puts
          // client-side shadows -- ignore either and the window jumps by that
          // much as the drag starts.
          // compute_point rather than translate_coordinates, which GTK 4.12
          // deprecated and this build treats as an error. Brace-initialised
          // because GRAPHENE_POINT_INIT is a C99 compound literal that C++
          // rejects -- the same trap GtkMountingManager records for
          // GRAPHENE_SIZE_INIT -- and the result is checked because the
          // function is warn-unused-result: false means the two widgets share
          // no ancestry, which a window mid-teardown really can produce.
          // Assigned field by field rather than brace-initialised, because this
          // whole lambda is one argument to the G_CALLBACK macro below and the
          // preprocessor counts commas at paren depth without understanding
          // braces: `{a, b}` reads as two macro arguments and the expansion
          // fails with "too many arguments provided to function-like macro".
          // Empty braces are safe, which is why the same type in
          // GtkTouchDispatcher.cpp raises nothing.
          graphene_point_t inRoot{};
          inRoot.x = static_cast<float>(x);
          inRoot.y = static_cast<float>(y);
          graphene_point_t inWindow{};
          if (!gtk_widget_compute_point(
                  GTK_WIDGET(self->root), GTK_WIDGET(self->window), &inRoot, &inWindow)) {
            return;
          }
          double originX = 0;
          double originY = 0;
          gtk_native_get_surface_transform(GTK_NATIVE(self->window), &originX, &originY);

          basalt::titleBar().beginMoveDrag(device,
                                           button,
                                           inWindow.x + originX,
                                           inWindow.y + originY,
                                           timestamp);

          // Claimed: the move loop now owns the pointer, and leaving the
          // sequence unclaimed would also deliver this press to the view under
          // it once the drag ended.
          gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        }),
        made);
    gtk_widget_add_controller(GTK_WIDGET(made->root), GTK_EVENT_CONTROLLER(dragRegions));
  }

  rn_view_set_resize_callback(made->root, onRootResized, made);

  // Maximised and full screen, which change the bounds and not the size of the
  // surface -- so the resize callback above never runs for them. GTK has no
  // move notification and no position to report; see GtkWindowControl.cpp.
  const auto onWindowState = +[](GObject * /*window*/, GParamSpec * /*spec*/, gpointer /*data*/) {
    basalt::notifyWindowBoundsChanged();
  };
  // Somebody is trying to close this window: its own close button, the window
  // manager, or Alt+F4. Every window, including the app's own -- an app that
  // wants to ask "are you sure" wants to ask hardest about the one it is
  // running in.
  g_signal_connect(made->window,
                   "close-request",
                   G_CALLBACK(+[](GtkWindow *window, gpointer data) -> gboolean {
                     (void)window;
                     auto *closing = static_cast<HostWindow *>(data);

                     // Asked to be asked first. Refuse, and tell the app; what
                     // happens next is its decision, and if that is "go ahead"
                     // it calls close() itself. See core/WindowHost.h for why
                     // the answer has to be available before the question.
                     if (basalt::hostWindowCloseIntercepted(closing->surfaceId)) {
                       basalt::hostWindowCloseRequested(closing->surfaceId);
                       return TRUE;
                     }

                     // The main window closing ends the process, which
                     // GtkApplication already arranges -- so nothing here has
                     // to, and unhandled is how it gets to.
                     if (closing->surfaceId == kSurfaceId) {
                       return FALSE;
                     }

                     // Closed by the person rather than by the app. Without
                     // this the host keeps a record whose GtkWindow is gone --
                     // a dangling pointer rather than a stale flag -- and the
                     // `<Window>` that opened it goes on believing it is open.
                     //
                     // Tell JavaScript, then take the window down the same way
                     // an app closing it would, so there is one teardown path
                     // rather than two.
                     basalt::hostWindowClosed(closing->surfaceId);
                     basalt::closeHostWindow(closing->surfaceId);
                     // Handled: closeHostWindow destroys the window once the
                     // surface has stopped, and letting GTK destroy it now
                     // would be the race that teardown exists to avoid.
                     return TRUE;
                   }),
                   made);

  g_signal_connect(made->window, "notify::maximized", G_CALLBACK(onWindowState), nullptr);
  g_signal_connect(made->window, "notify::fullscreened", G_CALLBACK(onWindowState), nullptr);
  // The animation choreographer runs on a frame clock, and a widget only has
  // one once it is realised -- hence the "map". The *main* window's, and only
  // its: attaching to every window's meant the last one opened stole the clock
  // from the first, so animations followed whichever window appeared most
  // recently and stopped when it closed. It also left a handler connected to a
  // frame clock that was about to be finalised, which GLib complained about at
  // shutdown and was how this was noticed at all.
  if (isMainWindow) {
    g_signal_connect(made->root, "map", G_CALLBACK(onRootMapped), host);
  }

  // Input, per window. Attached to this window's root, which is where its hit
  // testing starts -- a second window with the first one's dispatcher would
  // deliver every press to the wrong tree.
  made->touchDispatcher =
      std::make_unique<basalt::GtkTouchDispatcher>(host->mountingManager.get(), made->root);
  // The keyboard half: Tab reaching a <Pressable>, and Enter activating it.
  made->focusManager =
      std::make_unique<basalt::GtkFocusManager>(host->mountingManager.get(), made->root);

  // Ctrl+D, which is React Native's developer menu. Its own controller rather
  // than another case in the focus manager's: this has nothing to do with
  // focus, and a shortcut that works wherever the caret is has to be on the
  // window in the capture phase anyway -- otherwise a <TextInput> with the
  // keyboard would eat it.
  GtkEventController *devKeys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(devKeys, GTK_PHASE_CAPTURE);
  g_signal_connect(devKeys, "key-pressed", G_CALLBACK(onDevMenuKey), host);
  gtk_widget_add_controller(GTK_WIDGET(made->window), devKeys);

  host->windows.push_back(std::move(owned));
  return made;
}

// The host's half of core/WindowHost.h.
//
// A single global, because a process has one host and one GtkApplication and
// the seam takes neither. The alternative -- threading a Host through a
// TurboModule -- would mean the module knowing what a Host is.
Host *gWindowHost = nullptr;

// The desktop's text scale changed while the app was running.
//
// New text picks the new scale up; text already laid out does not reflow,
// because nothing here tells Fabric the measurements it cached are stale. The
// call that would is a dirty-layout on every paragraph, which React Native
// spells as a `ShadowTree` commit with `forceRemount`; backlog/text.md records
// it rather than this pretending to do it.
void onTextScaleChanged(GtkSettings *settings, GParamSpec * /*pspec*/, gpointer /*data*/) {
  basalt::setSystemFontScale(basalt::gtkTextScale(settings));
}

void onActivate(GtkApplication *app, gpointer data) {
  auto *host = static_cast<Host *>(data);

  // Constructed here, on the GTK main thread: GtkMountingManager records this
  // thread and asserts that every widget mutation lands back on it.
  host->mountingManager = std::make_shared<basalt::GtkMountingManager>();

  // The application's own window, which is windows.front() from here on and is
  // what everything not yet per-window means by "the window".
  host->application = app;
  gWindowHost = host;
  createHostWindow(host, app, kSurfaceId, "basalt-core", kInitialWidth, kInitialHeight);

  // The desktop's text scale, on this thread because GtkSettings belongs to it
  // and text is measured off it. See core/FontScaling.h, which is where
  // `allowFontScaling` decides whether a fragment honours this at all.
  GtkSettings *settings = gtk_settings_get_default();
  basalt::setSystemFontScale(basalt::gtkTextScale(settings));
  if (settings != nullptr) {
    g_signal_connect(settings, "notify::gtk-xft-dpi", G_CALLBACK(onTextScaleChanged), nullptr);
  }

  // Prime the bounds cache, which `getBounds()` answers from: without this an
  // app's first render sees a window of no size, and only a later resize
  // corrects it. See core/WindowBoundsCache.cpp.
  basalt::notifyWindowBoundsChanged();

  // And the display list, for the same reason and off the same seam: an app
  // asking which screens exist before anything has been plugged or unplugged
  // should not be told there are none.
  basalt::notifyDisplaysChanged();

  // Accepting what the desktop drags onto this window. On the surface root
  // rather than per view: GTK delivers a drop to the controller under the
  // pointer, and which *app* view that is is core/DragAndDrop.h's question.
  basalt::attachDropTarget(host->main().root);

  // And dragging out of it. A separate controller on the same widget: GTK
  // keeps the two directions apart and so does the marker -- a view may be
  // both, and most are neither.
  basalt::attachDragSource(host->main().root);

  host->runLoopObserverManager = std::make_shared<RunLoopObserverManager>();
  host->choreographer = std::make_shared<basalt::GtkAnimationChoreographer>();

  // Light or dark, and any change to it. Before ReactHost, so the module can
  // answer the first `Appearance.getColorScheme()` a bundle makes, which for an
  // Expo app is during its first import.
  basalt::startObservingColorScheme();

  // Before ReactHost, so the beat is being induced from the first event on.
  host->runLoopObserver = basalt::installRunLoopObserver(host->runLoopObserverManager);

  // Say what this host needs rather than inheriting a default that moves.
  //
  // HermesInstance gives the runtime a microtask queue only when
  // enableBridgelessArchitecture() is true, and React's scheduler enqueues a
  // microtask on its first render. That flag defaults to true on React Native
  // `main` and false on 0.87.1, so the same code that works against main throws
  // "Could not enqueue microtask because they are disabled in this runtime" on
  // a release, before anything appears on screen. This host is bridgeless --
  // there is no bridge here to be the alternative -- so it should have been
  // asserting that all along.
  //
  // iOS and Android override feature flags at startup too; relying on the
  // default was the anomaly.
  facebook::react::ReactNativeFeatureFlags::override(
      std::make_unique<LinuxFeatureFlags>());

  ReactInstanceConfig config;
  // The appId is how this host tells Metro which platform it is. ReactCxxPlatform's
  // DevServerHelper builds its bundle URL from `constexpr DEFAULT_PLATFORM =
  // "android"` with no hook and no setting, and `app=` -- which comes from
  // here, and which Metro itself ignores -- is the only thing in that URL a
  // host controls. The Metro plugin reads it back and corrects the platform.
  // See APP_ID_PREFIX in packages/basalt-core/metro-config.js.
  config.appId = "basalt-linux";
  config.deviceName = "linux";

  // Dev mode changes three things at once, which is worth being explicit about:
  // loadScript tries Metro before the on-disk bundle; DevServerHelper exists,
  // which is the only condition under which ReactCxxTurboModuleProvider serves
  // the DevSettings module a __DEV__ bundle requires; and ReactHost opens a
  // packager connection whose reload message reloads the instance.
  config.enableDevMode = g_getenv("BASALT_DEV") != nullptr;
  host->devMode = config.enableDevMode;
  config.enableInspector = config.enableDevMode;
  if (const char *devHost = g_getenv("BASALT_DEV_HOST")) {
    config.devServerHost = devHost;
  }
  if (const char *devPort = g_getenv("BASALT_DEV_PORT")) {
    config.devServerPort = static_cast<uint32_t>(g_ascii_strtoull(devPort, nullptr, 10));
  }
  if (config.enableDevMode) {
    g_message("dev mode: Metro at %s:%u, entry '%s'",
              config.devServerHost.c_str(),
              config.devServerPort,
              host->sourcePath.c_str());
    // So the http client can tell the bundle request apart from an app's own
    // fetch, and refuse to feed a Metro error page to the JS engine. See
    // core/DevBundle.h.
    basalt::setDevServerOrigin(config.devServerHost, config.devServerPort);
  }

  try {
    host->reactHost = std::make_unique<ReactHost>(config,
                                                  host->mountingManager,
                                                  host->runLoopObserverManager,
                                                  makeContextContainer(),
                                                  facebook::react::getDefaultOnJsErrorFunc(),
                                                  logToGlib,
                                                  nullptr,
                                                  makeTurboModuleProviders(
                                                      basalt::scriptURLFor(
                                                      host->bundlePath,
                                                      config.enableDevMode,
                                                      config.devServerHost,
                                                      config.devServerPort,
                                                      host->sourcePath.empty() ? "index"
                                                                               : host->sourcePath,
                                                      "linux"),
                                                                  config.enableDevMode,
                                                                  host->mountingManager
                                                                      ->imageLoader()),
                                                  // React Native's error
                                                  // inspector. ReactCxxPlatform
                                                  // implements the LogBox
                                                  // TurboModule itself and only
                                                  // provides it when a host
                                                  // hands over one of these;
                                                  // passing null, as this did,
                                                  // left NativeLogBox.show() a
                                                  // call into nothing. See
                                                  // core/LogBoxSurface.h.
                                                  std::make_shared<basalt::LogBoxSurfaceDelegate>(
                                                      [host](const std::string &appKey) {
                                                        showLogBoxSurface(host, appKey);
                                                      },
                                                      [host]() { hideLogBoxSurface(host); }),
                                                  // `useNativeDriver: true`.
                                                  // ReactCommon has a C++
                                                  // implementation of the whole
                                                  // animated graph and
                                                  // ReactCxxPlatform provides
                                                  // the module for it, but only
                                                  // when a host asks by handing
                                                  // over a provider. Without it
                                                  // every native-driven
                                                  // Animated call throws
                                                  // "Native animated module is
                                                  // not available".
                                                  std::make_shared<
                                                      facebook::react::
                                                          NativeAnimatedNodesManagerProvider>(),
                                                  installBindings,
                                                  host->choreographer);
  } catch (const std::exception &error) {
    g_error("could not construct ReactHost: %s", error.what());
  }

  // How anything in the core reaches the scheduler, which only the host has.
  // Reanimated is the one caller; see core/UIManagerAccess.h.
  basalt::setEventListenerInstaller(
      [host](std::shared_ptr<const facebook::react::EventListener> listener) {
        if (host->reactHost == nullptr) {
          return;
        }
        host->reactHost->runOnScheduler(
            [listener = std::move(listener)](facebook::react::Scheduler &scheduler) {
              scheduler.addEventListener(listener);
            });
      });

  // And how anything in the core reaches the JavaScript thread. An Expo
  // module is given no call invoker, so this is the only way one can emit an
  // event or settle a promise later; see core/JsRuntimeAccess.h.
  basalt::setRuntimeRunner(
      [host](std::function<void(facebook::jsi::Runtime &)> work) {
        if (host->reactHost == nullptr) {
          return;
        }
        host->reactHost->runOnRuntimeScheduler(std::move(work));
      });

  // `loadScript` falls back to the on-disk bundle whenever the Metro fetch
  // fails, which is right when nothing is listening and wrong when Metro
  // answered with an error: running the last bundle that built, while the
  // developer looks at source that is not what is executing, hides the very
  // thing they need to see. So the fallback is allowed only for the first case,
  // and the second stops here with Metro's own message.
  const bool loaded = host->reactHost->loadScript(host->bundlePath, host->sourcePath);
  if (const auto devError = basalt::devBundleError()) {
    g_warning("Metro could not build the bundle (HTTP %ld):\n%s",
              devError->status,
              devError->message.c_str());
    // Non-zero, so a script that starts this host can tell a build failure from
    // a run that ended. The AppKit host returns 1 from main for the same reason.
    host->exitStatus = 1;
    g_application_quit(G_APPLICATION(gtk_window_get_application(host->main().window)));
    return;
  }
  if (!loaded) {
    g_warning("could not load script: %s", host->bundlePath.c_str());
    gtk_window_present(host->main().window);
    return;
  }
  // Where it came from, not where it might have. See core/DevBundle.h.
  if (const auto from = basalt::devBundleFetched()) {
    g_message("loaded script from the packager: %s", from->c_str());
  } else {
    g_message("loaded script: %s", host->bundlePath.c_str());
  }

  // The module name decides who drives the surface.
  //
  // Non-empty: SurfaceHandler::start calls AppRegistry.runApplication, React
  // mounts the registered component, and everything after this point is
  // ordinary React Native.
  //
  // Empty: the surface is registered without JS being called at all, leaving it
  // for a script to commit into through nativeFabricUIManager by hand. That is
  // how this host ran before there was a Metro bundle, and e2e/demo.js still
  // exercises it.
  host->reactHost->startSurface(kSurfaceId,
                                host->moduleName,
                                folly::dynamic::object(),
                                constraintsFor(kInitialWidth, kInitialHeight),
                                layoutContextFor(host->main().scaleFactor));
  host->surfaceStarted = true;
  g_message("started surface %d%s%s",
            static_cast<int>(kSurfaceId),
            host->moduleName.empty() ? " (no module; raw Fabric script)" : " for module ",
            host->moduleName.c_str());

  gtk_window_present(host->main().window);

  if (host->moduleName.empty()) {
    g_message("--- committing tree 1 from JS ---");
    callRenderFunction(host, 1);
    g_timeout_add(2000, commitSecondTree, host);
  }

  guint scriptedDelayMs = 1500;
  if (const char *taps = g_getenv("BASALT_TEST_TAP")) {
    scriptedDelayMs = scheduleTestTaps(host, taps, scriptedDelayMs, basalt::PointerButton::Primary);
  }
  // BASALT_TEST_SECONDARY_TAP: the other button, which presses nothing and
  // arrives as a pointer event. See core/PointerButtons.h.
  if (const char *taps = g_getenv("BASALT_TEST_SECONDARY_TAP")) {
    scriptedDelayMs =
        scheduleTestTaps(host, taps, scriptedDelayMs, basalt::PointerButton::Secondary);
  }
  if (const char *hovers = g_getenv("BASALT_TEST_HOVER")) {
    scriptedDelayMs = scheduleTestHovers(host, hovers, scriptedDelayMs);
  }
  if (const char *scrolls = g_getenv("BASALT_TEST_SCROLL")) {
    scriptedDelayMs = scheduleTestScrolls(host, scrolls, scriptedDelayMs);
  }
  // BASALT_TEST_CLOSE_WINDOW: the surface id of a window to close the way a
  // person would -- its own close button, not the app asking. The two take
  // different paths through the host and only one of them can leave a record
  // whose window is gone, which is why it is worth being able to drive.
  if (const char *closing = g_getenv("BASALT_TEST_CLOSE_WINDOW")) {
    const auto surfaceId = static_cast<facebook::react::SurfaceId>(g_ascii_strtoll(closing, nullptr, 10));
    struct PendingClose {
      Host *host;
      facebook::react::SurfaceId surfaceId;
    };
    g_timeout_add(
        scriptedDelayMs,
        +[](gpointer data) -> gboolean {
          std::unique_ptr<PendingClose> pending{static_cast<PendingClose *>(data)};
          g_message("BASALT_TEST_CLOSE_WINDOW: closing window %d",
                    static_cast<int>(pending->surfaceId));
          HostWindow *target = pending->host->windowFor(pending->surfaceId);
          if (target != nullptr) {
            // `close`, not `destroy`: this is the request a close button makes,
            // so the close-request handler runs exactly as it would for a
            // person.
            gtk_window_close(target->window);
          }
          return G_SOURCE_REMOVE;
        },
        new PendingClose{host, surfaceId});
    scriptedDelayMs += 1000;
  }

  // BASALT_TEST_DROP: "x,y:path" -- a file dropped at that point, reported the
  // way the toolkit would report it.
  //
  // Entered below GTK rather than through it, and the reason is the same one
  // BASALT_TEST_TAP has: a real drag needs a source outside this process, and
  // there is no way to conjure one from inside a test. What this does exercise
  // is everything this platform owns -- the hit test, the ancestor walk, the
  // accept check, the event, and React's half -- which is all of the code
  // that could be wrong about *which view* is told and *what* it is told.
  // What it does not exercise is GtkDropTarget itself.
  if (const char *drop = g_getenv("BASALT_TEST_DROP")) {
    struct PendingDrop {
      Host *host;
      std::string spec;
    };
    g_timeout_add(
        scriptedDelayMs,
        +[](gpointer data) -> gboolean {
          std::unique_ptr<PendingDrop> pending{static_cast<PendingDrop *>(data)};
          const std::string &spec = pending->spec;
          const size_t comma = spec.find(',');
          const size_t colon = spec.find(':', comma == std::string::npos ? 0 : comma);
          if (comma == std::string::npos || colon == std::string::npos) {
            g_message("BASALT_TEST_DROP: expected x,y:path");
            return G_SOURCE_REMOVE;
          }
          const double x = g_ascii_strtod(spec.substr(0, comma).c_str(), nullptr);
          const double y = g_ascii_strtod(spec.substr(comma + 1, colon - comma - 1).c_str(), nullptr);
          const std::string path = spec.substr(colon + 1);

          basalt::DragPayload payload;
          payload.files.push_back(path);

          const facebook::react::Tag found =
              basalt::dropTargetAt(pending->host->main().root, x, y, basalt::DropAcceptsFiles);
          g_message("BASALT_TEST_DROP: %s at %g,%g onto tag %d",
                    path.c_str(), x, y, static_cast<int>(found));
          if (found == 0) {
            return G_SOURCE_REMOVE;
          }
          basalt::DropEvent event;
          event.tag = found;
          event.phase = basalt::DropPhase::Drop;
          event.x = x;
          event.y = y;
          event.payload = payload;
          basalt::reportDrop(event);
          return G_SOURCE_REMOVE;
        },
        new PendingDrop{host, drop});
    scriptedDelayMs += 1000;
  }

  // BASALT_TEST_QUIT: the session ending, which is what quitting means on this
  // desktop -- there is no Cmd-Q for a GTK app to intercept. Emitting the
  // signal directly rather than arranging a real logout, for the obvious
  // reason; what it exercises is everything downstream of the signal, which is
  // all of this platform's half.
  //
  // A count rather than a flag, for the reason the AppKit host gives: one ask
  // cannot show both halves.
  if (const char *asks = g_getenv("BASALT_TEST_QUIT")) {
    const long times = std::max(1L, static_cast<long>(g_ascii_strtoll(asks, nullptr, 10)));
    for (long ask = 0; ask < times; ask++) {
      g_timeout_add(
          scriptedDelayMs,
          +[](gpointer data) -> gboolean {
            auto *self = static_cast<Host *>(data);
            g_message("BASALT_TEST_QUIT: ending the session");
            if (self != nullptr && self->application != nullptr) {
              g_signal_emit_by_name(self->application, "query-end");
            }
            return G_SOURCE_REMOVE;
          },
          host);
      scriptedDelayMs += 1000;
    }
  }
  // BASALT_TEST_KEY: a declared shortcut, pressed. Enters after the keyval
  // translation; see core/TestKeys.h for what that does and does not prove.
  for (const basalt::KeyCombination &pressed : basalt::scriptedKeyPresses()) {
    g_timeout_add(scriptedDelayMs, fireTestKey, new PendingKey{host, pressed});
    scriptedDelayMs += 400;
  }

  if (const char *focus = g_getenv("BASALT_TEST_FOCUS")) {
    scriptedDelayMs = scheduleTestFocus(host, focus, scriptedDelayMs);
  }
  if (const char *drag = g_getenv("BASALT_TEST_DRAG")) {
    scheduleTestDrag(host, drag, scriptedDelayMs);
    scriptedDelayMs += 1000;
  }
  if (const char *text = g_getenv("BASALT_TEST_TYPE")) {
    g_timeout_add(scriptedDelayMs, fireTestType, new PendingType{host, text});
  }

  // What the schedule above came to, so that BASALT_QUIT_WHEN_SETTLED waits for
  // the last of it rather than only for the first mount. Said once, here, after
  // everything that moves the total. See core/TestSettle.h.
  basalt::noteScriptedInputEndsInMs(scriptedDelayMs);
  g_unix_signal_add(SIGINT, quitOnSignal, app);
  g_unix_signal_add(SIGTERM, quitOnSignal, app);

  if (const char *quitAfter = g_getenv("BASALT_QUIT_AFTER_MS")) {
    const gint64 ms = g_ascii_strtoll(quitAfter, nullptr, 10);
    if (ms > 0) {
      g_timeout_add(static_cast<guint>(ms), quitAfterTimeout, app);
    }
  }

  // BASALT_TEST_QUIT_FILE and BASALT_QUIT_WHEN_SETTLED, polled together.
  // Registered only when one of them is set, so a path whose file never appears
  // leaves a harmless timer rather than changing how the app behaves.
  if (basalt::testQuitFilePath().has_value() || basalt::quitWhenSettledMs().has_value()) {
    g_timeout_add(basalt::kTestQuitFilePollMs, quitWhenTestFileAppears, app);
  }
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
  const char *path = g_getenv("BASALT_DUMP_MENU");
  if (path == nullptr) {
    return;
  }
  const std::string described = basalt::describeApplicationMenu();
  GError *error = nullptr;
  if (!g_file_set_contents(path, described.c_str(), static_cast<gssize>(described.size()), &error)) {
    g_warning("could not write the menu dump: %s", error != nullptr ? error->message : "?");
    g_clear_error(&error);
  }
}

// BASALT_DUMP_TREE: write the widget tree to a file on the way out, so an
// automated run can assert on what React actually produced rather than on a
// screenshot. See docs/TESTING.md.
void dumpTreeIfRequested(Host *host) {
  const char *path = g_getenv("BASALT_DUMP_TREE");
  if (path == nullptr || host->main().root == nullptr) {
    return;
  }
  GString *out = g_string_new(nullptr);
  char *appTree = rn_view_describe_tree(host->main().root);
  g_string_append(out, appTree);
  g_free(appTree);

  // Every other window, each under a header naming its surface. Appended rather
  // than merged for the same reason the inspector is: they are separate trees
  // on screen, and nesting one inside another would say something untrue.
  for (const auto &other : host->windows) {
    if (other->surfaceId == kSurfaceId || other->root == nullptr) {
      continue;
    }
    g_string_append_printf(out, "--- window %d ---\n", static_cast<int>(other->surfaceId));
    char *otherTree = rn_view_describe_tree(other->root);
    g_string_append(out, otherTree);
    g_free(otherTree);
  }

  // The error inspector is a second surface with a root of its own, so it is
  // invisible to a dump of the app's. Same separator the AppKit host writes.
  if (host->logBoxRoot != nullptr) {
    g_string_append(out, "--- LogBox ---\n");
    char *logBoxTree = rn_view_describe_tree(host->logBoxRoot);
    g_string_append(out, logBoxTree);
    g_free(logBoxTree);
  }
  char *description = g_string_free(out, FALSE);
  GError *error = nullptr;
  if (g_file_set_contents(path, description, -1, &error) == FALSE) {
    g_warning("could not write %s: %s", path, error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
  } else {
    g_message("wrote widget tree to %s", path);
  }
  g_free(description);
}

void onShutdown(GApplication * /*app*/, gpointer data) {
  auto *host = static_cast<Host *>(data);

  // Before the surface stops, which tears the tree down.
  dumpTreeIfRequested(host);
  dumpMenuIfRequested();

  if (host->choreographer != nullptr) {
    host->choreographer->detach();
  }
  // Stop feeding the beat before the manager it points at is released.
  basalt::removeRunLoopObserver(host->runLoopObserver);
  host->runLoopObserver = nullptr;
  // Every window, not only the app's own -- which is what this said when there
  // was only ever one, and did not say when there stopped being.
  //
  // A dispatcher left alive past `mountingManager.reset()` below is the crash
  // this file just fixed in the other direction: GTK goes on delivering
  // crossings and key presses while the widgets come down, and the handler
  // reads an event emitter out of a manager that has been freed. The
  // destructors disconnect, so the order here is what decides whether anything
  // is still listening.
  for (const auto &window : host->windows) {
    window->focusManager.reset();
    window->touchDispatcher.reset();
  }
  // Before the host goes: work posted to a host on its way out must not run.
  basalt::setRuntimeRunner(nullptr);
  if (host->reactHost != nullptr) {
    // Surfaces must stop before the host goes away, or teardown asserts.
    host->reactHost->stopAllSurfaces();
    host->surfaceStarted = false;
    // Destroying the host joins the JS thread, so it has to happen while the
    // mounting manager it holds is still alive.
    host->reactHost.reset();
  }
  host->choreographer.reset();
  host->runLoopObserverManager.reset();
  // The window records before the manager that made their roots. Each holds a
  // borrowed `RnView *` root and a resize callback carrying the record itself,
  // and a record outliving the manager is the same hazard one line up.
  host->windows.clear();
  host->mountingManager.reset();
}

} // namespace

namespace basalt {

// core/WindowHost.h, on GTK.
//
// The surface id is allocated here rather than by JavaScript: it is Fabric's
// number, the mounting manager keys its roots by it, and two windows racing to
// pick one would be two windows sharing a root. Odd ids only, above the ones
// this host reserves -- the app's surface is 1 and the error inspector's is 2.
facebook::react::SurfaceId openHostWindow(const NewWindowOptions &options) {
  Host *host = gWindowHost;
  if (host == nullptr || host->application == nullptr || !host->surfaceStarted ||
      options.component.empty()) {
    return 0;
  }

  static facebook::react::SurfaceId nextSurfaceId = kLogBoxSurfaceId + 1;
  const facebook::react::SurfaceId surfaceId = nextSurfaceId++;

  const int width = static_cast<int>(options.width);
  const int height = static_cast<int>(options.height);
  HostWindow *made = createHostWindow(host,
                                      host->application,
                                      surfaceId,
                                      options.title.empty() ? "" : options.title.c_str(),
                                      width,
                                      height);

  // The frame has to exist before the surface starts, for the same reason the
  // main window's does: Fabric lays the root out against the constraints it is
  // given here and the widget has not been allocated yet.
  rn_view_set_frame(made->root, 0, 0, static_cast<float>(width), static_cast<float>(height));

  host->reactHost->startSurface(surfaceId,
                                options.component,
                                options.props,
                                constraintsFor(width, height),
                                layoutContextFor(made->scaleFactor));
  gtk_window_present(made->window);
  g_message("opened window %d for module %s",
            static_cast<int>(surfaceId),
            options.component.c_str());
  return surfaceId;
}

// Lets the session manager get on with it.
//
// Taken in the "query-end" handler and dropped here, so a logout an app
// refused does not leave the session wedged if the app later quits by some
// other route -- or never answers at all.
void releaseQuitInhibit(Host *host) {
  if (host == nullptr || host->application == nullptr || host->quitInhibitCookie == 0) {
    return;
  }
  gtk_application_uninhibit(host->application, host->quitInhibitCookie);
  host->quitInhibitCookie = 0;
}

// Ends the application. `g_application_quit` rather than closing every
// window: it stops the main loop whatever is open, which is what a person
// choosing Quit means, and it runs the "shutdown" handler on the way out.
void quitHost() {
  Host *host = gWindowHost;
  if (host == nullptr || host->application == nullptr) {
    return;
  }
  // Whatever the session manager was told to wait for, it is over.
  releaseQuitInhibit(host);
  g_application_quit(G_APPLICATION(host->application));
}

void closeHostWindow(facebook::react::SurfaceId surfaceId) {
  Host *host = gWindowHost;
  // The main window is not closed this way: destroying the surface an app is
  // running in is not the same thing as closing its window, and an app that
  // means the second should say so through `close()` on the window itself.
  if (host == nullptr || surfaceId == kSurfaceId || surfaceId == kLogBoxSurfaceId) {
    return;
  }

  if (host->windowFor(surfaceId) == nullptr) {
    return;
  }

  // Stopping a surface unmounts its React tree, which produces one last
  // transaction of Remove and Delete mutations. Those arrive on the UI thread
  // afterwards, and the widgets they name have to still be there when they do.
  host->reactHost->stopSurface(surfaceId);

  // So the window is destroyed a round trip later: out to the JavaScript
  // thread, which is where the teardown runs, and back to this one, which is
  // where its mutations are applied. Both queues are ordered, so anything the
  // stop produced is ahead of this.
  //
  // Destroying the root immediately instead is survivable -- MountingWalk
  // treats a mutation naming a tag it does not have as a warning, and says
  // that a transaction racing a surface teardown is exactly what it is for --
  // but it logs four of them per window and says something went wrong when
  // nothing did.
  host->reactHost->runOnRuntimeScheduler([host, surfaceId](facebook::jsi::Runtime &) {
    basalt::postToUiThread([host, surfaceId] {
      HostWindow *going = host->windowFor(surfaceId);
      if (going == nullptr) {
        return;
      }
      // The dispatchers before the widgets they hold: both keep a borrowed
      // root and disconnect from it on the way out.
      going->focusManager.reset();
      going->touchDispatcher.reset();
      gtk_window_destroy(going->window);
      host->mountingManager->destroySurfaceRoot(surfaceId);

      for (auto it = host->windows.begin(); it != host->windows.end(); ++it) {
        if (it->get() == going) {
          host->windows.erase(it);
          break;
        }
      }
      g_message("closed window %d", static_cast<int>(surfaceId));
    });
  });
}

std::vector<facebook::react::SurfaceId> hostWindows() {
  std::vector<facebook::react::SurfaceId> open;
  if (gWindowHost == nullptr) {
    return open;
  }
  open.reserve(gWindowHost->windows.size());
  for (const auto &candidate : gWindowHost->windows) {
    open.push_back(candidate->surfaceId);
  }
  return open;
}

} // namespace basalt

int main(int argc, char **argv) {

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

  Host host;
  host.bundlePath = argc > 1 ? argv[1] : "build/main.jsbundle.js";
  host.moduleName = argc > 2 ? argv[2] : "BasaltDemo";

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
  const char *sourcePath = g_getenv("BASALT_DEV_ENTRY");
  host.sourcePath = sourcePath != nullptr ? sourcePath : "index";

  // The app's Expo config, if this is an Expo app and it has been bundled.
  // Before anything installs the runtime. See core/ExpoModules.h.
  basalt::loadExpoAppConfigBeside(host.bundlePath);

  // What this app calls itself, which a generic host binary cannot know. Read
  // before the GtkApplication, because the application id is construct-only and
  // is what a desktop matches a running window to its `.desktop` file with --
  // and therefore what puts the app's own name and icon on its notifications.
  // See core/AppIdentity.h and cli/packageApp.js.
  const basalt::AppIdentity &identity = basalt::appIdentity(host.bundlePath);

  // GTK would try to interpret argv as files to open. The bundle path is ours,
  // so the application never sees it.
  //
  // `dev.basalt.host` is the fallback rather than the rule: an app that was
  // started through `react-native run-linux` has an identity file and gets its
  // own id. GTK requires a valid D-Bus name, and an identifier that is not one
  // would make gtk_application_new fail outright, so it is checked first.
  const char *applicationId =
      (!identity.identifier.empty() && g_application_id_is_valid(identity.identifier.c_str()))
      ? identity.identifier.c_str()
      : "dev.basalt.host";
  GtkApplication *app = gtk_application_new(applicationId, G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(onActivate), &host);
  g_signal_connect(app, "shutdown", G_CALLBACK(onShutdown), &host);

  // A monitor plugged in, unplugged or rearranged. The monitor list is a
  // GListModel, so this is its "items-changed" rather than a signal of its
  // own -- GTK 4 has no monitors-changed on GdkDisplay.
  if (GdkDisplay *display = gdk_display_get_default()) {
    if (GListModel *monitors = gdk_display_get_monitors(display)) {
      g_signal_connect(monitors,
                       "items-changed",
                       G_CALLBACK(+[](GListModel *, guint, guint, guint, gpointer) {
                         basalt::notifyDisplaysChanged();
                       }),
                       nullptr);
    }
  }

  // The session ending -- logout, shutdown, reboot -- which is the only thing
  // on this desktop that corresponds to macOS's Cmd-Q.
  //
  // What GTK does *not* have is an application-level quit gesture to
  // intercept. There is no Cmd-Q: a GTK app ends when its last window closes,
  // and that is a close-request on a window, which `useCloseRequest` already
  // guards. So this handler is the whole of the difference between the two
  // halves here, where on macOS the difference is most of the feature.
  //
  // `gtk_application_inhibit` is what actually holds the session back;
  // answering the signal is not enough on its own, and an app that only
  // listened would be told the session is ending and then lose to it. The
  // cookie is taken when the app asks to intercept and dropped when it stops
  // -- see quitInterceptionChanged below.
  g_signal_connect(app,
                   "query-end",
                   G_CALLBACK(+[](GApplication * /*application*/, gpointer data) {
                     if (!basalt::hostQuitIntercepted()) {
                       return;
                     }
                     auto *self = static_cast<Host *>(data);
                     // GTK's own documentation for this signal says to call
                     // inhibit from inside the handler, which is why the
                     // cookie is taken here rather than when the app
                     // registered: answering the signal alone does not hold
                     // the session, and an app that only listened would be
                     // told the session is ending and then lose to it.
                     if (self != nullptr && self->application != nullptr &&
                         self->quitInhibitCookie == 0) {
                       self->quitInhibitCookie =
                           gtk_application_inhibit(self->application,
                                                   gtk_application_get_active_window(self->application),
                                                   GTK_APPLICATION_INHIBIT_LOGOUT,
                                                   "the application has unsaved work");
                     }
                     basalt::hostQuitRequested();
                   }),
                   &host);

  const int status = g_application_run(G_APPLICATION(app), 1, argv);
  g_object_unref(app);
  return host.exitStatus != 0 ? host.exitStatus : status;
}
