/**
 * What React Native's CLI reads out of this package.
 *
 * An entry per platform, without which the CLI does not believe these are
 * platforms and `react-native bundle --platform macos` is rejected before Metro
 * sees it.
 *
 * Commands are not here. `run-linux` needs a GTK host to run, so it belongs to
 * basalt-gtk -- the CLI reads a react-native.config.js out of every
 * dependency, so an app that has only the AppKit package never sees a command it
 * could not have used.
 *
 * `projectConfig` returning null for a project with no native directory is how
 * the CLI is told "this project does not target that platform", and is what
 * keeps the command out of the way in an app that only builds for iOS. Today
 * every project targets all three, because there is no per-project native code
 * to look for yet; when there is, this is where the check goes.
 *
 * @format
 */

// Checked, not compiled.
//
// This file stays JavaScript on purpose, and it is the only one in the package
// that does. React Native's CLI reads it from `node_modules/<package>/` by
// path convention rather than through `exports`, so it has to be loadable as
// CommonJS at exactly this path -- and what it exports is a data structure,
// which is the one shape types buy nothing to write and everything to check.
// `@ts-check` gives the checking without putting a tool this project does not
// control behind a build step.
// @ts-check
'use strict';

/** @typedef {{projectConfig: () => object, dependencyConfig: () => null}} PlatformEntry */

// `./dist/metro-config`, not `./metro-config`: that file is TypeScript now and
// this one is not compiled, so the sibling it used to sit beside is no longer
// there. This is the file React Native's CLI loads from the package root, so
// it must resolve with nothing but Node.
// Required from `dist` at runtime, typed from the source.
//
// They are the same module. Splitting them this way is what stops checking
// this file from depending on the output of the very compilation that checks
// it -- `./dist/metro-config.d.ts` does not exist yet on a clean build, and a
// config file that only type-checks on the second run is a trap.
//
// The path is built rather than written so that TypeScript does not try to
// resolve it; the cast beside it says what it is.
const metroConfig = /** @type {typeof import('./metro-config')} */ (
  require(`${__dirname}/dist/metro-config`)
);
const DESKTOP_PLATFORMS = metroConfig.DESKTOP_PLATFORMS;

// The same entry for each. They differ in their view layer and in nothing the
// CLI can see, which is the whole argument of this project restated as four
// lines of configuration.
//
// Deliberately no `npmPackageName`, which is the one field that looks obviously
// right and is wrong here.
//
// Setting it tells the CLI this is an *out-of-tree platform* in the
// react-native-macos and react-native-windows sense: a fork. It then installs
// `reactNativePlatformResolver`, which rewrites every `react-native/...` import
// to `basalt-core/...` when bundling, and requires this package to
// export `./setup-env`. Both are correct for a fork that vendors React Native's
// JavaScript. This package vendors none of it -- it redirects nine of React
// Native's own shims to their existing `.android.js` siblings and overrides two
// files -- so the rewrite sends every import into a package that does not
// contain it.
//
// Without the field these are still platforms the CLI accepts, and resolution
// stays with `withDesktopPlatforms` in metro-config.js, which is where the
// actual policy lives.
/** @type {Record<string, PlatformEntry>} */
const platforms = {};
for (const name of DESKTOP_PLATFORMS) {
  platforms[name] = {
    projectConfig: () => ({}),
    // No autolinking: nothing here has native code to link yet, and claiming
    // otherwise would make the CLI generate references to files that do not
    // exist.
    dependencyConfig: () => null,
  };
}

module.exports = {platforms};
