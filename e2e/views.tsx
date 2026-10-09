/**
 * A real React Native app made only of `<View>`.
 *
 * This exists to test the JavaScript platform layer without needing the view
 * layer to be finished. It imports `react-native` properly, goes through
 * `AppRegistry`, renders with React and lays out with Yoga -- so every one of
 * the overrides in `packages/basalt-core/metro-config.js` has to work
 * for it to run at all. What it deliberately avoids is `<Text>`, `<Image>`,
 * `<ScrollView>` and `<TextInput>`, none of which macOS can mount yet.
 *
 * The layout is nested flex on purpose. A flat row would produce a tree that
 * agrees between two platforms by accident; this one only agrees if Yoga ran
 * on identical input and the mutations arrived in the same order.
 *
 * Nothing here branches on the platform, because `scripts/compare_hosts.sh`
 * diffs the two trees and a difference must mean a bug rather than a choice.
 * `Platform.OS` is logged instead, which is how the same script checks that
 * each host got the platform it asked for.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, View} from 'react-native';
import type {LayoutChangeEvent, ViewStyle} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

// `backgroundSize`, `backgroundPosition` and `backgroundRepeat`, which have to
// come in through a cast on the pinned React Native.
//
// v0.87.1's TypeScript API -- `ReactNativeApi.d.ts`, which is what a release
// checkout type-checks against -- declares `backgroundImage` and none of these
// three, in either spelling. Its *Flow* types declare `experimental_` versions
// of all three and its C++ reads those raw props, so the props work; they just
// cannot be written down. `main` declares the plain names in both and its C++
// reads either.
//
// So the prefixed spelling goes in through one cast, which is what `cursor`
// does below and for the same reason: the type is the narrow half, not the
// platform. See backlog/upstream.md.
const backgroundTiling = {
  experimental_backgroundSize: '20px 20px',
  experimental_backgroundPosition: 'left 5px top 5px',
  experimental_backgroundRepeat: 'repeat',
} as unknown as ViewStyle;

const styles = StyleSheet.create({
  page: {
    flex: 1,
    backgroundColor: '#1f2129',
    padding: 24,
  },
  row: {
    flexDirection: 'row',
    flex: 2,
  },
  left: {
    flex: 2,
    backgroundColor: '#4d8cf2',
    padding: 16,
    marginRight: 16,
    borderRadius: 8,
  },
  right: {
    flex: 1,
    backgroundColor: '#f27359',
    borderRadius: 8,
  },
  // A child that overflows its parent, which is React Native's default and one
  // of the first things a new view layer gets wrong.
  badge: {
    width: 120,
    height: 48,
    marginTop: 120,
    backgroundColor: '#e6eeff',
    opacity: 0.85,
  },
  // The layout probes. Absolute and off to one side, so they measure what they
  // ask about and move nothing else: `left: 0, top: 0` inside the page's
  // padding puts them at (24,24) on every host.
  // Every probe has a background, and not for looks: a view with nothing to
  // draw is flattened away before it reaches a host, and a flattened probe is
  // a probe with no line in the tree. The colour is what keeps it.
  probes: {position: 'absolute', left: 0, top: 0},
  probeBox: {width: 20, height: 10, backgroundColor: '#4d8cf2'},
  padParent: {width: 100, height: 30, padding: 12, backgroundColor: '#2b3445'},
  marginParent: {
    width: 100,
    height: 30,
    flexDirection: 'row',
    backgroundColor: '#2b3445',
  },
  marginSecond: {width: 20, height: 10, marginLeft: 7, backgroundColor: '#4d8cf2'},
  gapParent: {
    width: 100,
    height: 30,
    flexDirection: 'row',
    gap: 9,
    backgroundColor: '#2b3445',
  },
  growParent: {
    width: 100,
    height: 30,
    flexDirection: 'row',
    backgroundColor: '#2b3445',
  },
  growRest: {height: 10, flexGrow: 1, backgroundColor: '#59cc8c'},
  insetParent: {width: 100, height: 30, backgroundColor: '#2b3445'},
  insetChild: {
    position: 'absolute',
    top: 3,
    left: 4,
    width: 20,
    height: 10,
    backgroundColor: '#4d8cf2',
  },
  ratio: {height: 10, aspectRatio: 2, backgroundColor: '#59cc8c'},
  footer: {
    flex: 1,
    marginTop: 16,
    backgroundColor: '#59cc8c',
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 16,
  },
  dot: {
    width: 40,
    height: 40,
    borderRadius: 20,
    backgroundColor: '#1f2129',
  },
  // borderStyle, which is the one border prop that changes how a border is
  // drawn rather than what colour it is: both hosts stroke a dashed outline
  // instead of filling four edges. Here rather than in a host-specific app
  // because the two trees are diffed, and a style that arrived on one desktop
  // and not the other is exactly what that diff is for.
  dashed: {
    width: 40,
    height: 40,
    borderWidth: 3,
    borderColor: '#1f2129',
    borderStyle: 'dashed',
  },
  dotted: {
    width: 40,
    height: 40,
    borderWidth: 3,
    borderColor: '#1f2129',
    borderStyle: 'dotted',
  },
  // cursor, the other prop that is invisible in a picture of a still window.
  // Two keywords rather than one: a plain one, and a hyphenated one that each
  // host has to spell its own way -- GDK takes it as it stands and macOS has to
  // find an NSCursor for it.
  handy: {
    width: 40,
    height: 40,
    backgroundColor: '#4d8cf2',
    cursor: 'pointer',
  },
  // backgroundImage: a CSS gradient, which is what React Native put behind that
  // prop. The angle is deliberately not a right angle and the box is not square,
  // so the gradient line is the spec's perpendicular construction rather than
  // the diagonal -- the two differ by enough to see.
  gradient: {
    width: 80,
    height: 40,
    backgroundImage: 'linear-gradient(135deg, #4d8cf2 0%, #e0484d 100%)',
  },
  // filter: a CSS filter list, which both hosts resolve through the same shared
  // arithmetic and then hand to a colour-matrix node or a Core Image filter.
  // Two functions, because a list is one matrix and the composition is the part
  // worth proving crosses the seam.
  filtered: {
    width: 40,
    height: 40,
    backgroundColor: '#4d8cf2',
    filter: 'grayscale(1) brightness(1.2)',
  },
  // filter: drop-shadow(), the one filter function that is a shadow rather than
  // a colour map. A sibling of the box-shadowed view below on purpose: the two
  // look alike and are different mechanisms, one following the alpha and one the
  // box, and the dump is what says which arrived.
  dropShadowed: {
    width: 40,
    height: 40,
    backgroundColor: '#4d8cf2',
    filter: 'drop-shadow(4px 6px 3px rgba(0, 0, 0, 0.5))',
  },
  // boxShadow, the other prop that only a dump can confirm. Two shadows and an
  // inset one, because the list and the inset flag are each a thing a host can
  // drop while still drawing something plausible.
  shadowed: {
    width: 40,
    height: 40,
    backgroundColor: '#ffffff',
    boxShadow: '0 4px 8px rgba(0, 0, 0, 0.25), inset 0 1px 0 #ffffff',
  },
  // outline: CSS's, which is not a border. Offset away from the edge and dashed,
  // because an offset ignored and a style dropped both still draw a ring: the
  // dump carries all four numbers so one scenario can compare them across hosts.
  outlined: {
    width: 40,
    height: 40,
    backgroundColor: '#ffffff',
    outlineWidth: 3,
    outlineOffset: 2,
    outlineColor: '#e0484d',
    outlineStyle: 'dashed',
  },
  // backgroundSize, backgroundPosition and backgroundRepeat, which apply to a
  // gradient because CSS treats one as an image. A 20pt tile stepping every 30pt
  // over a 60x40 box: the size shrinks it, the repeat tiles it, and `space`
  // would share the slack instead -- all three resolved in core/BackgroundLayers.h
  // and read back out of the dump.
  tiled: {
    width: 60,
    height: 40,
    backgroundImage: 'linear-gradient(90deg, #4d8cf2 0%, #e0484d 100%)',
    ...backgroundTiling,
  },
  // View flattening, which `collapsable` turns off. A view with nothing to draw
  // -- no background, no border, no shadow -- forms no view at all:
  // `ViewShadowNode::initialize` leaves the FormsView trait unset and the
  // differentiator never mounts it. Two sizes, so the dump can say which of the
  // two arrived: the 7x3 one should be flattened away and the 9x3 one, which
  // asks not to be, should be there.
  flattened: {
    width: 7,
    height: 3,
  },
  kept: {
    width: 9,
    height: 3,
  },
  // A radial gradient, whose ending shape is the half that is specified and
  // easy to get wrong: `circle at 30% 30%` on a 60x40 box is a circle through
  // the farthest corner from there, which is 42 x 28 away, so its radius is
  // hypot(42, 28). Both hosts resolve it through the same shared code, so the
  // two dumps agreeing is what says neither did its own arithmetic.
  radial: {
    width: 60,
    height: 40,
    backgroundImage: 'radial-gradient(circle at 30% 30%, #4d8cf2 0%, #1f2129 100%)',
  },
  // The older iOS shadow props, which no host read: every card and sheet
  // written before boxShadow existed sets these four. The radius is a CALayer
  // blur radius, so it arrives doubled as a CSS blur, and the opacity
  // multiplies the colour's alpha -- both of which the dump shows.
  legacyShadowed: {
    width: 40,
    height: 40,
    backgroundColor: '#ffffff',
    shadowColor: '#000000',
    shadowOpacity: 0.5,
    shadowOffset: {width: 2, height: 4},
    shadowRadius: 3,
  },
  // mixBlendMode on a child, which is the one view prop whose observable is what
  // it does to the pixels beneath it: multiply over the footer's background.
  // The parent carries the backdrop, so the blended box is a child of its own
  // wrapper rather than a sibling in the row.
  blendParent: {
    width: 40,
    height: 40,
    backgroundColor: '#e0484d',
  },
  blended: {
    width: 24,
    height: 24,
    marginTop: 8,
    marginLeft: 8,
    backgroundColor: '#808080',
    mixBlendMode: 'multiply',
  },
  draggable: {
    width: 40,
    height: 40,
    backgroundColor: '#4d8cf2',
    // Cast because React Native's own `CursorValue` is `'auto' | 'pointer'` and
    // nothing else, while its C++ parses all thirty-four CSS keywords and its
    // ViewProps carries them. The type is the narrow half, not the platform:
    // see backlog/upstream.md.
    cursor: 'ns-resize' as unknown as 'pointer',
  },
});

// `onLayout`, which is the one prop in `BaseViewProps` that no host reads and
// none should: ReactCommon collects the nodes whose layout changed and the
// Scheduler dispatches the event, so the whole path is upstream's. Nothing here
// had ever proved it fires, which is what this logs and the scenario asserts.
//
// The size rather than the position, because a fixed 40x40 box is the same
// number on every host where its place in a flex row is not.
function onDotLayout(event: LayoutChangeEvent) {
  const {width, height} = event.nativeEvent.layout;
  console.log(`onLayout ${width}x${height}`);
}

// The layout props, which are Yoga's and reach no host: Fabric hands Yoga the
// style, Yoga answers with a frame, and every host applies the frame. So the
// only thing that can go wrong is arrival -- a name dropped from
// `ReactNativeStyleAttributes` or the style flattener reaches nobody -- and the
// only thing that can show it is the frame itself.
//
// Each probe isolates one prop against a fixed box, and carries a `testID` so
// the scenario can find its line without depending on where in the tree it
// sits. Absolute, so none of them moves anything else on the page: this app is
// also the cross-host parity fixture, where frames are compared.
function LayoutProbes() {
  return (
    <View style={styles.probes}>
      {/* `padding`: the child starts 12 in from the parent's edge. */}
      <View testID="pad-parent" style={styles.padParent}>
        <View testID="pad-child" style={styles.probeBox} />
      </View>
      {/* `marginLeft` on the second child, which is the gap between the two. */}
      <View testID="margin-parent" style={styles.marginParent}>
        <View testID="margin-first" style={styles.probeBox} />
        <View testID="margin-second" style={styles.marginSecond} />
      </View>
      {/* `gap`, which is the same spacing asked for by the parent instead. */}
      <View testID="gap-parent" style={styles.gapParent}>
        <View testID="gap-first" style={styles.probeBox} />
        <View testID="gap-second" style={styles.probeBox} />
      </View>
      {/* `flexGrow`, which takes what is left of a fixed row. */}
      <View testID="grow-parent" style={styles.growParent}>
        <View testID="grow-fixed" style={styles.probeBox} />
        <View testID="grow-rest" style={styles.growRest} />
      </View>
      {/* `position: 'absolute'` with two insets, inside a relative parent. */}
      <View testID="inset-parent" style={styles.insetParent}>
        <View testID="inset-child" style={styles.insetChild} />
      </View>
      {/* `aspectRatio`, which decides a width from a height. */}
      <View testID="ratio" style={styles.ratio} />
    </View>
  );
}

function App() {
  return (
    <View style={styles.page}>
      <LayoutProbes />
      <View style={styles.row}>
        <View style={styles.left}>
          <View style={styles.badge} />
        </View>
        <View style={styles.right} />
      </View>
      <View style={styles.footer}>
        <View style={styles.dot} />
        <View style={styles.dashed} />
        <View style={styles.dotted} />
        <View style={styles.filtered} />
        <View style={styles.gradient} />
        <View style={styles.dropShadowed} />
        <View style={styles.shadowed} />
        <View style={styles.outlined} />
        <View style={styles.tiled} />
        <View style={styles.radial} />
        <View style={styles.legacyShadowed} />
        <View style={styles.blendParent}>
          <View style={styles.blended} />
        </View>
        <View style={styles.flattened} />
        <View collapsable={false} style={styles.kept} />
        <View style={styles.dot} onLayout={onDotLayout} />
        <View style={styles.handy} />
        <View style={styles.draggable} />
        <View style={styles.dot} />
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltViews', () => App);
