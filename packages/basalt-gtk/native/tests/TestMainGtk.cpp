// The GTK test suite's entry point.
//
// Separate from the runner because it is the only part of the suite that knows
// about a toolkit: every test here touches widgets, and a GtkWidget cannot be
// constructed before GTK is initialised. The macOS suite has its own.

#include "TestHarness.h"

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

  return basalt::testing::runAllTests(argc, argv);
}
