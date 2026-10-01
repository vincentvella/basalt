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
 * looping: `metro-config.js` skips the override when the request comes from the
 * override itself. That guard was written for exactly this case.
 *
 * @format
 */

'use strict';

import ScrollView from 'basalt-core/upstream/Libraries/Components/ScrollView/ScrollView';
import * as React from 'react';

type BasaltScrollViewProps = {
  refreshControl?: React.ReactElement | null;
  children?: React.ReactNode;
  // React Native's ScrollView props, which it exports no usable type for. This
  // component reads two and forwards the rest untouched.
  [key: string]: unknown;
};

function BasaltScrollView({
  refreshControl,
  children,
  ...props
}: BasaltScrollViewProps): React.ReactElement {
  if (refreshControl == null) {
    return <ScrollView {...props}>{children}</ScrollView>;
  }
  // First, so it takes the row at the top of the content: on a phone the
  // spinner lives in the rubber band above the list, and a desktop scroll view
  // has none. See the header of RefreshControl.js beside this file.
  return (
    <ScrollView {...props}>
      {refreshControl}
      {children}
    </ScrollView>
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
