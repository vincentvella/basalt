// A stand-in for a desktop's notification daemon.
//
// The one thing missing from testing notifications: `GtkNotifications.cpp`
// sends `Notify` over the session bus, and neither a Mac nor a CI runner has
// anything listening for it. Without something here the only paths that could
// be exercised were the two that report why nothing can be sent.
//
// It owns `org.freedesktop.Notifications` on whatever bus it is started on,
// answers `Notify` with an increasing id, and prints what it was asked to show
// so the end-to-end suite can assert on it. Everything a real daemon does
// beyond that -- the window, the timeout, the actions, the signals back -- is
// what a test does not need and what would make this a second implementation
// rather than a fixture.
//
// Started by scripts/integration_test.py, which also starts the bus. See
// docs/TESTING.md.
#include <gio/gio.h>
#include <stdio.h>

static const char *kXml =
    "<node><interface name='org.freedesktop.Notifications'>"
    "  <method name='Notify'>"
    "    <arg type='s' name='app_name' direction='in'/>"
    "    <arg type='u' name='replaces_id' direction='in'/>"
    "    <arg type='s' name='app_icon' direction='in'/>"
    "    <arg type='s' name='summary' direction='in'/>"
    "    <arg type='s' name='body' direction='in'/>"
    "    <arg type='as' name='actions' direction='in'/>"
    "    <arg type='a{sv}' name='hints' direction='in'/>"
    "    <arg type='i' name='timeout' direction='in'/>"
    "    <arg type='u' name='id' direction='out'/>"
    "  </method>"
    "  <method name='CloseNotification'>"
    "    <arg type='u' name='id' direction='in'/>"
    "  </method>"
    "  <method name='GetCapabilities'>"
    "    <arg type='as' name='caps' direction='out'/>"
    "  </method>"
    "</interface></node>";

static guint32 nextId = 1;

static void onCall(GDBusConnection *connection, const gchar *sender, const gchar *path,
                   const gchar *interface, const gchar *method, GVariant *parameters,
                   GDBusMethodInvocation *invocation, gpointer user_data) {
  (void)connection; (void)sender; (void)path; (void)interface; (void)user_data;

  if (g_strcmp0(method, "Notify") == 0) {
    const gchar *app = nullptr, *icon = nullptr, *summary = nullptr, *body = nullptr;
    guint32 replaces = 0;
    gint timeout = 0;
    GVariantIter *actions = nullptr;
    GVariant *hints = nullptr;
    g_variant_get(parameters, "(&su&s&s&sas@a{sv}i)", &app, &replaces, &icon, &summary, &body,
                  &actions, &hints, &timeout);
    printf("STUB Notify app=%s summary=%s body=%s\n", app, summary, body);
    fflush(stdout);
    if (actions != nullptr) g_variant_iter_free(actions);
    if (hints != nullptr) g_variant_unref(hints);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", nextId++));
    return;
  }
  if (g_strcmp0(method, "CloseNotification") == 0) {
    guint32 id = 0;
    g_variant_get(parameters, "(u)", &id);
    printf("STUB CloseNotification id=%u\n", id);
    fflush(stdout);
    g_dbus_method_invocation_return_value(invocation, nullptr);
    return;
  }
  if (g_strcmp0(method, "GetCapabilities") == 0) {
    GVariantBuilder caps;
    g_variant_builder_init(&caps, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&caps, "s", "body");
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", &caps));
    return;
  }
  g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.UnknownMethod",
                                             "no");
}

static const GDBusInterfaceVTable kVTable = {onCall, nullptr, nullptr, {nullptr}};

static void onBusAcquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  (void)name;
  auto *info = static_cast<GDBusNodeInfo *>(user_data);
  g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
                                    info->interfaces[0], &kVTable, nullptr, nullptr, nullptr);
}

static void onNameAcquired(GDBusConnection *c, const gchar *name, gpointer d) {
  (void)c; (void)d;
  printf("STUB ready %s\n", name);
  fflush(stdout);
}

int main(void) {
  GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(kXml, nullptr);
  g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.Notifications", G_BUS_NAME_OWNER_FLAGS_NONE,
                 onBusAcquired, onNameAcquired, nullptr, info, nullptr);
  g_main_loop_run(g_main_loop_new(nullptr, FALSE));
  return 0;
}
