// Tests for macOS's half of the platform-services seam.
//
// The clipboard and URL halves only. The alert is tested from JavaScript
// instead; see the note further down for why it cannot be tested here.

#include "TestHarness.h"

#include "MenuModel.h"
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
// e2e/alert.js instead, which keeps a 300ms heartbeat running and shows an alert
// half a second in -- the ticks carry straight on past it, which a blocking
// implementation could not do.

// A menu role reaching the field that has focus, in an app that is not the
// active one. See AppKitAppWindow.h.
//
// `performMenuRole` used to be one line, `[NSApp sendAction:selector to:nil
// from:nil]`, which walks the responder chain from the **key window's** first
// responder. A background app has no key window, so a `paste` role reached
// nothing at all -- and this suite is a background app, which is what makes the
// test possible without a harness around it.
//
// Uses the real clipboard and puts back what was there, the same courtesy
// `clipboard_round_trips` above extends.
TEST(a_menu_role_reaches_the_focused_field_without_the_app_being_active) {
  @autoreleasepool {
    const std::string original = basalt::clipboardText();

    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 320, 80)
                  styleMask:NSWindowStyleMaskTitled
                    backing:NSBackingStoreBuffered
                      defer:NO];
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(10, 10, 300, 24)];
    [window.contentView addSubview:field];
    // Not released when closed, which is the same line main_appkit.mm needs for
    // the windows the host makes and for the same reason: `close` on a window
    // whose `releasedWhenClosed` is YES -- the default for one built in code --
    // releases it while ARC still holds a reference, and the second release
    // comes when the pool drains.
    //
    // That is undefined behaviour, so it did what undefined behaviour does: it
    // passed here and segfaulted on CI's Mac, inside `objc_autoreleasePoolPop`
    // with no test name, until the harness started flushing.
    window.releasedWhenClosed = NO;
    [window orderFront:nil];
    // First responder without the app being active, which AppKit allows: what
    // it does not do is make the window *key*.
    EXPECT([window makeFirstResponder:field]);
    EXPECT(NSApp.keyWindow == nil || NSApp.keyWindow == window);

    basalt::setClipboardText("pasted by a role");
    basalt::performMenuRole("paste");

    EXPECT_EQ(std::string(field.stringValue.UTF8String), std::string("pasted by a role"));

    [window orderOut:nil];
    [window close];
    basalt::setClipboardText(original);
  }
}
