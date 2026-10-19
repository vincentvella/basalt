// Which window a bounds read is about.
//
// The interesting half of per-window geometry is in three hosts and needs real
// windows, which is `scripts/integration_test.py -k "two windows"`. What is
// portable is the bookkeeping underneath: an id that means "the app's own",
// resolved to whatever the host calls that window, and a cache that answers
// per window rather than handing one window's numbers to another.
//
// Worth asserting rather than assuming, because both ways it can be wrong are
// quiet. A read that resolved nothing would hand an app zeroes forever and
// look like a window that had not opened yet; a cache keyed by nothing would
// hand a preview window the main window's size, which is a plausible number
// and the wrong one.

#include "TestHarness.h"

#include "WindowControl.h"

#include <sstream>

using facebook::react::SurfaceId;

namespace {

// `setAppWindowId` is process-wide, as the thing it describes is. Restored so
// that a test leaves the next one the state it found.
struct AppWindow {
  explicit AppWindow(SurfaceId id) { basalt::setAppWindowId(id); }
  ~AppWindow() { basalt::setAppWindowId(basalt::kAppWindow); }
};

TEST(window_bounds_a_window_nobody_reported_reads_zeroes) {
  // The same answer a read before the first window gets, and deliberately so:
  // an app whose window has not reported yet renders once against zeroes and
  // is corrected immediately after. Anything else here would be another
  // window's numbers.
  const basalt::WindowBounds bounds = basalt::lastKnownWindowBounds(987654);
  EXPECT_EQ(bounds.width, 0.0);
  EXPECT_EQ(bounds.height, 0.0);
  EXPECT_EQ(bounds.x, 0.0);
  EXPECT_EQ(bounds.y, 0.0);
  EXPECT(!bounds.fullScreen);
  EXPECT(!bounds.maximized);
}

TEST(window_zero_is_whatever_the_host_calls_its_own_window) {
  const AppWindow installed(1);
  EXPECT_EQ((long)basalt::resolveWindow(basalt::kAppWindow), 1L);
}

TEST(window_an_id_is_passed_through_whatever_the_app_window_is) {
  const AppWindow installed(1);
  EXPECT_EQ((long)basalt::resolveWindow(7), 7L);
  // Including the app's own window named by its real id, which is what an
  // event about it carries.
  EXPECT_EQ((long)basalt::resolveWindow(1), 1L);
}

TEST(window_zero_before_a_host_has_said_is_still_zero) {
  // Every test binary and both mount harnesses are in this state: core is
  // linked, no window was ever made, and nothing called `setAppWindowId`.
  // Zero then reaches each host's own "the app's window" fallback, which is
  // the rule every caller used before windows had ids.
  EXPECT_EQ((long)basalt::resolveWindow(basalt::kAppWindow), (long)basalt::kAppWindow);
}

TEST(window_the_app_window_and_its_id_read_the_same_entry) {
  // The two spellings have to land on one cache entry. If they did not, a
  // `useWindow()` outside any <Window> -- which holds zero -- would read a
  // different record from the one the host writes when the main window moves,
  // and would never update.
  const AppWindow installed(1);
  const basalt::WindowBounds byZero = basalt::lastKnownWindowBounds(basalt::kAppWindow);
  const basalt::WindowBounds byId = basalt::lastKnownWindowBounds(1);
  EXPECT_EQ(byZero.width, byId.width);
  EXPECT_EQ(byZero.height, byId.height);
  EXPECT_EQ(byZero.x, byId.x);
  EXPECT_EQ(byZero.y, byId.y);
}

} // namespace
