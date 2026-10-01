// Tests for the clipboard half of core/PlatformServices.h on Windows.
//
// These use the real clipboard, because the thing being tested is how it
// behaves when somebody else holds it -- and the only somebody that can be
// arranged from a test is a second thread in this process, which OpenClipboard
// treats exactly like another program. What the clipboard held before is put
// back at the end, as text; anything richer than text is lost, the same as
// js/modules.js running.

#include "TestHarness.h"

#include "PlatformServices.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace {

// Opens the clipboard on another thread and keeps it for `holdMs`, which is
// what a clipboard listener does right after every write. Returns once the
// clipboard is actually held, so the caller's attempt is guaranteed to collide.
class ClipboardHolder {
 public:
  explicit ClipboardHolder(int holdMs) {
    thread_ = std::thread([this, holdMs] {
      // Opened against a window, because only that excludes anyone. A holder
      // that passes no window -- as the code under test does -- blocks nobody,
      // not even another process, and the first version of this test held the
      // clipboard that way and passed with the retry removed. A clipboard
      // listener has a window by definition; that is how it is told.
      const HWND window = CreateWindowExW(
          0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
      // Retried, and for longer than the code under test retries: the write
      // just before this is announced to every listener on the machine, and
      // one of them is usually still reading.
      for (int attempt = 0; attempt < 200 && !held_; ++attempt) {
        if (OpenClipboard(window)) {
          held_ = true;
        } else {
          Sleep(5);
        }
      }
      if (held_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
        CloseClipboard();
      }
      if (window != nullptr) {
        DestroyWindow(window);
      }
      finished_ = true;
    });
    // Until it is held, or the attempt to hold it has failed and held() says so.
    while (!held_ && !finished_) {
      std::this_thread::yield();
    }
  }
  ~ClipboardHolder() {
    thread_.join();
  }
  ClipboardHolder(const ClipboardHolder &) = delete;
  ClipboardHolder &operator=(const ClipboardHolder &) = delete;

  bool held() const {
    return held_;
  }

 private:
  std::thread thread_;
  std::atomic<bool> held_{false};
  std::atomic<bool> finished_{false};
};

class RestoreClipboard {
 public:
  RestoreClipboard() : saved_(basalt::clipboardText()) {}
  ~RestoreClipboard() {
    basalt::setClipboardText(saved_);
  }
  RestoreClipboard(const RestoreClipboard &) = delete;
  RestoreClipboard &operator=(const RestoreClipboard &) = delete;

 private:
  std::string saved_;
};

} // namespace

TEST(win32_clipboard_round_trips_unicode) {
  RestoreClipboard restore;
  const std::string written = "h\xC3\xA9llo \xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x9C\x82";
  basalt::setClipboardText(written);
  EXPECT_EQ(basalt::clipboardText(), written);
}

TEST(win32_clipboard_write_waits_out_a_brief_holder) {
  RestoreClipboard restore;
  basalt::setClipboardText("before");
  {
    // Well inside the retry budget, and far longer than one attempt.
    ClipboardHolder holder(20);
    EXPECT(holder.held());
    basalt::setClipboardText("after");
  }
  EXPECT_EQ(basalt::clipboardText(), std::string("after"));
}

TEST(win32_clipboard_read_waits_out_a_brief_holder) {
  RestoreClipboard restore;
  basalt::setClipboardText("still here");
  std::string read;
  {
    ClipboardHolder holder(20);
    EXPECT(holder.held());
    read = basalt::clipboardText();
  }
  EXPECT_EQ(read, std::string("still here"));
}
