// expo-notifications, as a package rather than as part of core.
//
// Moved out of `core/ExpoModules.cpp` because a notification is permission-
// gated, and a permission prompt is the operating system saying this is not
// ordinary application behaviour. See
// `openspec/changes/split-optional-capabilities-into-packages`.
//
// Nothing here changed in the move except where it lives and how it is
// installed: core calls `installPackageExpoModules`, which the build generates,
// and that calls the function at the bottom of this file. Core does not name
// this package and does not know it exists.

#include "Notifications.h"

#include "ExpoModules.h"
#include "JsiPromise.h"
#include "PlatformServices.h"

#include <jsi/jsi.h>

#include <string>
#include <vector>

namespace basalt {

#ifdef BASALT_HAS_EXPO

namespace {

using facebook::jsi::Array;
using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::PropNameID;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;


// expo-notifications, which is thirteen modules and three of them real.
//
// React Native has no notification API to port -- PushNotificationIOS is a
// separate package and Android's is a library -- so the contract worth
// implementing is the one an app is most likely already using. See
// core/Notifications.h for what each desktop can actually do, and why macOS and
// Windows report that they cannot rather than substituting something else.
//
// All of them are registered even though most are empty, and that is not
// padding: `requireNativeModule` throws when a name is missing, and
// expo-notifications requires every one of them at import. The list grows with
// the package -- 0.28 asked for twelve and 55 adds `ExpoTopicSubscriptionModule`
// -- so a version that wants one more will say so by failing on `import`, which
// is a clearer failure than a method quietly missing. An empty module
// imports cleanly and then reports "not available on this platform" naming the
// method an app called, which is the behaviour ExpoModules.h's header argues
// for. A missing module would instead take the app down on `import`.

// The content an app passed to `presentNotificationAsync` or
// `scheduleNotificationAsync`.
NotificationContent contentFrom(Runtime &runtime, const Object &content) {
  NotificationContent parsed;
  const auto read = [&](const char *key) -> std::string {
    const Value value = content.getProperty(runtime, key);
    return value.isString() ? value.asString(runtime).utf8(runtime) : std::string{};
  };
  parsed.title = read("title");
  parsed.subtitle = read("subtitle");
  parsed.body = read("body");
  return parsed;
}

// expo's PermissionResponse, which is what both permission methods answer with.
// A desktop asks nobody: either the platform can show a notification or it
// cannot, and there is no prompt in between -- so `canAskAgain` is false and
// `status` is the answer already.
Value permissionResponse(Runtime &runtime) {
  const NotificationSupport support = notificationSupport();

  Object response(runtime);
  response.setProperty(
      runtime, "status", String::createFromUtf8(runtime, support.available ? "granted" : "denied"));
  response.setProperty(runtime, "granted", Value(support.available));
  response.setProperty(runtime, "canAskAgain", Value(false));
  response.setProperty(runtime, "expires", String::createFromUtf8(runtime, "never"));
  // Not part of expo's type, and the only place a reason can be put where a
  // developer will see it. An app reading `status` is unaffected.
  if (!support.available) {
    response.setProperty(runtime, "reason", String::createFromUtf8(runtime, support.reason));
  }

  Object ios(runtime);
  ios.setProperty(runtime, "status", Value(support.available ? 2 : 1));
  ios.setProperty(runtime, "allowsAlert", Value(support.available));
  ios.setProperty(runtime, "allowsBadge", Value(false));
  ios.setProperty(runtime, "allowsSound", Value(false));
  response.setProperty(runtime, "ios", std::move(ios));

  return Value(runtime, response);
}

Object makeNotificationPresenterModule(Runtime &runtime) {
  Object module = expoModule(runtime);

  addExpoFunction(runtime,
              module,
              "presentNotificationAsync",
              2,
              [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
                if (count < 2 || !args[0].isString() || !args[1].isObject()) {
                  return rejected(rt, "presentNotificationAsync expects an identifier and content");
                }
                const std::string identifier = args[0].asString(rt).utf8(rt);
                const NotificationContent content = contentFrom(rt, args[1].asObject(rt));
                if (!showNotification(identifier, content)) {
                  return rejected(rt, "could not show a notification: " + notificationSupport().reason);
                }
                // expo answers with the identifier it was given.
                return resolved(rt, String::createFromUtf8(rt, identifier));
              });

  addExpoFunction(runtime,
              module,
              "dismissNotificationAsync",
              1,
              [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
                if (count < 1 || !args[0].isString()) {
                  return rejected(rt, "dismissNotificationAsync expects an identifier");
                }
                dismissNotification(args[0].asString(rt).utf8(rt));
                // Resolves either way: dismissing something already gone is not
                // an error anywhere else either.
                return resolved(rt);
              });

  addExpoFunction(runtime,
              module,
              "dismissAllNotificationsAsync",
              0,
              [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                dismissAllNotifications();
                return resolved(rt);
              });

  addExpoFunction(runtime,
              module,
              "getPresentedNotificationsAsync",
              0,
              [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                const std::vector<std::string> identifiers = presentedNotifications();
                Array list(rt, identifiers.size());
                for (size_t i = 0; i < identifiers.size(); i++) {
                  // The shape expo's `mapNotification` reads: a request with an
                  // identifier, and the date it was shown.
                  Object request(rt);
                  request.setProperty(
                      rt, "identifier", String::createFromUtf8(rt, identifiers[i]));
                  request.setProperty(rt, "content", Object(rt));
                  request.setProperty(rt, "trigger", Value::null());

                  Object notification(rt);
                  notification.setProperty(rt, "request", std::move(request));
                  notification.setProperty(rt, "date", Value(0));
                  list.setValueAtIndex(rt, i, std::move(notification));
                }
                return resolved(rt, std::move(list));
              });

  return module;
}

Object makeNotificationSchedulerModule(Runtime &runtime) {
  Object module = expoModule(runtime);

  // The modern way to show one: `scheduleNotificationAsync` with a null
  // trigger, which expo documents as "deliver immediately" and is what
  // `presentNotificationAsync` was deprecated in favour of.
  //
  // A trigger that is not null is rejected by name rather than silently never
  // firing. Scheduling needs a timer that outlives the process and a store to
  // keep the queue in, which is a feature rather than a branch.
  addExpoFunction(runtime,
              module,
              "scheduleNotificationAsync",
              3,
              [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
                if (count < 2 || !args[0].isString() || !args[1].isObject()) {
                  return rejected(rt, "scheduleNotificationAsync expects an identifier and content");
                }
                if (count >= 3 && !args[2].isNull() && !args[2].isUndefined()) {
                  return rejected(rt,
                                  "scheduling a notification for later is not implemented on this "
                                  "platform; a null trigger delivers immediately");
                }
                const std::string identifier = args[0].asString(rt).utf8(rt);
                const NotificationContent content = contentFrom(rt, args[1].asObject(rt));
                if (!showNotification(identifier, content)) {
                  return rejected(rt, "could not show a notification: " + notificationSupport().reason);
                }
                return resolved(rt, String::createFromUtf8(rt, identifier));
              });

  return module;
}

Object makeNotificationPermissionsModule(Runtime &runtime) {
  Object module = expoModule(runtime);

  for (const char *name : {"getPermissionsAsync", "requestPermissionsAsync"}) {
    addExpoFunction(runtime,
                module,
                name,
                1,
                [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                  return resolved(rt, permissionResponse(rt));
                });
  }

  return module;
}
} // namespace

// Called by the translation unit CMake generates when this package is
// discovered; see core/PackageModules.h.
void installNotificationExpoModules(Runtime &runtime, Object &modules) {
  // expo-notifications. The three with methods on them, and then the nine that
  // exist only so that importing the package does not throw; see the comment
  // above makeNotificationPresenterModule.
  modules.setProperty(runtime, "ExpoNotificationPresenter", makeNotificationPresenterModule(runtime));
  modules.setProperty(runtime, "ExpoNotificationScheduler", makeNotificationSchedulerModule(runtime));
  modules.setProperty(
      runtime, "ExpoNotificationPermissionsModule", makeNotificationPermissionsModule(runtime));
  for (const char *name : {"ExpoNotificationsEmitter",
                           "ExpoNotificationsHandlerModule",
                           "ExpoNotificationCategoriesModule",
                           "ExpoNotificationChannelManager",
                           "ExpoNotificationChannelGroupManager",
                           "ExpoBackgroundNotificationTasksModule",
                           "ExpoBadgeModule",
                           "ExpoPushTokenManager",
                           "ExpoTopicSubscriptionModule",
                           "NotificationsServerRegistrationModule"}) {
    modules.setProperty(runtime, name, expoModule(runtime));
  }
}

#else

void installNotificationExpoModules(facebook::jsi::Runtime & /*runtime*/,
                                    facebook::jsi::Object & /*modules*/) {}

#endif

} // namespace basalt
