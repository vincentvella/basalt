// macOS's half of the colour-scheme seam.
//
// `NSApp.effectiveAppearance` is the authority: it accounts for the system
// setting, any per-application override in defaults, and the accessibility
// contrast variants, none of which reading `AppleInterfaceStyle` out of
// NSUserDefaults would see.

#include "ColorScheme.h"

#import <Cocoa/Cocoa.h>

// Key-value observing needs an object to deliver to, and the C++ half is not
// one. Observing the *application's* effectiveAppearance rather than the
// `AppleInterfaceThemeChangedNotification` distributed notification: that one
// fires for the system setting only, so an app told to be dark on a light
// system would never hear anything.
@interface RnAppKitAppearanceObserver : NSObject
@end

@implementation RnAppKitAppearanceObserver

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context {
  (void)keyPath;
  (void)object;
  (void)change;
  (void)context;
  basalt::notifyColorSchemeChanged();
}

@end

namespace basalt {

namespace {

// Kept for the life of the process: it *is* the observer registration, and
// letting it go would stop the notifications without saying so.
RnAppKitAppearanceObserver *gObserver = nil;

} // namespace

ColorScheme systemColorScheme() {
  @autoreleasepool {
    NSAppearance *appearance = NSApp != nil ? NSApp.effectiveAppearance : nil;
    if (appearance == nil) {
      // Before NSApplication exists -- a unit test, or a very early read. Light
      // is React Native's default when a platform has no answer.
      return ColorScheme::Light;
    }
    NSAppearanceName best = [appearance
        bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
    return [best isEqualToString:NSAppearanceNameDarkAqua] ? ColorScheme::Dark
                                                           : ColorScheme::Light;
  }
}

void startObservingColorScheme() {
  if (gObserver != nil || NSApp == nil) {
    return;
  }
  gObserver = [[RnAppKitAppearanceObserver alloc] init];
  [NSApp addObserver:gObserver
          forKeyPath:@"effectiveAppearance"
             options:NSKeyValueObservingOptionNew
             context:nullptr];
}

} // namespace basalt
