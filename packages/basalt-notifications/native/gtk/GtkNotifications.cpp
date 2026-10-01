// Desktop notifications on Linux, over `org.freedesktop.Notifications`.
//
// The session-bus service every desktop environment provides, called directly
// with GDBus rather than through GNotification. The difference matters for a
// host run out of a build directory: `g_application_send_notification` routes
// through the portal, which looks the application up by its id and shows
// nothing at all when no matching `.desktop` file is installed. A direct
// `Notify` call has no such requirement -- any process on the bus may make one
// -- which is what an application that has not been installed yet needs.
//
// See core/Notifications.h for what the other two desktops can do and why.

#include "Notifications.h"

#include <gio/gio.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace basalt {

namespace {

constexpr const char *kBusName = "org.freedesktop.Notifications";
constexpr const char *kObjectPath = "/org/freedesktop/Notifications";
constexpr const char *kInterface = "org.freedesktop.Notifications";

// The bus, connected once and kept. Null when there is no session bus at all,
// which is what a headless run or a container without one looks like.
GDBusConnection *sessionBus() {
  static GDBusConnection *connection = []() -> GDBusConnection * {
    GError *error = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (bus == nullptr) {
      g_debug("no session bus for notifications: %s",
              error != nullptr ? error->message : "unknown error");
      g_clear_error(&error);
    }
    return bus;
  }();
  return connection;
}

// Whether anything is actually listening on the bus for that name. A session
// bus with no notification daemon on it is the normal state of a headless run,
// and calling Notify there fails with ServiceUnknown after a timeout -- so it
// is asked about first and reported as unavailable rather than as a failure.
bool serviceIsRunning() {
  GDBusConnection *bus = sessionBus();
  if (bus == nullptr) {
    return false;
  }
  static const bool running = [bus]() {
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(bus,
                                                  "org.freedesktop.DBus",
                                                  "/org/freedesktop/DBus",
                                                  "org.freedesktop.DBus",
                                                  "NameHasOwner",
                                                  g_variant_new("(s)", kBusName),
                                                  G_VARIANT_TYPE("(b)"),
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  1000,
                                                  nullptr,
                                                  &error);
    if (reply == nullptr) {
      g_clear_error(&error);
      return false;
    }
    gboolean owned = FALSE;
    g_variant_get(reply, "(b)", &owned);
    g_variant_unref(reply);
    return owned != FALSE;
  }();
  return running;
}

// What is on screen: the caller's identifier against the numeric id the service
// answered with, which is the only handle `CloseNotification` takes.
//
// Guarded because the Expo module is called from the JavaScript thread and
// nothing here hops to the main one -- a D-Bus call is not a widget.
std::mutex &shownMutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<std::string, guint32> &shown() {
  static std::unordered_map<std::string, guint32> map;
  return map;
}

} // namespace

NotificationSupport notificationSupport() {
  if (sessionBus() == nullptr) {
    return {false, "there is no session bus to send a notification on"};
  }
  if (!serviceIsRunning()) {
    return {false, "no notification service is running on the session bus"};
  }
  return {true, ""};
}

bool showNotification(const std::string &identifier, const NotificationContent &content) {
  GDBusConnection *bus = sessionBus();
  if (bus == nullptr || !serviceIsRunning()) {
    return false;
  }

  // The subtitle has nowhere of its own to go: the specification has a summary
  // and a body and nothing between them, so it joins the body rather than being
  // dropped. Dropping it would lose text an app chose to show.
  std::string body = content.body;
  if (!content.subtitle.empty()) {
    body = body.empty() ? content.subtitle : content.subtitle + "\n" + body;
  }

  // Notify(app_name, replaces_id, app_icon, summary, body, actions, hints,
  // timeout). `replaces_id` 0 means a new notification; -1 as the timeout means
  // the service's own default, which is what a desktop's settings say.
  GError *error = nullptr;
  GVariant *reply = g_dbus_connection_call_sync(
      bus,
      kBusName,
      kObjectPath,
      kInterface,
      "Notify",
      g_variant_new("(susssasa{sv}i)",
                    "basalt-core",
                    0U,
                    "",
                    content.title.c_str(),
                    body.c_str(),
                    nullptr,
                    nullptr,
                    -1),
      G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE,
      2000,
      nullptr,
      &error);
  if (reply == nullptr) {
    g_warning("could not show a notification: %s",
              error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
    return false;
  }

  guint32 id = 0;
  g_variant_get(reply, "(u)", &id);
  g_variant_unref(reply);

  const std::lock_guard<std::mutex> lock(shownMutex());
  shown()[identifier] = id;
  return true;
}

bool dismissNotification(const std::string &identifier) {
  GDBusConnection *bus = sessionBus();
  guint32 id = 0;
  {
    const std::lock_guard<std::mutex> lock(shownMutex());
    const auto it = shown().find(identifier);
    if (it == shown().end()) {
      return false;
    }
    id = it->second;
    shown().erase(it);
  }
  if (bus == nullptr) {
    return false;
  }

  // Fire and forget: the reply carries nothing, and a notification the user
  // already dismissed closes with no error anyway.
  g_dbus_connection_call(bus,
                         kBusName,
                         kObjectPath,
                         kInterface,
                         "CloseNotification",
                         g_variant_new("(u)", id),
                         nullptr,
                         G_DBUS_CALL_FLAGS_NONE,
                         2000,
                         nullptr,
                         nullptr,
                         nullptr);
  return true;
}

void dismissAllNotifications() {
  std::vector<std::string> identifiers;
  {
    const std::lock_guard<std::mutex> lock(shownMutex());
    identifiers.reserve(shown().size());
    for (const auto &[identifier, id] : shown()) {
      (void)id;
      identifiers.push_back(identifier);
    }
  }
  for (const std::string &identifier : identifiers) {
    dismissNotification(identifier);
  }
}

std::vector<std::string> presentedNotifications() {
  const std::lock_guard<std::mutex> lock(shownMutex());
  std::vector<std::string> identifiers;
  identifiers.reserve(shown().size());
  for (const auto &[identifier, id] : shown()) {
    (void)id;
    identifiers.push_back(identifier);
  }
  return identifiers;
}

} // namespace basalt
