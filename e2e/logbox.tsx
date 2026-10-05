'use strict';
import * as React from 'react';
import {AppRegistry, StyleSheet, Text, View} from 'react-native';
const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  label: {color: '#f7f8fa', fontSize: 20},
});
function App() {
  React.useEffect(() => {
    console.warn('a warning that should raise a yellow box');
    setTimeout(() => {
      console.error('an error that should raise a red box');
    }, 1500);
  }, []);
  return (
    <View style={styles.page}>
      <Text style={styles.label}>the app behind the box</Text>
    </View>
  );
}
AppRegistry.registerComponent('BasaltLogBox', () => App);
