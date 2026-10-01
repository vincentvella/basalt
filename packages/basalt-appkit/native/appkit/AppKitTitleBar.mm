#import "AppKitTitleBar.h"

#include <cstdint>
#include <functional>

namespace basalt {

namespace {

NSColor *colorFrom(uint32_t argb) {
  // processColor's 0xAARRGGBB. sRGB explicitly, for the reason every other
  // colour on this host is: letting AppKit pick a device space shifts it on a
  // wide-gamut display, and the same app on Linux would not shift.
  return [NSColor colorWithSRGBRed:((argb >> 16) & 0xFF) / 255.0
                             green:((argb >> 8) & 0xFF) / 255.0
                              blue:(argb & 0xFF) / 255.0
                             alpha:((argb >> 24) & 0xFF) / 255.0];
}

// Whether a colour is dark enough to want light text over it. The same
// question DWMWA_USE_IMMERSIVE_DARK_MODE answers on Windows, asked here
// because AppKit has no caption text colour: the appearance decides it.
bool isDark(uint32_t argb) {
  const double r = ((argb >> 16) & 0xFF) / 255.0;
  const double g = ((argb >> 8) & 0xFF) / 255.0;
  const double b = (argb & 0xFF) / 255.0;
  // Rec. 601 luma, which is what every "is this dark" check uses and is close
  // enough for a binary decision.
  return (0.299 * r + 0.587 * g + 0.114 * b) < 0.5;
}

} // namespace

void AppKitTitleBar::attach(NSWindow *window) {
  window_ = window;
  apply();
}

void AppKitTitleBar::setTitle(std::optional<std::string> title) {
  title_ = std::move(title);
  apply();
}

void AppKitTitleBar::setColors(std::optional<uint32_t> background,
                               std::optional<uint32_t> text,
                               std::optional<uint32_t> border) {
  background_ = background;
  text_ = text;
  border_ = border;
  apply();
}

void AppKitTitleBar::setStyle(TitleBarStyle style) {
  if (style_ == style) {
    return;
  }
  style_ = style;
  apply();
  notify();
}

void AppKitTitleBar::minimize() {
  [window_ miniaturize:nil];
}

void AppKitTitleBar::toggleMaximize() {
  // `zoom:` rather than a maximised flag: on macOS the green button toggles
  // between the window's frame and the "best" one for the screen, and there is
  // no maximised state to set.
  [window_ zoom:nil];
}

void AppKitTitleBar::close() {
  // performClose: rather than close:, so the delegate gets its say and the
  // window closes the way the red button closes it.
  [window_ performClose:nil];
}

void AppKitTitleBar::startDrag() {
  NSWindow *window = window_;
  if (window == nil) {
    return;
  }
  // The event currently being handled, which is the press that started this.
  // AppKit takes over the drag from there and runs it until the mouse is
  // released.
  NSEvent *event = NSApp.currentEvent;
  if (event != nil) {
    [window performWindowDragWithEvent:event];
  }
}

TitleBarMetrics AppKitTitleBar::metrics() const {
  TitleBarMetrics result;
  result.style = style_;
  if (style_ == TitleBarStyle::Native) {
    // The system's title bar is outside the content view, so it costs the app
    // nothing and there is nothing to report.
    return result;
  }

  NSWindow *window = window_;
  if (window == nil) {
    return result;
  }

  // What the content view gained by extending under the title bar, which is
  // exactly what the app has to leave clear.
  //
  // Not `contentRectForFrameRect:`: with NSWindowStyleMaskFullSizeContentView
  // the content rect *is* the frame, so that difference is zero and says the
  // title bar costs nothing -- which is true of the window and false of the
  // app. `contentLayoutRect` is the part not covered by the title bar, which
  // is the question actually being asked.
  result.height = NSHeight(window.contentView.frame) - NSHeight(window.contentLayoutRect);

  // The traffic lights, from where they actually are rather than from a
  // constant: their size and spacing are the system's to change.
  NSButton *close = [window standardWindowButton:NSWindowCloseButton];
  NSButton *zoom = [window standardWindowButton:NSWindowZoomButton];
  if (close != nil && zoom != nil) {
    const NSRect closeFrame = close.frame;
    const NSRect zoomFrame = zoom.frame;
    // From the window's left edge to the far side of the last button, which is
    // the width an app has to keep clear -- the buttons sit at the left on
    // macOS, where Windows puts them at the right.
    result.buttonsWidth = NSMaxX(zoomFrame) + NSMinX(closeFrame);
  }
  return result;
}

void AppKitTitleBar::setMetricsListener(std::function<void(const TitleBarMetrics &)> listener) {
  listener_ = std::move(listener);
}

void AppKitTitleBar::apply() {
  NSWindow *window = window_;
  if (window == nil) {
    return;
  }

  if (title_.has_value()) {
    NSString *value = [NSString stringWithUTF8String:title_->c_str()];
    window.title = value != nil ? value : @"";
  }

  const bool hidden = style_ == TitleBarStyle::Hidden;
  window.titlebarAppearsTransparent = hidden || background_.has_value();
  // The title text would otherwise sit over the app's own content.
  window.titleVisibility = hidden ? NSWindowTitleHidden : NSWindowTitleVisible;

  if (hidden) {
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
  } else {
    window.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
  }

  if (background_.has_value()) {
    window.backgroundColor = colorFrom(*background_);
    // AppKit has no caption *text* colour: the window's appearance decides it.
    // So a dark caption gets the dark appearance, which is the same decision
    // Windows makes through DWMWA_USE_IMMERSIVE_DARK_MODE -- and honouring an
    // explicit textColor beyond that is not something AppKit offers.
    const bool wantsLight = text_.has_value() ? !isDark(*text_) : isDark(*background_);
    window.appearance = [NSAppearance appearanceNamed:wantsLight ? NSAppearanceNameDarkAqua
                                                                 : NSAppearanceNameAqua];
  }

  // `border` has no AppKit equivalent. A window's edge is the system's, and
  // there is no DWMWA_BORDER_COLOR here -- recorded rather than silently
  // dropped, so the asymmetry is visible where it is decided.
  (void)border_;
}

void AppKitTitleBar::notify() {
  if (listener_) {
    listener_(metrics());
  }
}

AppKitTitleBar &titleBar() {
  static AppKitTitleBar instance;
  return instance;
}

} // namespace basalt
