/**
 * A React Native app that asks `expo-notifications` for a notification.
 *
 * React Native has no notification API to port -- `PushNotificationIOS` is a
 * separate package and Android's is a library -- so the contract worth
 * implementing is the one an app is most likely already using. That is the same
 * argument react-native-gesture-handler was ported on, and expo-clipboard
 * before it: the package's JavaScript is unchanged, and what this platform
 * supplies is the native modules underneath it.
 *
 * What each desktop can actually do differs, and the API already has a way to
 * say so -- which is why the first thing this checks is the permission, exactly
 * as expo's own documentation tells an app to. Linux can show one through
 * `org.freedesktop.Notifications` and reports `granted`; macOS and Windows
 * report `denied` with a reason, because both need the host to be an installed,
 * bundled application and it is not one yet. See native/core/Notifications.h.
 *
 * Every step is logged, which is what the end-to-end suite reads.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, View} from 'react-native';
import * as Notifications from 'expo-notifications';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  pip: {width: 28, height: 28, borderRadius: 14, backgroundColor: '#56c98a'},
});

function App() {
  const [done, setDone] = React.useState(false);

  React.useEffect(() => {
    (async () => {
      // Importing the package at all is the first thing that used to fail:
      // `requireNativeModule` throws on a name it cannot find, and
      // expo-notifications asks for twelve of them.
      console.log('notifications: imported');

      const permission = await Notifications.getPermissionsAsync();
      console.log(`notifications: status ${permission.status}`);
      console.log(`notifications: granted ${permission.granted}`);
      if (permission.reason) {
        console.log(`notifications: reason ${permission.reason}`);
      }

      try {
        const identifier = await Notifications.scheduleNotificationAsync({
          content: {title: 'basalt-core', body: 'A notification from a desktop'},
          // Null means deliver now, which is the modern spelling of what
          // `presentNotificationAsync` was deprecated in favour of.
          trigger: null,
        });
        console.log(`notifications: scheduled ${typeof identifier === 'string'}`);
        const presented = await Notifications.getPresentedNotificationsAsync();
        console.log(`notifications: presented ${presented.length}`);
        await Notifications.dismissAllNotificationsAsync();
        console.log('notifications: dismissed');
      } catch (error) {
        // The honest outcome where the desktop cannot show one. An app is
        // expected to have checked the permission first; this reports the
        // rejection rather than hiding it.
        console.log(`notifications: rejected ${error.message}`);
      }

      // A method this platform does not implement at all. expo's own check
      // reports it by name rather than the call silently doing nothing, which
      // is what leaving it off buys.
      try {
        await Notifications.getNotificationChannelsAsync();
        console.log('notifications: channels answered');
      } catch (error) {
        console.log(`notifications: channels unavailable (${error.constructor.name})`);
      }

      setDone(true);
    })();
  }, []);

  return <View style={styles.page}>{done ? <View style={styles.pip} /> : null}</View>;
}

AppRegistry.registerComponent('BasaltNotifications', () => App);
