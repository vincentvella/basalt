// The GTK test suite's entry point.
//
// Separate from the runner because it is the only part of the suite that knows
// about a toolkit: every test here touches widgets, and a GtkWidget cannot be
// constructed before GTK is initialised. The macOS suite has its own.

#include "TestHarness.h"

#include "CrashHandler.h"

#include <gtk/gtk.h>

#include <iostream>

int main(int argc, char **argv) {

  // No window is ever presented, but GTK still needs a display connection: on a
  // headless machine run this under a nested or virtual display server. See
  // docs/TESTING.md.
  if (gtk_init_check() == FALSE) {
    std::cerr << "could not initialise GTK; these tests need a display.\n"
              << "On a headless Linux box: xvfb-run -a ./build/basalt_gtk_tests\n";
    return 77; // The convention automake uses for "skipped", not "failed".
  }

  // The same handler the hosts install, for the same reason: a suite that dies
  // on a segfault prints a truncated line and an exit code, and a suite that
  // installs this prints the frames.
  //
  // It earned its place the day it went in. A use-after-free in
  // `~GtkFocusManager` had been latent for months, surfacing only when an
  // unrelated field was added to RnView's struct and moved what the freed bytes
  // happened to be; what the suite said was "exit 139" after a passing test, and
  // what this said was `SIGSEGV at 0xaaaaaaaaaaaaaaaa` -- GLib's poison -- three
  // frames under `RN_IS_VIEW`.
  basalt::installCrashHandler();
  return basalt::testing::runAllTests(argc, argv);
}
