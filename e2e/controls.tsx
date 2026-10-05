/**
 * A React Native app about the four components that are not a box.
 *
 * <ActivityIndicator>, <Switch>, <Modal> and <RefreshControl> are the ones a
 * desktop host has to grow a real control for: everything else in React Native
 * is a rectangle with a colour, a picture or some text in it, and these are a
 * spinner, a toggle, a window-filling overlay and a pull gesture. Each is here
 * once, in a layout with fixed coordinates so the integration test can press
 * exactly one of them at a time.
 *
 * What each one proves:
 *
 *   spinners   that `animating` and `size` reach the host at all. The stopped
 *              one is the interesting half -- `hidesWhenStopped` means it draws
 *              nothing, and a host that ignores it shows a still spinner.
 *   switches   that a toggle becomes `onValueChange` and that the *app* is what
 *              moves the switch. The third is disabled and must not move.
 *   modal      that `visible` mounts an overlay over the whole surface, that
 *              `onShow` fires, and that Escape reaches `onRequestClose`. The
 *              modal does not close itself: the handler does, which is React
 *              Native's contract and the thing most likely to be implemented
 *              as "the host closes it and tells you afterwards".
 *   refresh    that a wheel past the top of a list fires `onRefresh` once, and
 *              that `refreshing` shows the spinner. See core/PullToRefresh.h
 *              for why a desktop counts the wheel rather than measuring a pull.
 *
 * Every interesting thing is logged, so the test reads stderr rather than
 * pixels -- and the tree dump is deterministic between presses, so
 * scripts/compare_all.sh can diff it across the three hosts.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {
  ActivityIndicator,
  AppRegistry,
  Modal,
  Platform,
  Pressable,
  RefreshControl,
  ScrollView,
  StyleSheet,
  Switch,
  Text,
  View,
} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  // A fixed height, so the rows below sit at the same y on all three hosts.
  // Text measurement is the one thing Pango, Core Text and DirectWrite will
  // never agree on, and the integration test presses fixed coordinates.
  label: {color: '#aab', fontSize: 13, height: 22},
  row: {flexDirection: 'row', alignItems: 'center', height: 56},
  cell: {width: 90, height: 56, justifyContent: 'center'},
  button: {
    width: 180,
    height: 48,
    borderRadius: 8,
    backgroundColor: '#4285f4',
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonText: {color: '#fff', fontSize: 15},
  list: {flex: 1, marginTop: 12, backgroundColor: '#1b1f2a', borderRadius: 10},
  item: {height: 44, justifyContent: 'center', paddingHorizontal: 14},
  itemText: {color: '#dde', fontSize: 14},
  // The overlay React Native itself puts inside a <Modal>: absolutely
  // positioned to fill the modal host, which is sized to the window by the
  // host rather than by any style here.
  sheet: {
    flex: 1,
    backgroundColor: '#000a',
    alignItems: 'center',
    justifyContent: 'center',
  },
  card: {
    width: 320,
    padding: 24,
    borderRadius: 14,
    backgroundColor: '#20242f',
    alignItems: 'center',
  },
  cardText: {color: '#eef', fontSize: 16, marginBottom: 16},
});

const ITEMS = Array.from({length: 12}, (_, index) => `Row ${index + 1}`);

function App() {
  const [first, setFirst] = React.useState(false);
  const [second, setSecond] = React.useState(true);
  const [modalVisible, setModalVisible] = React.useState(false);
  const [refreshing, setRefreshing] = React.useState(false);

  const onRefresh = React.useCallback(() => {
    console.log('refresh requested');
    setRefreshing(true);
    // A real app would be fetching. This just has to end, so that the
    // controlled `refreshing` prop is seen going both ways.
    setTimeout(() => {
      console.log('refresh finished');
      setRefreshing(false);
    }, 1200);
  }, []);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>ActivityIndicator</Text>
      <View style={styles.row}>
        <View style={styles.cell}>
          <ActivityIndicator size="small" />
        </View>
        <View style={styles.cell}>
          <ActivityIndicator size="large" color="#4285f4" />
        </View>
        <View style={styles.cell}>
          <ActivityIndicator size="small" animating={false} />
        </View>
      </View>

      <Text style={styles.label}>Switch</Text>
      <View style={styles.row}>
        <View style={styles.cell}>
          <Switch
            value={first}
            onValueChange={next => {
              console.log(`switch one -> ${next}`);
              setFirst(next);
            }}
          />
        </View>
        <View style={styles.cell}>
          <Switch
            value={second}
            onValueChange={next => {
              console.log(`switch two -> ${next}`);
              setSecond(next);
            }}
          />
        </View>
        <View style={styles.cell}>
          <Switch value={true} disabled={true} onValueChange={() => {
            console.log('switch three -> moved, which it must not');
          }} />
        </View>
      </View>

      <Text style={styles.label}>Modal</Text>
      <Pressable
        style={styles.button}
        onPress={() => {
          console.log('opening modal');
          setModalVisible(true);
        }}>
        <Text style={styles.buttonText}>Open modal</Text>
      </Pressable>

      <ScrollView
        style={styles.list}
        refreshControl={
          <RefreshControl refreshing={refreshing} onRefresh={onRefresh} />
        }>
        {ITEMS.map(item => (
          <View key={item} style={styles.item}>
            <Text style={styles.itemText}>{item}</Text>
          </View>
        ))}
      </ScrollView>

      <Modal
        visible={modalVisible}
        transparent={true}
        animationType="none"
        onShow={() => console.log('modal shown')}
        onRequestClose={() => {
          // The app closes it, not the host. React Native's contract is that
          // this is a request; a modal whose app ignores it stays up.
          console.log('modal close requested');
          setModalVisible(false);
        }}>
        <View style={styles.sheet}>
          <View style={styles.card}>
            <Text style={styles.cardText}>Press Escape to close</Text>
            <Pressable
              style={styles.button}
              onPress={() => {
                console.log('modal dismissed by button');
                setModalVisible(false);
              }}>
              <Text style={styles.buttonText}>Close</Text>
            </Pressable>
          </View>
        </View>
      </Modal>
    </View>
  );
}

AppRegistry.registerComponent('BasaltControls', () => App);
