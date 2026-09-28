// Keyboard shortcuts on a view.
//
// Four things, because four things can go wrong independently: a declared
// combination must fire, an undeclared one must not, a modifier must be part of
// the match, and a list that changes must take effect. The last is the failure
// mode <KeyHandler> invites by registering through a module rather than through
// nativeID -- silently stopping after a re-render -- so it is the one worth a
// scenario most.
import React, {useCallback, useMemo, useState} from 'react';
import {AppRegistry, StyleSheet, Text, View} from 'react-native';
import {KeyHandler} from 'react-native-basalt';

function App() {
  const [pressed, setPressed] = useState([]);
  // Starts with `m`, and `j` is added once `m` has arrived. So the second press
  // only fires if re-registering worked -- if the list were captured once, `j`
  // would be undeclared for ever and the log would stop after one line.
  const [extended, setExtended] = useState(false);

  const keys = useMemo(
    () =>
      extended
        ? [{key: 'm'}, {key: 'j'}, {key: 'z', metaKey: true}]
        : [{key: 'm'}, {key: 'z', metaKey: true}],
    [extended],
  );

  const onKeyDown = useCallback(pressedKey => {
    const modifiers = [
      pressedKey.altKey ? 'alt' : null,
      pressedKey.ctrlKey ? 'ctrl' : null,
      pressedKey.metaKey ? 'meta' : null,
      pressedKey.shiftKey ? 'shift' : null,
    ]
      .filter(Boolean)
      .join('+');
    const described = modifiers ? `${pressedKey.key}+${modifiers}` : pressedKey.key;
    console.log(`key ${described}`);
    setPressed(previous => [...previous, described]);
    if (pressedKey.key === 'm') {
      setExtended(true);
    }
  }, []);

  return (
    <KeyHandler keys={keys} onKeyDown={onKeyDown} style={styles.page}>
      <Text style={styles.label}>keys</Text>
      {/* In the tree, so a run that pressed nothing is visibly different from
          one whose presses went nowhere. */}
      <Text style={styles.log}>{`pressed ${pressed.length}: ${pressed.join(' ')}`}</Text>
    </KeyHandler>
  );
}

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#14151a', padding: 24},
  label: {color: '#e6e6e6', fontSize: 16, marginBottom: 8},
  log: {color: '#9ad', fontSize: 14},
});

AppRegistry.registerComponent('BasaltKeys', () => App);
