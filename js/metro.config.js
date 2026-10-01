// Metro configuration for a project with no node_modules of its own.
//
// This directory is not an npm package in the usual sense: nothing is
// installed into it. `react`, `react-native`, Metro and the Babel preset all
// resolve out of the React Native checkout that scripts/bootstrap.sh prepared,
// which keeps the demo in step with the exact source tree the C++ side is built
// against. RN_DIR points at it, the same variable CMake takes.
//
// Three settings do the work:
//   watchFolders        lets Metro read files outside this directory at all.
//   nodeModulesPaths    is where `require('react-native')` is looked up.
//   extraNodeModules    pins react to one copy, so hooks do not blow up.

const path = require('path');
const fs = require('fs');
const {createRequire} = require('module');

const rnDir = path.resolve(
  process.env.RN_DIR ?? path.resolve(__dirname, '..', '..', 'react-native'),
);

if (!fs.existsSync(path.join(rnDir, 'node_modules', 'react-native'))) {
  throw new Error(
    `No React Native checkout with installed dependencies at ${rnDir}.\n` +
      'Point RN_DIR at one, and run scripts/bootstrap.sh if its node_modules is empty.',
  );
}

// Resolve as package specifiers rather than by path. These are workspace
// packages whose entry point is declared only in "exports", and Node ignores
// "exports" when you require an absolute directory path.
const rnRequire = createRequire(path.join(rnDir, 'package.json'));
const {getDefaultConfig, mergeConfig} = rnRequire('@react-native/metro-config');

// The desktop platforms themselves: adds linux, macos and windows to Metro's
// platform list and puts this project's replacements in front of the React
// Native modules that have no variant for them. Without it, bundling with
// --platform linux produces a bundle that builds and then dies on
// Platform.constants being undefined.
// The built package: basalt-core is TypeScript, and requiring the
// source here does not fail cleanly -- Node 24 loads the .ts sibling and
// reports `Unexpected token 'export'`, which names neither the file nor the
// reason. Run scripts/build_ts.sh; scripts/bundle.sh does it for you.
const {withDesktopPlatforms} = require('../packages/basalt-core/dist/metro-config');

// See extraNodeModules below. Named individually so that nothing else of the
// app's is in reach, and quietly empty when BASALT_EXPO_APP is unset.
const expoApp = process.env.BASALT_EXPO_APP;
const expoModules = {};
if (expoApp != null && expoApp !== '') {
  // @shopify/react-native-skia alongside them. The variable is named for Expo
  // and the mechanism is not: it borrows named packages from a real app, which is
  // the only way this directory -- which installs nothing -- can bundle something
  // that imports one. Skia is borrowed rather than vendored for the same reason
  // its C++ is; see cmake/Skia.cmake.
  for (const name of ['expo', 'expo-modules-core', 'expo-notifications',
                      '@shopify/react-native-skia']) {
    const candidate = path.join(expoApp, 'node_modules', name);
    if (fs.existsSync(candidate)) {
      expoModules[name] = candidate;
    }
  }
}

// One React and one React Native, for every module including a borrowed one.
//
// `extraNodeModules` below is not enough on its own: it is a fallback, consulted
// only when ordinary resolution fails. A package borrowed out of somebody else's
// app has that app's `node_modules` above it, so its own `require('react')`
// succeeds there and never reaches the fallback -- and the app ends up with two
// Reacts. The symptom is not a resolution error but
// `Cannot read property 'useRef' of null` from inside the borrowed package,
// which reads as a bug in that package.
//
// Measured: bundling @shopify/react-native-skia out of an app failed exactly
// that way until this existed.
const pinned = {
  react: path.join(rnDir, 'node_modules', 'react'),
  'react-native': path.join(rnDir, 'node_modules', 'react-native'),
};

function resolvePinned(context, moduleName, platform) {
  for (const [name, dir] of Object.entries(pinned)) {
    if (moduleName === name || moduleName.startsWith(`${name}/`)) {
      // Resolved as though the request came from that copy, so its own
      // subpath exports and conditions apply rather than the borrower's.
      return context.resolveRequest(
        {...context, originModulePath: path.join(dir, 'package.json')},
        moduleName,
        platform,
      );
    }
  }
  return context.resolveRequest(context, moduleName, platform);
}

module.exports = withDesktopPlatforms(mergeConfig(getDefaultConfig(__dirname), {
  projectRoot: __dirname,
  // The Expo app's tree as well when one was named: Metro refuses to read a
  // file outside the project root and its watch folders, so naming the package
  // in extraNodeModules is only half of borrowing it.
  watchFolders: [
    rnDir,
    path.resolve(__dirname, '..', 'packages'),
    ...(expoApp != null && expoApp !== '' ? [expoApp] : []),
  ],
  watcher: {
    // Metro checking its own watcher: it writes a probe file periodically and
    // warns if the change never comes back. Worth having on for a project whose
    // whole development loop is Fast Refresh -- a watcher that stops delivering
    // is otherwise completely silent, and looks like "my edit did nothing".
    healthCheck: {enabled: true},
  },
  resolver: {
    // See resolvePinned: one React for every module, borrowed or not.
    resolveRequest: resolvePinned,
    nodeModulesPaths: [path.join(rnDir, 'node_modules')],
    // Without this, two copies of React can end up in the graph -- one
    // resolved from here and one from a transitive dependency inside the
    // checkout -- and hooks fail at runtime with the usual invalid-hook-call.
    extraNodeModules: {
      react: path.join(rnDir, 'node_modules', 'react'),
      'react-native': path.join(rnDir, 'node_modules', 'react-native'),
      'basalt-core': path.resolve(__dirname, '..', 'packages', 'basalt-core'),
      // The capability packages with JavaScript of their own, so a demo app can
      // import one the way an app that installed it would.
      'basalt-subprocess': path.resolve(
        __dirname, '..', 'packages', 'basalt-subprocess'),
      // Expo, borrowed from an app that has it installed.
      //
      // This directory is not an npm package and nothing is installed into it,
      // so an app here that imports `expo-notifications` has nothing to resolve
      // against. BASALT_EXPO_APP names a real Expo app -- the same one the host
      // was built against with -DBASALT_EXPO_MODULES_CORE -- and its packages
      // are borrowed by name rather than by adding its node_modules to the
      // search path, which would also hand over its React and its React Native.
      //
      // Unset is the normal case and changes nothing: js/notifications.js is
      // the only app that needs it, and it is skipped without it.
      ...expoModules,
    },
  },
}));
