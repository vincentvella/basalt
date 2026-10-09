// The clipboard half of core/PlatformServices.h on GTK, and which thread it runs
// on.
//
// The thread is the whole point of this file. `DesktopClipboardModule` calls
// these from the JavaScript thread, and GDK may only be used on the main one:
// `gdk_clipboard_set_text` claims a selection, claiming one needs a server
// timestamp, and asking for a timestamp blocks in `XIfEvent`. Doing that from a
// second thread deadlocked a CI run on 2026-10-08, with the main thread stopped
// in GTK's frame clock waiting for the same display. So the write is handed to
// the main thread, and these assert that it is.
//
// The real clipboard, because that is what the seam talks to, and whatever was
// there is put back at the end -- as text, which is the same thing e2e/modules.js
// running costs.

#include "TestHarness.h"

#include "PlatformServices.h"

#include <gtk/gtk.h>

#include <sstream>
#include <string>
#include <thread>

namespace {

// GDK's own answer, which is what the main thread sees. Deliberately not
// basalt::clipboardText(), which also answers from what was last written.
std::string gdkClipboardText() {
  GdkDisplay *display = gdk_display_get_default();
  if (display == nullptr) {
    return {};
  }
  GdkContentProvider *provider = gdk_clipboard_get_content(gdk_display_get_clipboard(display));
  if (provider == nullptr) {
    return {};
  }
  GValue value = G_VALUE_INIT;
  g_value_init(&value, G_TYPE_STRING);
  std::string text;
  if (gdk_content_provider_get_value(provider, &value, nullptr)) {
    const char *string = g_value_get_string(&value);
    if (string != nullptr) {
      text = string;
    }
  }
  g_value_unset(&value);
  return text;
}

// Runs whatever the main loop has pending, which is where a queued write lands.
void pump() {
  for (int i = 0; i < 100 && g_main_context_pending(nullptr); i++) {
    g_main_context_iteration(nullptr, FALSE);
  }
}

} // namespace

TEST(clipboard_round_trips) {
  const std::string original = basalt::clipboardText();

  basalt::setClipboardText("basalt round trip");
  pump();
  EXPECT_EQ(basalt::clipboardText(), std::string("basalt round trip"));

  // Non-ASCII, in and out: a byte-wise copy of a UTF-8 string is where a
  // clipboard loses characters.
  basalt::setClipboardText("héllo · 世界");
  pump();
  EXPECT_EQ(basalt::clipboardText(), std::string("héllo · 世界"));

  basalt::setClipboardText(original);
  pump();
}

// The write happens on the main thread, which is what stops the deadlock. Asked
// as a question about *when* GDK changes: a synchronous write would have reached
// GDK before the loop ran, from the wrong thread.
TEST(clipboard_a_write_from_another_thread_waits_for_the_main_thread) {
  const std::string original = basalt::clipboardText();

  basalt::setClipboardText("before");
  pump();
  EXPECT_EQ(gdkClipboardText(), std::string("before"));

  std::thread([] { basalt::setClipboardText("from another thread"); }).join();

  // Nothing has run the loop, so GDK still holds what it held: the write is
  // queued rather than done, which is the whole fix.
  EXPECT_EQ(gdkClipboardText(), std::string("before"));
  // And a read sees it anyway, which is what `getString()` straight after a
  // `setString()` needs.
  EXPECT_EQ(basalt::clipboardText(), std::string("from another thread"));

  pump();
  EXPECT_EQ(gdkClipboardText(), std::string("from another thread"));
  EXPECT_EQ(basalt::clipboardText(), std::string("from another thread"));

  basalt::setClipboardText(original);
  pump();
}

// A write from the main thread is not deferred, because the share picker in
// core/ShareFallback.h writes from there and then opens a mail client: a
// clipboard that filled in a loop iteration's time would be a race.
TEST(clipboard_a_write_on_the_main_thread_happens_at_once) {
  const std::string original = basalt::clipboardText();

  basalt::setClipboardText("on the main thread");
  // No pump.
  EXPECT_EQ(gdkClipboardText(), std::string("on the main thread"));

  basalt::setClipboardText(original);
  pump();
}
