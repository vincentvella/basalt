/**
 * An app that draws its own title bar.
 *
 * `style: 'hidden'` takes the system's title bar away and gives the app the
 * whole window, which means the app owes three things back: a header to look
 * at, room for the caption buttons the system still draws, and a way to move
 * the window. The last is `<TitleBar.DragRegion>`, and it is the part a test
 * cannot press: moving a window means a real pointer, so this app exists to
 * be dragged by hand.
 *
 * What a scenario *can* read off it is the arithmetic: the metrics are not
 * zero under the hidden style, the header is exactly that tall, and the marks
 * reached the host. Those are what an app lays itself out against, and they
 * are the same three numbers on all three desktops.
 *
 * Try it by hand: drag the header and the window moves, drag the button and
 * it does not, double-click the header and the window zooms.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, Pressable, StyleSheet, Text, View} from 'react-native';
import {TitleBar, useTitleBarMetrics, windowControl} from 'basalt-core';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#16181d'},
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: '#1c1f26',
    paddingHorizontal: 12,
  },
  title: {color: '#f7f6f3', fontSize: 14, flexGrow: 1, height: 20},
  button: {backgroundColor: '#b8431e', paddingHorizontal: 12, paddingVertical: 4},
  buttonLabel: {color: '#f7f6f3', fontSize: 13, height: 18},
  body: {flexGrow: 1, padding: 24},
  line: {color: '#9aa0a8', fontSize: 15, height: 22},
});

function App() {
  const {height, buttonsWidth, style} = useTitleBarMetrics();
  const [presses, setPresses] = React.useState(0);

  React.useEffect(() => {
    // Logged as well as rendered, for the same reason displays.tsx logs: a
    // person running this by hand wants the numbers without squinting.
    console.log(`title bar style ${style} height ${height} buttons ${buttonsWidth}`);
  }, [style, height, buttonsWidth]);

  return (
    <View style={styles.page}>
      <TitleBar style="hidden" title="Basalt" />
      <TitleBar.DragRegion
        style={[
          styles.header,
          // The caption buttons are the system's and they are drawn over this
          // header, so the app leaves them room. Which side depends on the
          // desktop: macOS puts them left, Windows and Linux right.
          {height},
          Platform.OS === 'macos'
            ? {paddingLeft: buttonsWidth + 12}
            : {paddingRight: buttonsWidth + 12},
        ]}>
        <Text style={styles.title}>drag me</Text>
        {/* Marked, so it stays a button inside a header that drags. */}
        <TitleBar.NoDragRegion>
          <Pressable style={styles.button} onPress={() => setPresses(count => count + 1)}>
            <Text style={styles.buttonLabel}>{`pressed ${presses}`}</Text>
          </Pressable>
        </TitleBar.NoDragRegion>
      </TitleBar.DragRegion>

      <View style={styles.body}>
        <Text style={styles.line}>{`style ${style}`}</Text>
        <Text style={styles.line}>{`height ${height}`}</Text>
        <Text style={styles.line}>{`buttons ${buttonsWidth}`}</Text>
        <Pressable style={styles.button} onPress={() => windowControl.toggleMaximize()}>
          <Text style={styles.buttonLabel}>toggle maximize</Text>
        </Pressable>
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltTitleBar', () => App);
