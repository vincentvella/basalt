// Does the shared core actually stand on its own?
//
// `basalt_core` compiles without a toolkit on the include path, which says
// nothing about whether it *links* without one. A header that is never included
// cannot betray you; an undefined symbol at link time can.
//
// So this is a program that links the core and nothing platform-specific, and
// touches each module the core provides. If it links, a new desktop platform
// can build the core as-is. If it does not, the missing symbols are exactly the
// list of what that platform has to implement -- which is the more useful
// outcome of the two, and the reason this prints them rather than asserting.
//
// It is not a test of behaviour. It never constructs a runtime and never
// renders anything. It answers one question: what does a second platform owe?
//
// What it deliberately does not include is any capability package's header.
// A package's core half is compiled into `basalt_core`, so its seam is a
// symbol this has to satisfy -- but an app installs the packages it wants and
// core has to compile in one that installed none. So a package brings its own
// stub, through BASALT_PACKAGE_PROBE_SOURCES; see
// packages/basalt-notifications/native/probe_stub.cpp. Including
// one here made core fail to compile in an app without that package, and the
// failure read as a missing file in core rather than as a dependency core
// should never have had.

#include "AppearanceModule.h"
#include "ColorScheme.h"
#include "CoreModules.h"
#include "ComponentRegistry.h"
#include "ExpoRuntime.h"
#include "FontRegistry.h"
#include "PlatformConstantsModule.h"
#include "SourceCodeModule.h"
#include "PlatformServices.h"
#include "MenuModel.h"
#include "WindowControl.h"
#include "WindowHost.h"
#include "StatusBarModule.h"
#include "WorkletsModule.h"

#include <cstdio>
#include <string>
#include <vector>

// The seam, stubbed, so that the link answers the question fully rather than
// stopping at the first thing missing. A real platform implements these against
// fontconfig, Core Text or DirectWrite; a platform that has not yet gets this
// far and no further, which is the point.
namespace basalt {
bool registerFont(const std::string &, const std::string &) {
  return false;
}
std::string resolveFontFamily(const std::string &name) {
  return name;
}
bool isFontRegistered(const std::string &) {
  return false;
}
unsigned long fontGeneration() {
  return 0;
}
std::string registeredFontNamesJoined(char) {
  return {};
}

// The colour-scheme seam, stubbed the same way. A real platform answers from
// NSAppearance, GtkSettings or the Windows registry; one that has not yet gets
// this far and no further.
ColorScheme systemColorScheme() {
  return ColorScheme::Light;
}
void startObservingColorScheme() {}

// The platform-services seam, stubbed the same way: clipboard, opening a URL
// and showing a dialog are the three things a core module cannot do alone.
std::string clipboardText() {
  return {};
}
void setClipboardText(const std::string &) {}
bool canOpenUrl(const std::string &) {
  return false;
}
bool openUrl(const std::string &) {
  return false;
}
void showAlert(const AlertRequest &, AlertCallback) {}
// A file dialog and a share sheet, stubbed the same way. Both answer "the person
// cancelled", which is what a platform that cannot ask them should say.
//
// Missing here until a capability package called one. The probe only ever sees
// the symbols something in core's link closure *references*, and nothing in core
// reached these two -- the dialog module and the share module are both only
// reachable from a host. So the seam was one a new port owed and the probe did
// not say so, which is the one thing this file exists to prevent.
void showFileDialog(const FileDialogRequest &, FileDialogCallback onDone) {
  if (onDone) {
    onDone(true, {});
  }
}
void shareContent(const ShareRequest &, ShareCallback onDone) {
  if (onDone) {
    onDone(ShareOutcome::Dismissed, {});
  }
}
// A popup menu, stubbed the same way. Answering -1 is the seam's "dismissed",
// which is the honest reply from a platform that cannot show one.
void showMenu(const MenuRequest &, MenuCallback onChosen) {
  if (onChosen) {
    onChosen(-1);
  }
}
// And its roles. A platform that cannot show a menu performs nothing in one, so
// every role is unsupported here and `entriesFrom` drops them all -- which is
// the same answer this file gives everywhere: no, and in the right shape.
bool menuRoleSupported(const std::string &) {
  return false;
}
void performMenuRole(const std::string &) {}
// Added when the gesture recognisers arrived: "run this on the UI thread", now
// and later. A platform with a run loop has both already; this one has neither
// and says so.
// The window seam, stubbed the same way. A real platform answers from a
// GtkWindow, an NSWindow or an HWND; one that has not yet reports a window of
// no size, which is what an app reading bounds before there is one sees.
WindowBounds windowBounds() {
  return {};
}
void setWindowSize(double, double) {}
void setWindowPosition(double, double) {}
void centerWindow() {}
void setWindowFullScreen(bool) {}
void applyWindowSizeLimits() {}
void setWindowResizable(bool) {}
void setWindowAlwaysOnTop(bool) {}
WindowCapabilities windowCapabilities() {
  // A platform with no window manager does none of them, which is the honest
  // answer rather than a convenient one: this probe exists to say what a new
  // port owes, and four falses is the starting position.
  return WindowCapabilities{};
}

// The application menu, stubbed the same way. A platform with no menu bar
// answers exactly this, which is also what GTK answers for real.
bool applicationMenuSupported() {
  return false;
}
void setApplicationMenu(const MenuModel &, std::function<void(int)>) {}
std::string describeApplicationMenu() {
  return {};
}

// The window seam's other half: opening one. A platform that cannot answers
// zero, which every caller reads as "no window was opened".
facebook::react::SurfaceId openHostWindow(const NewWindowOptions &) {
  return 0;
}
void closeHostWindow(facebook::react::SurfaceId) {}
std::vector<facebook::react::SurfaceId> hostWindows() {
  return {};
}

void postDelayed(double, std::function<void()>) {}
void postToUiThread(std::function<void()>) {}
bool isUiThread() {
  return false;
}
} // namespace basalt

