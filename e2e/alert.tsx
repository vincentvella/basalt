'use strict';
import * as React from 'react';
import {Alert, AppRegistry, StyleSheet, View} from 'react-native';

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#1f2129', padding: 24},
  box: {width: 200, height: 80, backgroundColor: '#4d8cf2', borderRadius: 8},
  ticked: {backgroundColor: '#59cc8c'},
});

function App() {
  const [ticks, setTicks] = React.useState(0);

  React.useEffect(() => {
    setTimeout(() => {
      console.log('showing the alert');
      Alert.alert('Delete this?', 'It cannot be undone.', [
        {text: 'Cancel', onPress: () => console.log('chose: Cancel')},
        {text: 'Delete', onPress: () => console.log('chose: Delete')},
      ]);
    }, 600);
    // A heartbeat: if the alert blocked the main queue, this would stop.
    const beat = setInterval(() => setTicks(t => t + 1), 300);
    return () => clearInterval(beat);
  }, []);

  React.useEffect(() => {
    console.log(`tick ${ticks}`);
  }, [ticks]);

  return (
    <View style={styles.page}>
      <View style={[styles.box, ticks > 4 && styles.ticked]} />
    </View>
  );
}

AppRegistry.registerComponent('BasaltAlert', () => App);
