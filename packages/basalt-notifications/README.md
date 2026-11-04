# basalt-notifications

Desktop notifications for [Basalt](https://basaltjs.dev), on Linux, macOS and
Windows.

```bash
npm install basalt-notifications
```

**This package has no JavaScript API of its own.** It implements
`expo-notifications`' contract natively, so what you write against is
`expo-notifications`, the same way `react-native-gesture-handler` works here.
React Native has nothing in core to be compatible with: `PushNotificationIOS`
is a separate package and Android's is a library.

```js
import * as Notifications from 'expo-notifications';

const {status} = await Notifications.getPermissionsAsync();
if (status === 'granted') {
  await Notifications.scheduleNotificationAsync({
    content: {title: 'Export finished', body: clip.name},
    trigger: null,
  });
}
```

[Full reference](https://basaltjs.dev/docs/reference/notifications).

## What is implemented

Presenting a notification, dismissing it, and the permission calls.

A trigger that is not `null` is **rejected by name** rather than silently
never firing. Scheduling for later needs a timer that outlives the process and
a store to keep the queue in, which is a feature rather than a branch. A
`null` trigger is what expo documents as "deliver immediately".

Check `getPermissionsAsync()` before presenting anything, which is what
expo-notifications' own documentation says to do. Where a desktop cannot show
one, it answers `denied` with a reason rather than failing at the call.

## Why it is a separate package

It is permission-gated, and the core platform should not ask an app's users
for anything the app did not ask for. Installing it is the asking.

Each desktop uses its own mechanism, and what any of them will do depends on
the app having an identity: a bundle identifier on macOS, an AppUserModelID on
Windows, a `.desktop` file on Linux. The run command packages the binary into
a real application on every run partly for this reason.

Its C++ reaches the host build through
[`basalt.native`](https://basaltjs.dev/docs/reference/app-json#basaltnative-in-a-package),
the same seam any package can use, so nothing in Basalt keeps a list of
package names.

## Licence

MIT.
