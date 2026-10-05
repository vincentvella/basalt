// A <Canvas> that draws, and then reads back what it drew.
//
// The point is the read-back. A Canvas that mounts, registers and shows nothing
// looks identical from the outside to one that works, so this asks Skia for the
// pixels it just rendered and says how many there are. That exercises the whole
// path in one go: React builds a picture, the JSI API pushes it to the native
// view by nativeId, the view draws it into a Metal surface, and the snapshot
// comes back through the same API.
//
// Needs an app with @shopify/react-native-skia installed to bundle against; see
// scripts/bundle.sh and BASALT_SKIA. There is no such package in this repository
// and deliberately so -- Skia is borrowed from the app, never vendored.
import React, {useEffect} from 'react';
import {AppRegistry, StyleSheet, Text, View} from 'react-native';
import {Canvas, Fill, Rect, useCanvasRef} from '@shopify/react-native-skia';

function App() {
  const ref = useCanvasRef();

  useEffect(() => {
    // After a frame: the picture is pushed on commit and drawn on the next
    // display cycle, so a snapshot taken synchronously here would be of an
    // empty surface and would prove the opposite of what it looks like.
    const timer = setTimeout(() => {
      try {
        const image = ref.current?.makeImageSnapshot();
        if (image == null) {
          console.log('skia snapshot: null');
          return;
        }
        console.log(`skia snapshot ${image.width()}x${image.height()}`);

        // The colours, not the byte count. A blank surface encodes to about the
        // same size as this one -- a solid fill and a rectangle compress almost
        // as well as nothing at all -- so a length is not evidence that anything
        // was drawn. Two pixels are: one inside the rectangle and one outside it
        // but inside the fill, which also tells "drew something" from "drew it
        // where I asked".
        const pixels = image.readPixels();
        const scale = image.width() / 200;
        const rgba = (x, y) => {
          const i = (Math.round(y * scale) * image.width() + Math.round(x * scale)) * 4;
          const hex = n => n.toString(16).padStart(2, '0');
          return `#${hex(pixels[i])}${hex(pixels[i + 1])}${hex(pixels[i + 2])}`;
        };
        // (60,60) is inside the 20,20 120x80 rect; (180,120) is outside it and
        // inside the Fill.
        console.log(`skia pixel inside the rect: ${rgba(60, 60)}`);
        console.log(`skia pixel inside the fill: ${rgba(180, 120)}`);
      } catch (error) {
        console.log(`skia snapshot failed: ${error.message}`);
      }
    }, 1200);
    return () => clearTimeout(timer);
  }, [ref]);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>skia canvas</Text>
      <Canvas ref={ref} style={styles.canvas}>
        <Fill color="#1f2129" />
        <Rect x={20} y={20} width={120} height={80} color="#ff0066" />
      </Canvas>
    </View>
  );
}

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#14151a', padding: 24},
  label: {color: '#e6e6e6', fontSize: 16, marginBottom: 12},
  canvas: {width: 200, height: 140},
});

AppRegistry.registerComponent('BasaltSkia', () => App);
