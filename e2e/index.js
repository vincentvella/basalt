/**
 * The demo app, in React.
 *
 * Ordinary React Native throughout: hooks, StyleSheet, flexbox, Text, View,
 * Image, Pressable and ScrollView. Nothing here knows which desktop it is
 * running on -- which is the point, and is why scripts/compare_all.sh can diff
 * its tree between the two.
 */

'use strict';

import React, {useRef, useState} from 'react';
import {
  AppRegistry,
  Image,
  Platform,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const PALETTE = ['#4285f4', '#9b59f6', '#f26f56', '#56c98a', '#f2c14e'];

const IMAGE = {uri: 'assets/checker.png'};

const ROWS = Array.from({length: 24}, (_, index) => index);

function Button({label, onPress, color}) {
  return (
    <Pressable
      onPress={onPress}
      accessible={true}
      accessibilityRole="button"
      accessibilityLabel={label}
      accessibilityHint={`Scrolls the list to the ${label.split(' ').pop()}`}
      style={({pressed}) => [
        styles.button,
        {backgroundColor: color, opacity: pressed ? 0.55 : 1},
      ]}>
      <Text style={styles.buttonLabel}>{label}</Text>
    </Pressable>
  );
}

// borderRadius, per-edge borders, rotation and zIndex, all in one strip. The
// rotated card carries a marker in its top-left corner: a positive angle turns
// clockwise, so the marker should end up towards the top right.
function PropsStrip() {
  return (
    <View style={styles.strip}>
      <View style={styles.pill}>
        <Text style={styles.pillLabel}>radius</Text>
      </View>

      <View style={styles.bordered}>
        <Text style={styles.pillLabel}>borders</Text>
      </View>

      <View style={styles.rotatedHolder}>
        <View style={styles.rotated}>
          <View style={styles.marker} />
        </View>
        <View style={styles.flipped} nativeID="flipped-card" />
      </View>

      <View style={styles.stack}>
        <View style={[styles.stacked, {backgroundColor: PALETTE[2], zIndex: 2}]} />
        <View
          style={[
            styles.stacked,
            {backgroundColor: PALETTE[3], left: 28, top: 12, zIndex: 1},
          ]}
        />
      </View>
    </View>
  );
}

function App() {
  const [offsetY, setOffsetY] = useState(0);
  const [name, setName] = useState('');
  const [focused, setFocused] = useState(false);
  const scroller = useRef(null);
  const field = useRef(null);

  return (
    <View style={styles.root}>
      <Text style={styles.heading}>
        React Native on the desktop
      </Text>

      <PropsStrip />

      <View style={styles.statusRow}>
        <Text style={styles.status}>contentOffset.y</Text>
        <Text style={styles.statusValue}>{String(Math.round(offsetY))}</Text>
      </View>

      <View style={styles.fieldRow}>
        <TextInput
          ref={field}
          style={[styles.field, focused && styles.fieldFocused]}
          value={name}
          onChangeText={setName}
          onFocus={() => setFocused(true)}
          onBlur={() => setFocused(false)}
          onSubmitEditing={() => setName(name.toUpperCase())}
          // Logged rather than rendered: both are about *when* they arrive, and
          // a rendered value only says what the last one was. onKeyPress in
          // particular has to be one event per key -- a field that reported the
          // whole string each time would look identical in the tree.
          onKeyPress={event => {
            console.log(`key: ${event.nativeEvent.key}`);
          }}
          onSelectionChange={event => {
            const {start, end} = event.nativeEvent.selection;
            console.log(`selection: ${start}-${end}`);
          }}
          placeholder="type a name, then press Enter"
          accessibilityLabel="Name field"
        />
        <Text style={styles.echo} numberOfLines={1}>
          {name.length > 0 ? `hello, ${name}` : 'waiting for onChangeText'}
        </Text>
      </View>

      <ScrollView
        ref={scroller}
        accessibilityRole="list"
        accessibilityLabel="Coloured rows"
        style={styles.scroller}
        contentContainerStyle={styles.scrollerContent}
        scrollEventThrottle={16}
        onScroll={event => setOffsetY(event.nativeEvent.contentOffset.y)}>
        {ROWS.map(index => (
          <View
            key={index}
            style={[styles.row, {backgroundColor: PALETTE[index % PALETTE.length]}]}>
            <Image source={IMAGE} resizeMode="cover" style={styles.thumb} />
            <Text style={styles.rowLabel}>row {index}</Text>
          </View>
        ))}
      </ScrollView>

      <View style={styles.controls}>
        <Button
          label="scroll to top"
          color={PALETTE[0]}
          onPress={() => scroller.current?.scrollTo({y: 0, animated: false})}
        />
        <Button
          label="scroll to end"
          color={PALETTE[3]}
          onPress={() => scroller.current?.scrollToEnd({animated: false})}
        />
        <Button
          label="focus the field"
          color={PALETTE[1]}
          onPress={() => field.current?.focus()}
        />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 24, backgroundColor: '#11131a'},
  heading: {fontSize: 26, fontWeight: '700', color: '#f7f8fa', marginBottom: 12},
  platform: {color: '#56c98a'},

  strip: {flexDirection: 'row', alignItems: 'center', marginBottom: 14},
  pill: {
    width: 120,
    height: 64,
    borderRadius: 32,
    backgroundColor: PALETTE[1],
    alignItems: 'center',
    justifyContent: 'center',
    marginRight: 16,
  },
  pillLabel: {fontSize: 15, fontWeight: '600', color: '#11131a'},
  bordered: {
    width: 120,
    height: 64,
    borderTopLeftRadius: 18,
    borderBottomRightRadius: 18,
    borderWidth: 4,
    borderColor: PALETTE[4],
    borderLeftColor: PALETTE[2],
    backgroundColor: '#1a1e28',
    alignItems: 'center',
    justifyContent: 'center',
    marginRight: 16,
  },
  rotatedHolder: {width: 120, height: 64, marginRight: 16},
  rotated: {
    width: 64,
    height: 64,
    marginLeft: 28,
    borderRadius: 10,
    backgroundColor: PALETTE[0],
    transform: [{rotate: '20deg'}],
  },
  // Turned away from the viewer, with `backfaceVisibility: 'hidden'`, so it is
  // not drawn at all. A host ignoring the prop shows it mirrored -- which all
  // three did -- so its absence from the tree is the assertion.
  flipped: {
    width: 64,
    height: 64,
    marginLeft: 12,
    backgroundColor: PALETTE[1],
    backfaceVisibility: 'hidden',
    transform: [{rotateY: '180deg'}],
  },
  marker: {width: 16, height: 16, backgroundColor: '#f7f8fa'},
  stack: {width: 120, height: 64},
  stacked: {position: 'absolute', width: 72, height: 44, borderRadius: 8},

  fieldRow: {flexDirection: 'row', alignItems: 'center', marginBottom: 12},
  field: {
    width: 320,
    height: 44,
    marginRight: 16,
    paddingHorizontal: 12,
    borderRadius: 8,
    borderWidth: 2,
    borderColor: '#2b3140',
    backgroundColor: '#1a1e28',
    fontSize: 17,
    color: '#f7f8fa',
  },
  fieldFocused: {borderColor: PALETTE[0]},
  echo: {fontSize: 17, color: '#56c98a'},

  statusRow: {flexDirection: 'row', alignItems: 'center', marginBottom: 12},
  status: {fontSize: 15, color: '#7f8794', marginRight: 10},
  statusValue: {fontSize: 22, fontWeight: '700', color: '#f7f8fa'},
  scroller: {flex: 1, backgroundColor: '#1a1e28', marginBottom: 16, borderRadius: 12},
  scrollerContent: {padding: 12},
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    height: 72,
    marginBottom: 10,
    paddingHorizontal: 12,
    borderRadius: 10,
  },
  thumb: {width: 72, height: 48, marginRight: 16, borderRadius: 6},
  rowLabel: {fontSize: 20, fontWeight: '600', color: '#11131a'},
  controls: {flexDirection: 'row'},
  button: {
    flex: 1,
    height: 52,
    marginRight: 12,
    borderRadius: 10,
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonLabel: {fontSize: 17, fontWeight: '600', color: '#11131a'},
});

AppRegistry.registerComponent('BasaltDemo', () => App);
