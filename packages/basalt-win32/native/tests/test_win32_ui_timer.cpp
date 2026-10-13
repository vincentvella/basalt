// Timers that arrive, including under the one condition that broke the ones
// before them. See win32/Win32UiTimer.h.

#include "TestHarness.h"

#include "Win32UiThread.h"
#include "Win32UiTimer.h"

#include <windows.h>

#include <chrono>
#include <functional>
#include <thread>

namespace {

// A real message loop for the duration of one test. Without one there is
// nothing to post to, `scheduleUiTimer` declines, and every assertion below
// would be about the declining rather than about the timer.
struct ScopedUiThread {
  ScopedUiThread() { basalt::installUiThread(); }
  ~ScopedUiThread() { basalt::uninstallUiThread(); }
  ScopedUiThread(const ScopedUiThread &) = delete;
  ScopedUiThread &operator=(const ScopedUiThread &) = delete;
};

// Pumps until `ready`, or until the deadline. Returns whether it came true.
//
// The inner drain is bounded rather than "until the queue is empty", which
// the real loop can afford and this cannot: one test here feeds the queue a
// message that reposts itself, and an unbounded drain never comes back out.
bool pumpUntil(const std::function<bool()> &ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    MSG message;
    for (int drained = 0; drained < 64; drained++) {
      if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (ready()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

} // namespace

TEST(ui_timer_fires_once_after_its_delay) {
  ScopedUiThread loop;
  int fired = 0;
  const basalt::UiTimerId timer = basalt::scheduleUiTimer(10, false, [&fired] { fired++; });
  EXPECT(timer != 0);
  EXPECT(pumpUntil([&fired] { return fired > 0; }));
  // And exactly once: a one-shot that repeated would be found by nothing else
  // here, because every other assertion is satisfied by the first call.
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(120);
  pumpUntil([&until] { return std::chrono::steady_clock::now() > until; });
  EXPECT_EQ(fired, 1);
  basalt::cancelUiTimer(timer);
}

TEST(ui_timer_repeats_until_it_is_cancelled) {
  ScopedUiThread loop;
  int fired = 0;
  const basalt::UiTimerId timer = basalt::scheduleUiTimer(5, true, [&fired] { fired++; });
  EXPECT(timer != 0);
  EXPECT(pumpUntil([&fired] { return fired >= 3; }));
  basalt::cancelUiTimer(timer);
  const int afterCancel = fired;
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
  pumpUntil([&until] { return std::chrono::steady_clock::now() > until; });
  EXPECT_EQ(fired, afterCancel);
}

TEST(ui_timer_can_be_cancelled_from_inside_its_own_callback) {
  // How a repeating animation ends: the last frame stops the thing that
  // delivered it. `cancelUiTimer` waits out a pool-thread delivery, so doing
  // this on the wrong thread would deadlock rather than misbehave.
  ScopedUiThread loop;
  int fired = 0;
  basalt::UiTimerId timer = 0;
  timer = basalt::scheduleUiTimer(5, true, [&fired, &timer] {
    fired++;
    if (fired == 2) {
      basalt::cancelUiTimer(timer);
    }
  });
  EXPECT(pumpUntil([&fired] { return fired >= 2; }));
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
  pumpUntil([&until] { return std::chrono::steady_clock::now() > until; });
  EXPECT_EQ(fired, 2);
}

TEST(ui_timer_arrives_on_a_queue_that_never_empties) {
  // The whole reason this file exists.
  //
  // `WM_TIMER` is not posted. It is *synthesised* by `PeekMessage`, and only
  // once the queue holds nothing else -- below posted messages, below input,
  // below `WM_PAINT`. A loop that always has something to do therefore never
  // gives a `WM_TIMER` the gap it needs, and every `SetTimer` in the host
  // simply stopped. That is the ordinary condition of a real app: measured on
  // kino, whose preview is a Skia `<Canvas>`, 120 `WM_PAINT` and 250 posted
  // work items a second while idle, against six `WM_TIMER` in twenty-five
  // seconds.
  //
  // Reproduced here with a message that reposts itself, which is the same
  // queue condition as the painting without needing a window on screen.
  ScopedUiThread loop;

  constexpr UINT kFlood = WM_APP + 7;
  static long flooded = 0;
  static long ticked = 0;
  // Cleared before the window goes, so the self-reposting message stops
  // rather than outliving this test in the queue the next one pumps.
  static bool flooding = true;
  flooded = 0;
  ticked = 0;
  flooding = true;

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM w, LPARAM l) -> LRESULT {
    if (message == kFlood) {
      flooded++;
      // Posted again before this one is finished with, so the queue is never
      // observed empty. Which is the condition under test.
      if (flooding) {
        PostMessageW(hwnd, kFlood, 0, 0);
      }
      return 0;
    }
    if (message == WM_TIMER) {
      ticked++;
      return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
  };
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"BasaltUiTimerBusyTest";
  RegisterClassExW(&windowClass);
  HWND window = CreateWindowExW(0,
                                L"BasaltUiTimerBusyTest",
                                L"",
                                0,
                                0,
                                0,
                                0,
                                0,
                                HWND_MESSAGE,
                                nullptr,
                                GetModuleHandleW(nullptr),
                                nullptr);
  EXPECT(window != nullptr);
  PostMessageW(window, kFlood, 0, 0);

  // The platform's own timer, for the comparison. Ten milliseconds, so a
  // working one would fire many times over what follows.
  SetTimer(window, 1, 10, nullptr);

  int fired = 0;
  const basalt::UiTimerId timer = basalt::scheduleUiTimer(10, false, [&fired] { fired++; });
  EXPECT(pumpUntil([&fired] { return fired > 0; }));

  // The flood really did run, so the pass above means the timer arrived
  // through a busy queue rather than through a quiet one.
  EXPECT(flooded > 100);
  // ...and the comparison. If this ever starts firing, Windows has changed
  // its message priorities and this whole file is worth re-reading rather
  // than the assertion being worth relaxing.
  EXPECT_EQ(static_cast<int>(ticked), 0);

  flooding = false;
  KillTimer(window, 1);
  basalt::cancelUiTimer(timer);
  DestroyWindow(window);
  UnregisterClassW(L"BasaltUiTimerBusyTest", GetModuleHandleW(nullptr));
}

TEST(ui_timer_declines_when_there_is_no_loop_to_post_to) {
  // No ScopedUiThread. Delivering here would mean running the callback on a
  // thread pool, which is how the rest of this suite -- written against
  // inline posting on one thread -- would start failing at random.
  int fired = 0;
  EXPECT_EQ(static_cast<int>(basalt::scheduleUiTimer(1, false, [&fired] { fired++; })), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  EXPECT_EQ(fired, 0);
  // And cancelling nothing is allowed, which is what lets a caller keep one
  // id in a field and cancel it without asking whether it ever started.
  basalt::cancelUiTimer(0);
}
