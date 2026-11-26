/**
 * One <TextInput> that logs every change, run as a *development* bundle.
 *
 * A development build mounts React Native's DebuggingOverlay over the whole
 * app, inside a `pointerEvents="none"` View. While AppKit's hit testing ignored
 * the prop, that overlay took every click: buttons kept working, being
 * hit-tested React Native's way, and no text field could be focused by clicking
 * it. A production bundle has no overlay, which is why the demo's own
 * real-click scenario never saw it. The scenario is
 * `test_text_input_click_in_a_dev_build`.
 *
 * @format
 */
'use strict';
import * as React from 'react';
import {AppRegistry, TextInput, View} from 'react-native';

function App(): React.ReactElement {
  const [text, setText] = React.useState('');
  return (
    <View style={{flex: 1, padding: 40, backgroundColor: '#f6f7f9'}}>
      <TextInput
        accessibilityLabel="type here"
        placeholder="type here"
        value={text}
        onChangeText={t => {
          setText(t);
          console.log(`TYPED ${t}`);
        }}
        style={{height: 40, borderWidth: 1, borderColor: '#888', paddingHorizontal: 8, fontSize: 16}}
      />
    </View>
  );
}
AppRegistry.registerComponent('Typing', () => App);
