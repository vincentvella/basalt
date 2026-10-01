/**
 * Globals the bundler defines, which no package declares.
 *
 * `__DEV__` is Metro's: it substitutes a literal at build time, so nothing
 * exports it and nothing can be imported to get it. React Native's own
 * JavaScript reads it everywhere and so does this package.
 *
 * Declared here rather than taken from React Native's types because which
 * bundle of those exists depends on what kind of React Native is present --
 * `types_generated` in a published package, `ReactNativeApi.d.ts` at a release
 * tag, `types_DEPRECATED` on main -- and only one of the three declares it.
 * Relying on that is what turned a green local build into twenty errors in CI:
 * a global this code genuinely uses should not be somebody else's to provide.
 *
 * @format
 */

declare const __DEV__: boolean;
