// The message loop, and the one thing it has to get right that is not
// obvious. See win32/Win32RunLoopObserver.h.

#include "TestHarness.h"

#include "Win32RunLoopObserver.h"

#include <windows.h>

#include <chrono>

namespace {

constexpr UINT kFlood = WM_APP + 11;

// A message-only window whose only message reposts itself, so the queue is
// never observed empty. `flooding` ends it, since a message still in the
// queue when the loop stops would be pumped by whatever runs next.
bool gFlooding = true;
long gFlooded = 0;

LRESULT CALLBACK floodProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  if (message == kFlood) {
    gFlooded++;
    // A ceiling, so that a loop which never beats fails rather than hangs:
    // the beat is what stops the flood and posts WM_QUIT, and a regression
    // here is precisely a beat that never happens.
    if (gFlooded > 50000) {
      gFlooding = false;
    }
    if (gFlooding) {
      PostMessageW(hwnd, kFlood, 0, 0);
    }
    return 0;
  }
  return DefWindowProcW(hwnd, message, w, l);
}

HWND makeFloodWindow() {
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = floodProc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"BasaltRunLoopFloodTest";
  RegisterClassExW(&windowClass);
  return CreateWindowExW(0,
                         L"BasaltRunLoopFloodTest",
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
}

void dropFloodWindow(HWND window) {
  gFlooding = false;
  DestroyWindow(window);
  UnregisterClassW(L"BasaltRunLoopFloodTest", GetModuleHandleW(nullptr));
  // Whatever is left of the flood, so the next test's pump does not inherit
  // it.
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
  }
}

} // namespace

TEST(run_loop_beats_once_when_the_queue_empties) {
  int beats = 0;
  PostMessageW(nullptr, WM_NULL, 0, 0);
  const int code = basalt::runMessageLoop([&beats] {
    beats++;
    PostQuitMessage(7);
  });
  EXPECT_EQ(code, 7);
  EXPECT_EQ(beats, 1);
}

TEST(run_loop_beats_on_a_queue_that_never_empties) {
  // The regression this file exists for.
  //
  // The loop drains before it beats, which is what "BeforeWaiting" means and
  // what React Native's event queue is flushed by. An app that paints
  // continuously -- kino, whose preview is a Skia `<Canvas>`: 120 WM_PAINT
  // and 250 posted work items a second while idle -- never lets the drain
  // finish, so the beat never ran and events sat in the queue. What that
  // looked like from outside was a button that usually worked: one scripted
  // tap in three did nothing, and a real click rather more often than that.
  //
  // Without the bound in the loop this test does not fail slowly. It never
  // beats at all, so it never posts WM_QUIT, and the deadline below is the
  // only thing that ends it.
  gFlooding = true;
  gFlooded = 0;
  HWND window = makeFloodWindow();
  EXPECT(window != nullptr);
  PostMessageW(window, kFlood, 0, 0);

  int beats = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  basalt::runMessageLoop([&] {
    beats++;
    // Several, so this is about a loop that keeps beating rather than one
    // that beat once on the way in.
    if (beats >= 4 || std::chrono::steady_clock::now() > deadline) {
      gFlooding = false;
      PostQuitMessage(0);
    }
  });

  EXPECT(beats >= 4);
  // ...and the flood really was running, so the beats above happened in spite
  // of it rather than after it ran out.
  EXPECT(gFlooded > 100);

  dropFloodWindow(window);
}
