// Desktop notifications on macOS.
//
// `UNUserNotificationCenter` is the only supported API -- `NSUserNotification`
// has been deprecated since 10.14 -- and it has one hard requirement that
// shapes this whole file: a bundle. For a process with no bundle identifier
// `+currentNotificationCenter` does not return nil and does not fail. It raises
// `NSInternalInconsistencyException: bundleProxyForCurrentProcess is nil` and
// terminates the process. Verified, not assumed.
//
// So every entry point below asks `available()` first, and `available()` asks
// the bundle identifier. That is not defensive style: it is the difference
// between an app that reports "notifications are unavailable" and one that
// dies inside Apple's code. A `@try` would not do, because finding out means
// entering that code.
//
// `react-native run-macos` now builds a real `.app` around the host -- see
// cli/packageApp.js -- so the usual answer is that there *is* a bundle. A host
// run straight out of a build directory still has none, which is the case the
// check keeps working.
//
// ## Authorisation
//
// macOS asks the person, once, and remembers. The request is made on the first
// call rather than at startup: an app that never notifies should never produce
// a permission prompt, which is also what expo-notifications' own contract
// says. Until it is answered, `showNotification` returns true and the
// notification is delivered if permission is granted -- the request and the
// delivery are one call to `addNotificationRequest`, and Apple's own sample
// does the same.

#include "Notifications.h"

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace basalt {

namespace {

// Whether it is safe to touch UNUserNotificationCenter at all. See the header.
bool available() {
  return NSBundle.mainBundle.bundleIdentifier != nil;
}

// What has been shown and not dismissed, as far as this process knows.
//
// `getDeliveredNotificationsWithCompletionHandler:` is the authoritative answer
// and it is asynchronous, which `presentedNotifications()` is not -- it is
// called from a TurboModule that answers a JavaScript promise synchronously in
// this codebase's other two hosts. So this mirrors what was asked for, which is
// what the Linux host keeps too, and for the same reason.
std::mutex &presentedLock() {
  static std::mutex lock;
  return lock;
}

std::unordered_set<std::string> &presented() {
  static std::unordered_set<std::string> set;
  return set;
}

} // namespace

NotificationSupport notificationSupport() {
  if (!available()) {
    return {false,
            "this host is not a bundled application, and macOS only delivers "
            "notifications for one -- run it through `react-native run-macos`, "
            "which builds the bundle"};
  }
  return {true, {}};
}

bool showNotification(const std::string &identifier, const NotificationContent &content) {
  if (!available()) {
    return false;
  }

  @autoreleasepool {
    UNMutableNotificationContent *body = [[UNMutableNotificationContent alloc] init];
    NSString *title = [NSString stringWithUTF8String:content.title.c_str()];
    NSString *subtitle = [NSString stringWithUTF8String:content.subtitle.c_str()];
    NSString *text = [NSString stringWithUTF8String:content.body.c_str()];
    body.title = title != nil ? title : @"";
    body.subtitle = subtitle != nil ? subtitle : @"";
    body.body = text != nil ? text : @"";
    body.sound = UNNotificationSound.defaultSound;

    NSString *key = [NSString stringWithUTF8String:identifier.c_str()];
    if (key == nil) {
      return false;
    }
    // No trigger: `nil` means deliver immediately, which is what
    // `presentNotificationAsync` means. Scheduling is the scheduler module's
    // job and goes through this same call with a date trigger when it arrives.
    UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:key
                                                                         content:body
                                                                         trigger:nil];

    UNUserNotificationCenter *centre = UNUserNotificationCenter.currentNotificationCenter;
    // Asked here rather than at startup, so an app that never notifies never
    // prompts. Granting is remembered by the system, so this is a no-op after
    // the first time.
    [centre requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound
                          completionHandler:^(BOOL granted, NSError *error) {
                            if (!granted) {
                              NSLog(@"notifications: not authorised%@",
                                    error != nil ? [@": " stringByAppendingString:error.localizedDescription]
                                                 : @"");
                              return;
                            }
                            [centre addNotificationRequest:request
                                     withCompletionHandler:^(NSError *addError) {
                                       if (addError != nil) {
                                         NSLog(@"notifications: %@", addError.localizedDescription);
                                       }
                                     }];
                          }];

    const std::lock_guard<std::mutex> guard(presentedLock());
    presented().insert(identifier);
    return true;
  }
}

bool dismissNotification(const std::string &identifier) {
  if (!available()) {
    return false;
  }
  @autoreleasepool {
    NSString *key = [NSString stringWithUTF8String:identifier.c_str()];
    if (key == nil) {
      return false;
    }
    // Both lists: one that has been delivered is in the notification centre,
    // and one that has not is still pending. Dismissing should take it out of
    // either, which is what `dismissNotificationAsync` means.
    [UNUserNotificationCenter.currentNotificationCenter
        removeDeliveredNotificationsWithIdentifiers:@[key]];
    [UNUserNotificationCenter.currentNotificationCenter
        removePendingNotificationRequestsWithIdentifiers:@[key]];
  }
  const std::lock_guard<std::mutex> guard(presentedLock());
  return presented().erase(identifier) > 0;
}

void dismissAllNotifications() {
  if (!available()) {
    return;
  }
  @autoreleasepool {
    [UNUserNotificationCenter.currentNotificationCenter removeAllDeliveredNotifications];
    [UNUserNotificationCenter.currentNotificationCenter removeAllPendingNotificationRequests];
  }
  const std::lock_guard<std::mutex> guard(presentedLock());
  presented().clear();
}

std::vector<std::string> presentedNotifications() {
  const std::lock_guard<std::mutex> guard(presentedLock());
  return {presented().begin(), presented().end()};
}

} // namespace basalt
