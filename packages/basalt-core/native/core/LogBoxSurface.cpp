#include "LogBoxSurface.h"

#include "PlatformServices.h"

namespace basalt {

LogBoxSurfaceDelegate::LogBoxSurfaceDelegate(Show show, Hide hide)
    : show_(std::move(show)), hide_(std::move(hide)) {}

void LogBoxSurfaceDelegate::createContentView(std::string appKey) {
  // Only recorded. The module constructs on the JavaScript thread and calls
  // this at once, long before anything wants a surface on screen, and starting
  // one here would mean touching widgets from the wrong thread for no gain.
  appKey_ = std::move(appKey);
}

bool LogBoxSurfaceDelegate::isContentViewReady() {
  return !appKey_.empty();
}

void LogBoxSurfaceDelegate::destroyContentView() {
  hide();
  appKey_.clear();
}

void LogBoxSurfaceDelegate::show() {
  if (showing_ || appKey_.empty() || !show_) {
    return;
  }
  showing_ = true;
  postToUiThread([show = show_, appKey = appKey_]() { show(appKey); });
}

void LogBoxSurfaceDelegate::hide() {
  if (!showing_ || !hide_) {
    return;
  }
  showing_ = false;
  postToUiThread([hide = hide_]() { hide(); });
}

bool LogBoxSurfaceDelegate::isShowing() {
  return showing_;
}

} // namespace basalt
