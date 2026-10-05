/**
 * The one thing this package needs from react-native-screens, declared.
 *
 * It is a peer dependency, and this repository deliberately has no
 * node_modules of its own, so there is nothing here for TypeScript to read the
 * real types out of. Declaring the single export used is better than depending
 * on the library at build time for a type: it keeps the repository's "no
 * installed dependencies" property, and it fails honestly if the export is
 * ever renamed, because the import stops resolving at runtime in a test rather
 * than compiling against a type nobody checked.
 *
 * `ScreenContext` holds the component the library renders for each screen.
 * Its real value type is the library's own `Screen`, which this package
 * replaces; typing it as a component of unknown props is as much as can be
 * said from outside, and is why the provider casts.
 *
 * @format
 */

declare module 'react-native-screens' {
  import type * as React from 'react';

  export const ScreenContext: React.Context<React.ComponentType<never>>;
}