int main() {
  using basalt::DesktopPlatformConstantsModule;
  using basalt::DesktopAccessibilityInfoModule;
  using basalt::DesktopAlertModule;
  using basalt::DesktopAppearanceModule;
  using basalt::DesktopClipboardModule;
  using basalt::DesktopI18nManagerModule;
  using basalt::DesktopLinkingModule;
  using basalt::DesktopSourceCodeModule;
  using basalt::DesktopStatusBarModule;

  // Each of these is a symbol the linker must find in the core alone.
  const std::string url =
      basalt::scriptURLFor("bundle.js", false, "localhost", 8081, "index", "probe");
  const bool expo = basalt::hasExpoRuntime();
  const bool worklets = basalt::hasWorklets();

  // The seams: declared by the core, implemented by a platform. On a platform
  // that has not implemented them, the link fails here and names them.
  const bool registered = basalt::isFontRegistered("nothing");
  const unsigned long generation = basalt::fontGeneration();
  const char *scheme = basalt::colorSchemeName(basalt::effectiveColorScheme());

  // Named so the link has to find each module the core claims to provide.
  std::printf("core links.\n");
  // Named so the link has to find each module the core claims to provide.
  std::printf("  modules           %s, %s, %s, %s,\n",
              std::string(DesktopPlatformConstantsModule::kModuleName).c_str(),
              std::string(DesktopSourceCodeModule::kModuleName).c_str(),
              std::string(DesktopStatusBarModule::kModuleName).c_str(),
              std::string(DesktopAppearanceModule::kModuleName).c_str());
  std::printf("                    %s, %s, %s, %s, %s\n",
              std::string(DesktopClipboardModule::kModuleName).c_str(),
              std::string(DesktopAlertModule::kModuleName).c_str(),
              std::string(DesktopLinkingModule::kModuleName).c_str(),
              std::string(DesktopI18nManagerModule::kModuleName).c_str(),
              std::string(DesktopAccessibilityInfoModule::kModuleName).c_str());
  std::printf("  scriptURLFor      %s\n", url.c_str());
  std::printf("  expo runtime      %s\n", expo ? "compiled in" : "not compiled in");
  std::printf("  worklets          %s\n", worklets ? "compiled in" : "not compiled in");
  std::printf("  font seam         isFontRegistered=%d generation=%lu\n",
              registered ? 1 : 0,
              generation);
  std::printf("  colour scheme     %s\n", scheme);
  std::printf("  services seam     canOpenUrl=%d clipboard=\"%s\"\n",
              basalt::canOpenUrl("https://example.com") ? 1 : 0,
              basalt::clipboardText().c_str());
  return 0;
}
