/**
 * An app you can drop a file onto.
 *
 * React Native has no API for this and a phone has no pointer to drag with,
 * so what is on screen here is the whole of the feature: a view that says it
 * takes files, and what it was given.
 *
 * Two targets, one inside the other, because the rule that matters is which
 * one is told: the innermost accepting view under the pointer, so a target
 * inside a target wins and a label inside one needs no marking of its own.
 *
 * And one <DragSource>, for the other direction. Nothing automated drives it:
 * once a drag begins the system owns it, and no instrument can put a file
 * manager on the other end. Drag the green row into one and see what lands --
 * that is the whole of its verification, which is why it is here at all.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';
import {DragSource, DropTarget} from 'basalt-core';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  heading: {color: '#f7f8fa', fontSize: 20, height: 28},
  line: {color: '#9aa3b2', fontSize: 15, height: 22},
  outer: {height: 260, backgroundColor: '#1a1e28', padding: 24},
  inner: {height: 120, backgroundColor: '#2b3140', padding: 16},
  over: {backgroundColor: '#4285f4'},
  draggable: {height: 44, backgroundColor: '#56c98a', padding: 12, marginTop: 16},
});

function App() {
  const [outer, setOuter] = React.useState('nothing yet');
  const [inner, setInner] = React.useState('nothing yet');
  const [overInner, setOverInner] = React.useState(false);

  return (
    <View style={styles.page}>
      <Text style={styles.heading}>drop a file</Text>
      <DropTarget
        accepts={['files']}
        style={styles.outer}
        onDrop={payload => {
          console.log(`outer drop ${payload.files.join(' ')}`);
          setOuter(payload.files.join(' ') || 'no files');
        }}>
        <Text style={styles.line}>outer {outer}</Text>
        <DropTarget
          accepts={['files']}
          style={[styles.inner, overInner && styles.over]}
          onDragOver={() => setOverInner(true)}
          onDragLeave={() => setOverInner(false)}
          onDrop={payload => {
            console.log(`inner drop ${payload.files.join(' ')}`);
            setOverInner(false);
            setInner(payload.files.join(' ') || 'no files');
          }}>
          <Text style={styles.line}>inner {inner}</Text>
        </DropTarget>
      </DropTarget>
      <DragSource file="/tmp/dragged-out.txt" style={styles.draggable}>
        <Text style={styles.line}>drag me out</Text>
      </DragSource>
    </View>
  );
}

AppRegistry.registerComponent('BasaltDrop', () => App);
