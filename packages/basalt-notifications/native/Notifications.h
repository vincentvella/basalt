// Desktop notifications, behind expo-notifications' contract.
//
// React Native has no notification API to port: `PushNotificationIOS` is a
// separate package and Android's is a library, so there is nothing in core to
// be compatible with. What an app on this platform is most likely to already
// be using is `expo-notifications`, so that is the contract implemented -- the
// same argument react-native-gesture-handler was ported on, and the same seam
// expo-clipboard uses. See core/ExpoModules.h.
//
// ## What each desktop can actually do
//
// **Linux** can, and without being installed. `org.freedesktop.Notifications`
// is a session-bus service that every desktop environment provides, it takes a
// summary and a body and hands back an id, and any process on the bus may call
// it. GNotification through GApplication would be the tidier API and needs a
// `.desktop` file matching the application id to display anything at all, which
// a host run out of a build directory does not have.
//
// **macOS** cannot, and the reason is worth writing down because it is not a
// missing implementation. `UNUserNotificationCenter` is the only supported API
// since `NSUserNotification` was deprecated, and for a process with no bundle
// identifier `+currentNotificationCenter` does not fail -- it throws
// `NSInternalInconsistencyException: bundleProxyForCurrentProcess is nil` and
// takes the process down. This host is a bare binary; `react-native run-macos`
// builds an executable, not an `.app`. So the API is not merely unavailable
// here, it is unsafe to touch, and the check is on the bundle identifier rather
// than on a call in a try block.
//
// **Windows** cannot either. A toast needs an AppUserModelID and a shortcut
// installed in the Start Menu, which is the same "be an installed application"
// requirement in a different shape.
//
// ## Why that is reported rather than worked around
//
// Because the API already has a way to say it: `getPermissionsAsync` answers
// `denied`, which is what expo-notifications' own documentation tells an app to
// check before it presents anything. A fallback -- a window, a log line, a tray
// balloon -- would be a different feature wearing this one's name. The share
// picker in core/ShareFallback.h had an honest substitute available and this
// does not, and that is the difference between the two.

#pragma once

#include <string>
#include <vector>

namespace basalt {

// What an app asked to show. expo-notifications' content has more in it --
// sound, badge, data, attachments -- and these three are what a desktop
// notification service takes.
struct NotificationContent {
  std::string title;
  std::string subtitle;
  std::string body;
};

// Whether this desktop can show one, and why not when it cannot. The reason is
// reported to JavaScript, so it has to say something a person can act on.
struct NotificationSupport {
  bool available{false};
  std::string reason;
};

NotificationSupport notificationSupport();

// Shows one, under an identifier the caller chose -- expo generates a uuid when
// an app does not. False if it could not be shown, which is not the same as
// unavailable: a service that is there and refuses is still a failure to
// report.
bool showNotification(const std::string &identifier, const NotificationContent &content);

// Takes one back down. False if that identifier is not showing.
bool dismissNotification(const std::string &identifier);

void dismissAllNotifications();

// The identifiers still on screen, as far as this process knows. A desktop
// notification service does not report what a user dismissed, so this is what
// was shown and not since taken down by us -- which is the same approximation
// every implementation of `getPresentedNotificationsAsync` makes.
std::vector<std::string> presentedNotifications();

} // namespace basalt
