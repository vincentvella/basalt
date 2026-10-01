// The title bar's metrics, which are the only part of it a test can reach.
//
// What the two styles *look* like is in the screenshots that went with phase
// 58; what is assertable is the arithmetic an app lays itself out against, and
// that arithmetic is the whole contract src/TitleBar.js has with the host.
//
// A real NSWindow, never ordered front. Its metrics are a property of the
// window rather than of anything on screen, so nothing here has to be shown.

#include "TestHarness.h"

#include <sstream>

#import "AppKitTitleBar.h"

TEST(titlebar_native_style_costs_the_app_nothing) {
  @autoreleasepool {
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 400, 300)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    basalt::titleBar().attach(window);
    basalt::titleBar().setStyle(basalt::TitleBarStyle::Native);

    // The system's title bar sits outside the content view, so it takes
    // nothing from the app and there is nothing to leave clear. Windows
    // reports the same zeroes for the same reason.
    const auto metrics = basalt::titleBar().metrics();
    EXPECT(metrics.style == basalt::TitleBarStyle::Native);
    EXPECT_EQ(metrics.height, 0.0);
    EXPECT_EQ(metrics.buttonsWidth, 0.0);
  }
}

TEST(titlebar_hidden_style_reports_what_to_leave_clear) {
  @autoreleasepool {
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 400, 300)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    basalt::titleBar().attach(window);
    basalt::titleBar().setStyle(basalt::TitleBarStyle::Hidden);

    const auto metrics = basalt::titleBar().metrics();
    EXPECT(metrics.style == basalt::TitleBarStyle::Hidden);
    // A real height rather than a guess: the content view now extends under
    // the title bar, and this is how much of it the app must not fill.
    EXPECT(metrics.height > 0.0);
    // And the traffic lights, which the system keeps drawing over the app's
    // own header -- the counterpart of the caption buttons on Windows.
    EXPECT(metrics.buttonsWidth > 0.0);

    // The window really did take the style, rather than the metrics being
    // computed from a flag nothing acted on.
    EXPECT((window.styleMask & NSWindowStyleMaskFullSizeContentView) != 0);
    EXPECT(window.titlebarAppearsTransparent);

    // And back, so a request that is unmounted restores the window.
    basalt::titleBar().setStyle(basalt::TitleBarStyle::Native);
    EXPECT((window.styleMask & NSWindowStyleMaskFullSizeContentView) == 0);
    EXPECT_EQ(basalt::titleBar().metrics().height, 0.0);
  }
}

TEST(titlebar_takes_a_title_and_a_colour) {
  @autoreleasepool {
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 400, 300)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    basalt::titleBar().attach(window);
    basalt::titleBar().setStyle(basalt::TitleBarStyle::Native);
    basalt::titleBar().setTitle(std::string("Inbox"));
    EXPECT([window.title isEqualToString:@"Inbox"]);

    // A dark caption asks for the dark appearance, because AppKit has no
    // caption text colour of its own -- the appearance is what decides it.
    basalt::titleBar().setColors(0xFF1A1E28u, std::nullopt, std::nullopt);
    EXPECT(window.titlebarAppearsTransparent);
    EXPECT([window.appearance.name isEqualToString:NSAppearanceNameDarkAqua]);

    // And a light one asks for the light appearance.
    basalt::titleBar().setColors(0xFFF5F5F5u, std::nullopt, std::nullopt);
    EXPECT([window.appearance.name isEqualToString:NSAppearanceNameAqua]);
  }
}

// The caption buttons' functions. What they do was checked live -- a probe
// drove the window from 900x700 to maximised and back, then minimised and
// closed it. What a test pins is the half with no window on the other end: the
// module outlives the window at shutdown, and a call arriving then must not
// take the process with it.
TEST(titlebar_actions_are_safe_with_no_window) {
  @autoreleasepool {
    basalt::titleBar().attach(nil);
    basalt::titleBar().minimize();
    basalt::titleBar().toggleMaximize();
    basalt::titleBar().close();
    basalt::titleBar().startDrag();
    EXPECT(true);
  }
}
