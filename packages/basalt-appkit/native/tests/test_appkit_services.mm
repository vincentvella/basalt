// Tests for macOS's half of the platform-services seam.
//
// The clipboard and URL halves only. The alert is tested from JavaScript
// instead; see the note further down for why it cannot be tested here.

#include "TestHarness.h"

#include "PlatformServices.h"

#import <Cocoa/Cocoa.h>

#include <sstream>
#include <string>

TEST(clipboard_round_trips) {
  @autoreleasepool {
    const std::string original = basalt::clipboardText();

    basalt::setClipboardText("basalt round trip");
    EXPECT_EQ(basalt::clipboardText(), std::string("basalt round trip"));

    // Unicode, because NSPasteboard is UTF-16 inside and the conversion either
    // way is where a clipboard loses characters.
    basalt::setClipboardText("héllo · 世界");
    EXPECT_EQ(basalt::clipboardText(), std::string("héllo · 世界"));

    // Put back whatever was there: this is the user's clipboard, not the
    // suite's.
    basalt::setClipboardText(original);
  }
}

TEST(can_open_url_answers_by_scheme) {
  @autoreleasepool {
    // Something is always registered for http on a Mac.
    EXPECT(basalt::canOpenUrl("https://example.com"));
    EXPECT(!basalt::canOpenUrl("zzznotascheme:whatever"));
    EXPECT(!basalt::canOpenUrl(""));
    EXPECT(!basalt::canOpenUrl("not a url at all"));
  }
}

// There is no test here for the alert, deliberately.
//
// A sheet attaches to a key or main window, and a command-line binary's
// activation policy is `prohibited` -- its windows can never become either. So
// showAlert falls back to a modal run loop, which is right in the real case it
// exists for (an alert before the first window is on screen) and puts a real
// dialog on the developer's actual desktop when a test does it. It did, once,
// while this was being written.
//
// What the alert needs proving about it is that it does not block: it is called
// from the JavaScript thread, and a modal run loop there would freeze every
// mount, timer and animation frame until somebody clicked. That is checked by
// js/alert.js instead, which keeps a 300ms heartbeat running and shows an alert
// half a second in -- the ticks carry straight on past it, which a blocking
// implementation could not do.
