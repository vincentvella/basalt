/**
 * `ScrollView`, for a desktop React Native platform.
 *
 * Unlike the other four overrides beside this one, this is not a rewrite: it is
 * React Native's own `ScrollView`, with the one line that drops `refreshControl`
 * on the floor put back.
 *
 * `ScrollView.render` handles the prop in two branches -- iOS puts the control
 * inside the scroll view, Android wraps the scroll view in one -- and has no
 * `else`. Bundle for `linux`, `macos` or `windows` and neither branch is taken,
 * so `<ScrollView refreshControl={...}>` renders the list and silently no
 * refresh control: no warning, no error, nothing on screen, and a
 * `<RefreshControl>` that is never mounted and so can never fire `onRefresh`.
 *
 * A wrapper rather than a copy. `ScrollView.js` is two thousand lines and every
 * one of them would have to be re-copied at each React Native upgrade; the part
 * that is wrong here is a single decision about where one child goes, and the
 * iOS arrangement -- the control as a child of the scroll view -- is the one
 * this platform implements. So the control is passed through as an ordinary
 * first child and everything else is React Native's.
 *
 * Importing the module this file replaces resolves to the original rather than
 * looping: a `basalt-core/upstream/...` specifier means React Native's own file
 * and is never answered with an override. See docs/DECISIONS.md for what that
 * rule used to be and what it cost.
 *
 * ## `pagingEnabled`, which never reached a desktop at all
 *
 * The second thing this wrapper puts back, and a different failure from the
 * first: `ScrollView.render` computes the native `pagingEnabled` prop with
 * `Platform.select({ios, android})` and no `default`, so on `linux`, `macos`
 * or `windows` it is `undefined`, `BaseScrollViewProps` keeps its own `false`,
 * and a paged list simply scrolls freely. All three hosts implement paging --
 * `core/ScrollSnap.h` has it, and reads `props->pagingEnabled` to turn it on --
 * so the only thing missing was the prop.
 *
 * It is translated rather than forced, because `ScrollView.js` is two thousand
 * lines and the value is computed inside its render: a paged list is given
 * `snapToInterval` of its own viewport and `disableIntervalMomentum`, which is
 * what `core/ScrollSnap.h` says paging *is* -- snap points a screenful apart,
 * and one page per flick however hard it was thrown. Only when the app set
 * neither `snapToInterval` nor `snapToOffsets`, which is the same precedence
 * upstream's iOS branch applies.
 *
 * The viewport comes from `onLayout`, so a paged list snaps from its second
 * frame rather than its first, and follows a window that is resized. The app's
 * own `onLayout` still fires.
 *
 * @format
 */

'use strict';

import ScrollView from 'basalt-core/upstream/Libraries/Components/ScrollView/ScrollView';
import * as React from 'react';
import {useCallback, useState} from 'react';

type LayoutEvent = {nativeEvent: {layout: {width: number; height: number}}};

type BasaltScrollViewProps = {
  refreshControl?: React.ReactElement | null;
  children?: React.ReactNode;
  // React Native's ScrollView props, which it exports no usable type for. This
  // component reads four and forwards the rest untouched.
  [key: string]: unknown;
};

/** The children in the order this platform mounts them: see the header. */
function withRefreshControl(
  refreshControl: React.ReactElement | null | undefined,
  children: React.ReactNode,
): React.ReactNode {
  // First, so it takes the row at the top of the content: on a phone the
  // spinner lives in the rubber band above the list, and a desktop scroll view
  // has none. See the header of RefreshControl.js beside this file.
  return refreshControl == null ? children : [refreshControl, children];
}

/** Whether this list asked for paging and nothing more specific. */
function wantsPaging(props: Record<string, unknown>): boolean {
  return (
    props.pagingEnabled === true &&
    props.snapToInterval == null &&
    props.snapToOffsets == null
  );
}

/**
 * A paged list, which has to measure itself: the page is the viewport, and
 * nothing knows how big that is until it has been laid out.
 *
 * A second component rather than a branch, for the reason View.tsx beside this
 * file has two: hooks cannot be conditional, and a scroll view that re-renders
 * on every layout is not a cost to impose on the lists that are not paged.
 */
function PagedScrollView({
  refreshControl,
  children,
  ...props
}: BasaltScrollViewProps): React.ReactElement {
  const [viewport, setViewport] = useState(0);
  const appOnLayout = props.onLayout as ((event: LayoutEvent) => void) | undefined;
  const horizontal = props.horizontal === true;

  const onLayout = useCallback(
    (event: LayoutEvent) => {
      const {width, height} = event.nativeEvent.layout;
      setViewport(horizontal ? width : height);
      appOnLayout?.(event);
    },
    [appOnLayout, horizontal],
  );

  return (
    <ScrollView
      {...props}
      onLayout={onLayout}
      // Zero until the first layout, which `ScrollSnapConfig` reads as "unused"
      // -- so a paged list snaps from its second frame. A page cannot be known
      // before the viewport is.
      snapToInterval={viewport}
      disableIntervalMomentum={true}>
      {withRefreshControl(refreshControl, children)}
    </ScrollView>
  );
}

function BasaltScrollView({
  refreshControl,
  children,
  ...props
}: BasaltScrollViewProps): React.ReactElement {
  if (wantsPaging(props)) {
    return (
      <PagedScrollView refreshControl={refreshControl} {...props}>
        {children}
      </PagedScrollView>
    );
  }
  return (
    <ScrollView {...props}>{withRefreshControl(refreshControl, children)}</ScrollView>
  );
}

BasaltScrollView.displayName = 'ScrollView';

// `ScrollView.Context` is read by VirtualizedList and by `ScrollView`'s own
// children, and the statics are part of the public export. Copied rather than
// re-declared so that anything React Native adds to them arrives here too.
// Indexed by a string on both sides: these are React Native's statics, whose
// names are not known here and whose shapes it publishes no types for.
const statics = BasaltScrollView as unknown as Record<string, unknown>;
const upstreamStatics = ScrollView as unknown as Record<string, unknown>;
for (const key of Object.keys(upstreamStatics)) {
  if (statics[key] === undefined) {
    statics[key] = upstreamStatics[key];
  }
}
statics.Context = upstreamStatics.Context;

export default BasaltScrollView;
