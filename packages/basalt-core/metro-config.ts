/**
 * Metro configuration for this project's desktop platforms.
 *
 * An out-of-tree React Native platform has to do two things to Metro, and the
 * second is the one that is not obvious.
 *
 * First, the platform has to be one Metro knows about, so that
 * `Button.macos.js` wins over `Button.js` the way `Button.android.js` would.
 *
 * Second, some of React Native's own modules cannot work on a platform it has
 * never heard of. There are three kinds, and each needs different handling:
 *
 *   1. **Self-importing shims.** React Native ships a family of files whose
 *      entire body is `import X from './X'; export default X;`, each marked
 *      with a note about "backwards compatibility of subpath (deep) imports".
 *      They exist so `react-native/Libraries/Image/Image` keeps working, and
 *      they rely on a platform-specific sibling winning the resolution. Bundle
 *      for one of these platforms and each resolves to *itself*, exports
 *      undefined, and kills whatever touches it -- as `Platform.constants`
 *      being undefined, or a view config being undefined, or a component being
 *      undefined.
 *
 *   2. **Modules with no neutral fallback at all**, which do not resolve, so
 *      there is no resolution to rewrite -- only a failure to intercept.
 *
 *   3. **Modules this project genuinely implements differently**, which is
 *      `Platform` and `TextInput`.
 *
 * React Native for Windows solves all of this with a whole override system and
 * a fork of every file it replaces. This is the same idea at the smallest size
 * that works.
 *
 * None of the three kinds is Linux-specific, which is the point of this file's
 * shape: the answers are identical for linux, macos and windows, and only
 * `Platform` differs, by one string. Anything that has to be written per
 * platform is a `.linux.js` / `.macos.js` / `.windows.js` file in
 * `src/overrides`, and the tables below name it.
 *
 * Usage, in an app's metro.config.js:
 *
 *     const {withDesktopPlatforms} = require('basalt-core/metro-config');
 *     module.exports = withDesktopPlatforms(config);
 *
 * @format
 */

import * as fs from 'node:fs';
import * as path from 'node:path';

/**
 * Metro's resolver types, as far as this file uses them.
 *
 * Metro ships no usable types for `resolveRequest` and its context, and the
 * shape is large. What is described here is exactly what this file reads and
 * calls, which is the same rule the turbo modules and Metro's build API got.
 */
export type Resolution = {filePath: string; type?: string};

export type ResolutionContext = {
  resolveRequest: (
    context: ResolutionContext,
    moduleName: string,
    platform: string | null,
  ) => Resolution;
  originModulePath?: string;
  [key: string]: unknown;
};

export type ResolveRequest = (
  context: ResolutionContext,
  moduleName: string,
  platform: string | null,
) => Resolution;

/**
 * A Metro config, as far as this composes with one. Deliberately loose: an app
 * hands in whatever `getDefaultConfig` produced, and claiming to know the rest
 * of that object would be claiming to know Metro's config schema.
 */
export type MetroConfig = {
  resolver?: {
    platforms?: string[];
    nodeModulesPaths?: string[];
    resolveRequest?: ResolveRequest;
    [key: string]: unknown;
  };
  server?: {
    rewriteRequestUrl?: (url: string) => string;
    [key: string]: unknown;
  };
  projectRoot?: string;
  watchFolders?: string[];
  [key: string]: unknown;
};

export type DesktopPlatformOptions = {
  /** Which desktops to enable. Defaults to all three. */
  platforms?: string[];
  /** Which one a dev-server request with no `app=` is assumed to be. */
  devServerPlatform?: string;
  /** What to fall back to for a module with no desktop implementation. */
  platformFallbacks?: string[];
};

/**
 * One entry in an override table: the tail of a path React Native would have
 * resolved, and what to use instead -- either a fixed path or one chosen by
 * the platform being bundled for.
 */
type OverrideEntry = [string, string | ((platform: string) => string)];

const OVERRIDE_DIR = path.join(__dirname, 'src', 'overrides');

/**
 * The platforms this package can bundle for.
 *
 * `macos` and `windows` deliberately match the names react-native-macos and
 * react-native-windows use. A library that ships `Button.macos.js` for those
 * forks resolves correctly here without knowing this project exists, and a
 * library that does not is no worse off. Picking different names would have
 * bought nothing and cost that.
 */
export const DESKTOP_PLATFORMS = ['linux', 'macos', 'windows'];

/**
 * Kind 1: React Native's self-importing deep-import shims.
 *
 * Each is answered with its own `.android.js` sibling rather than a file of
 * ours. That is deliberate: these platforms report `PlatformConstantsAndroid`
 * from C++, share ReactCommon's prop parsing, and drive the same components
 * Android's JavaScript drives, so Android's implementation is the one that
 * matches what is actually implemented here. Copying them would mean nine forks
 * drifting silently from upstream.
 *
 * Found with:
 *
 *     grep -rl 'backwards compatibility of subpath (deep) imports' Libraries src
 *
 * Spelled out rather than detected, so a new shim upstream produces an honest
 * failure here rather than a silent redirect to Android.
 */
export const SELF_IMPORTING_SHIMS = [
  path.join('Libraries', 'Alert', 'RCTAlertManager.js'),
  path.join('Libraries', 'Components', 'AccessibilityInfo', 'legacySendAccessibilityEvent.js'),
  path.join('Libraries', 'Components', 'DrawerAndroid', 'DrawerLayoutAndroid.js'),
  path.join('Libraries', 'Components', 'ToastAndroid', 'ToastAndroid.js'),
  path.join('Libraries', 'Image', 'Image.js'),
  path.join('Libraries', 'Network', 'RCTNetworking.js'),
  path.join('Libraries', 'StyleSheet', 'PlatformColorValueTypes.js'),
  path.join('Libraries', 'Utilities', 'BackHandler.js'),
  // Platform.js belongs to this family too, but is answered by kind 3 below.
];

/**
 * How this package's own overrides reach React Native's internals.
 *
 * A platform implementation has to import things React Native does not export:
 * `NativeAlertManager`, `PolyfillFunctions`, the upstream `ScrollView` an
 * override wraps. Written as `react-native/Libraries/...` that costs two
 * warnings in every app that uses this package, in every bundle, for every
 * import -- one from `@react-native/babel-preset`'s warn-on-deep-imports
 * plugin, and one from Metro when the subpath is not in React Native's
 * `exports`. Neither says anything a reader can act on: the deep import is
 * deliberate, and there is no shallower path to the same file.
 *
 * So the overrides spell it this way instead, and the resolver below turns the
 * prefix into an absolute path inside the React Native package. Babel's plugin
 * looks at the literal specifier and this one does not begin with
 * `react-native/`; Metro resolves an absolute path without consulting
 * `exports`, and with its usual extension and platform machinery still
 * applying. What is imported is exactly the same file.
 *
 * It is not a way of pretending the imports are not deep. `docs/` and the
 * override headers say which internals this platform depends on; this only
 * stops the tooling from saying it once per import per bundle.
 */
const UPSTREAM_PREFIX = 'basalt-core/upstream/';

/**
 * Upstream files that are not in every supported React Native, and what to
 * use instead where they are absent.
 *
 * `supported-versions.json` lists 0.86 and 0.87, and the abort API moved
 * between them: 0.86's own `setUpXHR` polyfills `AbortController` from the
 * `abort-controller` npm package, and 0.87 brought an implementation into
 * React Native's source. An override written against the newer one therefore
 * fails to *resolve* on the older -- a bundling error, not a runtime one, so
 * no amount of try/catch in the override helps.
 *
 * Keyed by the path after the prefix, and consulted only when the upstream
 * file is really not there, so the newer layout is what is used wherever it
 * exists. The exports differ too; see src/overrides/setUpXHR.ts.
 */
const UPSTREAM_FALLBACKS: ReadonlyMap<string, string> = new Map([
  ['src/private/webapis/dom/abort-api/AbortController', 'abort-controller/dist/abort-controller'],
  ['src/private/webapis/dom/abort-api/AbortSignal', 'abort-controller/dist/abort-controller'],
]);

/**
 * Kind 3: modules this project implements itself. Checked before the shim list,
 * so `Platform` gets ours rather than Android's.
 *
 * A replacement is either a path, when every platform shares it, or a function
 * of the platform when they do not. Only `Platform` needs the second form, and
 * the one-line files it points at exist so that the difference between the
 * platforms stays exactly one string. See src/overrides/createPlatform.js.
 */
export const PLATFORM_OVERRIDES: ReadonlyArray<OverrideEntry> = [
  [
    // React Native's own file plus three props it registers and never declares:
    // `onPointerDown`, `onPointerUp` and `onPointerCancel` are in
    // `bubblingEventTypes` and parsed by ReactCommon, but missing from
    // `validAttributes` -- so React never sends them and the event is dropped
    // in C++. It was in SELF_IMPORTING_SHIMS until a right-click needed one.
    path.join('Libraries', 'NativeComponent', 'BaseViewConfig.js'),
    path.join(OVERRIDE_DIR, 'BaseViewConfig.js'),
  ],
  [
    path.join('Libraries', 'Utilities', 'Platform.js'),
    (platform: string) => path.join(OVERRIDE_DIR, `Platform.${platform}.js`),
  ],
  [
    // Not a shim: React Native's TextInput.js branches on `Platform.OS` being
    // exactly 'android' or 'ios' and renders undefined on anything else. See
    // the header of the replacement for why it is a rewrite rather than a
    // third branch.
    path.join('Libraries', 'Components', 'TextInput', 'TextInput.js'),
    path.join(OVERRIDE_DIR, 'TextInput.js'),
  ],
  [
    // Also not a shim, and the same failure: `Alert.alert()` branches on
    // `Platform.OS` being exactly 'ios' or 'android' with no else, so on a
    // desktop it returns having done nothing -- no dialog, no error. The
    // AlertManager module has been there since phase 32 and was never reached.
    path.join('Libraries', 'Alert', 'Alert.js'),
    path.join(OVERRIDE_DIR, 'Alert.js'),
  ],
  [
    // Also not a shim. React Native's `Share.share()` branches on `Platform.OS`
    // being exactly 'android' or 'ios' and rejects with "Unsupported platform"
    // otherwise -- so the native module is never reached and implementing one
    // changes nothing. See the header of the replacement.
    path.join('Libraries', 'Share', 'Share.js'),
    path.join(OVERRIDE_DIR, 'Share.js'),
  ],
  [
    // Also not a shim, and the same shape as Share: `Settings.js` branches on
    // `Platform.OS === 'ios'` and hands everything else `SettingsFallback`,
    // which is four methods that warn and answer null -- so `SettingsManager`
    // was never reached and the audit's note that it threw was wrong. See the
    // header of the replacement.
    path.join('Libraries', 'Settings', 'Settings.js'),
    path.join(OVERRIDE_DIR, 'Settings.js'),
  ],
  [
    // React Native's own file, plus the listener the host's developer menu
    // needs: `DevSettings.reload()` is the only way into ReactHost's private
    // reload, and there is no way to call it from C++. It runs from
    // InitializeCore in every development bundle, so an app that imports
    // nothing from this package still gets a working dev menu. See its header.
    path.join('Libraries', 'Core', 'setUpDeveloperTools.js'),
    path.join(OVERRIDE_DIR, 'setUpDeveloperTools.js'),
  ],
  [
    // Also not a shim: `RefreshControl.js` branches on `Platform.OS === 'ios'`
    // with an `else` that renders Android's AndroidSwipeRefreshLayout, whose
    // shadow node needs fbjni and which no desktop registers.
    path.join('Libraries', 'Components', 'RefreshControl', 'RefreshControl.js'),
    path.join(OVERRIDE_DIR, 'RefreshControl.js'),
  ],
  [
    // The one wrapper in this table rather than a rewrite. `ScrollView.render`
    // places `refreshControl` in an `if (ios) ... else if (android)` with no
    // `else`, so on a desktop the prop is dropped and the control never
    // mounts. The replacement is React Native's own ScrollView with the child
    // put back; see its header.
    path.join('Libraries', 'Components', 'ScrollView', 'ScrollView.js'),
    path.join(OVERRIDE_DIR, 'ScrollView.js'),
  ],
  [
    // Not a shim either: React Native's own View, with the keyboard props
    // react-native-macos has. An app that declares a shortcut the way that
    // platform does has them dropped in JavaScript otherwise, silently. See the
    // override's header, including why a view without them renders upstream's
    // directly rather than paying for hooks it does not use.
    path.join('Libraries', 'Components', 'View', 'View.js'),
    path.join(OVERRIDE_DIR, 'View.js'),
  ],
  [
    // Also not a shim. `fetch` is broken on this platform without it: every
    // request asks for a blob response, which ReactCxxPlatform's
    // NetworkingModule cannot produce, so the response getter throws before
    // whatwg-fetch builds a Response. The replacement is React Native's own
    // file with the blob response type routed through base64; see its header.
    path.join('Libraries', 'Core', 'setUpXHR.js'),
    path.join(OVERRIDE_DIR, 'setUpXHR.js'),
  ],
];

/**
 * Kind 2: modules React Native ships only as `.android.js` and `.ios.js`, with
 * no neutral file to resolve to. Keyed without an extension, because a request
 * that fails to resolve is only known by its extensionless path.
 */
export const MISSING_MODULES: ReadonlyArray<OverrideEntry> = [
  [
    path.join('rndevtools', 'ReactDevToolsSettingsManager'),
    path.join(OVERRIDE_DIR, 'ReactDevToolsSettingsManager.js'),
  ],
];

/**
 * Kind 4: a *library* that ships only `.ios` and `.android` files.
 *
 * `react-native-screens` has `TabsScreen.ios.tsx`, `TabsScreen.android.tsx` and
 * `TabsScreen.web.tsx`, and an `index.ts` that says `from './TabsScreen'`.
 * Resolution fails, and the whole bundle fails with it -- one component nothing
 * in the app renders takes down a build that would otherwise have worked. Every
 * library that has never heard of this platform is a candidate, which is all of
 * them, so this cannot be a list of names.
 *
 * So a resolution that fails for a desktop platform is retried as another
 * platform, in this order, and the first that resolves wins. The build gets a
 * real implementation of the module rather than failing; if that implementation
 * needs a native module this platform does not have, it fails at runtime like
 * any other unsupported library, which is a much better place to fail.
 *
 * Android first, for the same reason `SELF_IMPORTING_SHIMS` are answered with
 * their `.android.js` sibling: these platforms report `PlatformConstantsAndroid`
 * and share ReactCommon's prop parsing. One order for all three desktops rather
 * than a per-platform guess, because two desktops resolving *different*
 * implementations of the same library is the one outcome worse than either.
 *
 * Set `platformFallbacks: []` to turn this off and get the resolution error.
 */
const PLATFORM_FALLBACKS = ['android', 'ios'];

/**
 * How a host tells the dev server which desktop it is.
 *
 * ReactCxxPlatform's DevServerHelper builds its bundle URL from
 * `constexpr DEFAULT_PLATFORM = "android"`, with no hook and no setting, so
 * every desktop host asks Metro for an android bundle. Left alone, an app would
 * be `Platform.OS === 'android'` under Fast Refresh and its real value in a
 * release build, which is a far worse trap than either on its own.
 *
 * The request is therefore corrected on arrival, and the only thing in it that
 * a host controls is `app=`, which comes from `ReactInstanceConfig::appId` and
 * which Metro itself ignores. So the hosts set that to
 * `basalt-<platform>` and this reads it back.
 *
 * It is a workaround and it should not have to exist. The fix is a `platform`
 * field on ReactInstanceConfig, upstream.
 */
export const APP_ID_PREFIX = 'basalt-';

export function appIdFor(platform: string): string {
  return APP_ID_PREFIX + platform;
}

// Whether `parent` contains `child`, or is it.
//
// Not `child.startsWith(parent)`. A React Native checkout at
// `/src/react-native` is a string prefix of this package at
// `/src/basalt-core/packages/basalt-core`, so the naive test
// reports that the package is already being watched when it is not, and Metro
// then refuses to read the very files this plugin hands it. Which is exactly
// the layout this repository is developed in.
function contains(parent: string, child: string): boolean {
  const relative = path.relative(parent, child);
  return relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative));
}

// Matched on the tail of a path rather than an absolute one: the React Native
// checkout can be a node_modules copy, a sibling clone or a workspace symlink.
function matchTail(
  filePath: string,
  table: ReadonlyArray<OverrideEntry>,
  platform: string | null,
): string | null {
  for (const [tail, replacement] of table) {
    if (filePath.endsWith(tail)) {
      if (typeof replacement !== 'function') {
        return replacement;
      }
      // A replacement chosen by platform cannot be chosen without one. Metro
      // resolves with a null platform for its own internal requests, and the
      // honest answer there is "this override does not apply" rather than a
      // path with `Platform.null.js` in it.
      return platform == null ? null : replacement(platform);
    }
  }
  return null;
}

/**
 * Where React Native is, as an absolute path.
 *
 * Asked of Metro rather than of Node. `require.resolve` answers from this
 * file's own node_modules, which in this repository -- and in any monorepo or
 * linked checkout -- is not where the copy being bundled lives; Metro already
 * knows, because it has `extraNodeModules` and `nodeModulesPaths` and the
 * app's root. Resolving the package's own entry point rather than a file
 * inside it also keeps this out of the `exports` check, which is half of what
 * UPSTREAM_PREFIX exists to avoid.
 */
let reactNativeRootCache: string | null = null;
function reactNativeRoot(resolve: () => Resolution): string {
  if (reactNativeRootCache == null) {
    reactNativeRootCache = path.dirname(resolve().filePath);
  }
  return reactNativeRootCache;
}

function replacementFor(filePath: string, platform: string | null): string | null {
  const own = matchTail(filePath, PLATFORM_OVERRIDES, platform);
  if (own != null) {
    return own;
  }
  for (const shim of SELF_IMPORTING_SHIMS) {
    if (filePath.endsWith(shim)) {
      // The sibling beside the shim, wherever that directory happens to be.
      return filePath.replace(/\.js$/, '.android.js');
    }
  }
  return null;
}

/**
 * Rewrites a dev-server bundle request that says `android` to the desktop
 * platform that actually asked for it. See APP_ID_PREFIX above.
 *
 * `fallback` is used when the request carries no `app=` this plugin recognises,
 * which is what an older host or a hand-typed URL looks like.
 */
function correctBundlePlatform(
  url: string,
  platforms: ReadonlyArray<string>,
  fallback: string,
): string {
  if (!/\.(bundle|map)\b/.test(url)) {
    return url;
  }
  if (!/([?&]platform=)android(&|$)/.test(url)) {
    return url;
  }

  const app = /[?&]app=([^&]*)/.exec(url);
  let target = fallback;
  if (app != null && app[1].startsWith(APP_ID_PREFIX)) {
    const named = app[1].slice(APP_ID_PREFIX.length);
    if (platforms.includes(named)) {
      target = named;
    }
  }
  if (target == null) {
    return url;
  }
  return url.replace(/([?&]platform=)android(&|$)/, `$1${target}$2`);
}

/**
 * Adds this project's desktop platforms to Metro and installs the overrides.
 *
 * Composes with an existing `resolveRequest` and `rewriteRequestUrl` rather
 * than replacing them, so an app that already has either keeps it.
 *
 * Options:
 *
 *   platforms          which of linux/macos/windows to enable. All of them by
 *                      default: bundling is per-platform anyway, so there is
 *                      nothing to be gained by making an app declare a subset.
 *   devServerPlatform  what to assume a dev-server request is for when it does
 *                      not say. Defaults to the first enabled platform. A host
 *                      built from this repo always says, so this only matters
 *                      for a URL typed by hand.
 *   platformFallbacks  which platforms to retry a failed resolution as, in
 *                      order. See PLATFORM_FALLBACKS. `[]` disables it.
 */
/** This package, by the name an app imports it as. */
const PACKAGE_NAME = 'basalt-core';

/**
 * The capability packages, which an app installs only if it wants them.
 *
 * Named rather than discovered: a list is something a person can read, and the
 * alternative -- scanning the app's dependencies for a prefix -- would also pick
 * up the host packages, which are not imported from JavaScript at all.
 */
const CAPABILITY_PACKAGES = ['basalt-subprocess'];

/** Our own package a request names, or null. Matches `name` and `name/subpath`. */
function ownedPackage(request: string): string | null {
  for (const name of [PACKAGE_NAME, ...CAPABILITY_PACKAGES]) {
    if (request === name || request.startsWith(`${name}/`)) {
      return name;
    }
  }
  return null;
}

export function withDesktopPlatforms(
  config: MetroConfig = {},
  options: DesktopPlatformOptions = {},
): MetroConfig {
  const enabled = options.platforms ?? DESKTOP_PLATFORMS;
  const devServerPlatform = options.devServerPlatform ?? enabled[0];
  const fallbacks = options.platformFallbacks ?? PLATFORM_FALLBACKS;
  // One line per module, not per import of it: a library resolved through the
  // fallback is usually imported from a dozen places.
  const reported = new Set();

  const resolver = config.resolver ?? {};
  const existingResolveRequest = resolver.resolveRequest;
  const server = config.server ?? {};
  const existingRewrite = server.rewriteRequestUrl;

  const platforms = resolver.platforms ?? [];
  const withDesktop = [
    ...enabled.filter(name => !platforms.includes(name)),
    ...platforms,
  ];

  // Metro will not read a file it is not watching, and this package hands it
  // files -- the overrides above. Inside this repo that is already true, since
  // `packages/` is on watchFolders; for anyone consuming the platform from a
  // linked checkout or another monorepo it is not, and bundling fails with
  // "Failed to get the SHA-1 for" the override rather than anything that names
  // the cause. Found by bundling a real app from another tree.
  const watchFolders = config.watchFolders ?? [];
  const withOverrides = watchFolders.some(folder => contains(folder, __dirname))
    ? [...watchFolders]
    : [...watchFolders, __dirname];

  // Metro resolves this package's files by their real path and then looks for
  // their dependencies by walking up from there. Installed normally that lands
  // in the app's node_modules and everything is found. Linked from a checkout,
  // or in a monorepo, it lands in this repository instead, where the app's
  // dependencies are not -- and the failure names @babel/runtime, a helper this
  // package's own compiled output needs, rather than naming the link.
  //
  // Naming the project's node_modules explicitly covers both.
  //
  // Every one from the project root upwards, not only the project's own, which
  // is what Node's own resolution walks. A workspace hoists its dependencies to
  // the repository root, so an app at `apps/desktop` has this package two
  // directories above its own node_modules -- and Metro, unlike Node, only
  // looks where it is told. The symptom is a module that resolves from
  // `src/App.tsx` and not from `modules/something/index.ts`, because the
  // relative walk from the deeper file runs out first. Found in kino, whose
  // local Expo modules sit one directory deeper than its source.
  const projectRoot = config.projectRoot ?? process.cwd();
  const nodeModulesPaths = resolver.nodeModulesPaths ?? [];
  const withProject = [...nodeModulesPaths];
  for (let directory = projectRoot; ; ) {
    const candidate = path.join(directory, 'node_modules');
    // Only ones that exist: Metro tolerates the rest, and a list of every
    // directory up to `/` is harder to read when something does go wrong.
    if (!withProject.includes(candidate) && fs.existsSync(candidate)) {
      withProject.push(candidate);
    }
    const parent = path.dirname(directory);
    if (parent === directory) {
      break;
    }
    directory = parent;
  }

  // And a direct answer for this package and its capability packages, as a
  // fallback rather than an override.
  //
  // `nodeModulesPaths` above is the general fix and it is not enough on its own:
  // Metro walks up from the *importing file*, and a file nested deeper than the
  // app's source -- `modules/something/index.ts` next to `src/` -- runs out of
  // parents before it reaches a workspace root. `extraNodeModules` is consulted
  // only when the ordinary walk has already failed, so naming these cannot
  // shadow an app's own copy of anything; it rescues the case where there was no
  // answer at all.
  //
  // Resolved from the project root with Node's own algorithm, which follows the
  // symlink a linked checkout leaves and finds nothing when the package is not
  // installed -- in which case it is not named, and the failure stays the one
  // about a missing dependency.
  const linked: Record<string, string> = {};
  for (const name of [PACKAGE_NAME, ...CAPABILITY_PACKAGES]) {
    try {
      const manifest = require.resolve(`${name}/package.json`, {paths: [projectRoot]});
      linked[name] = path.dirname(manifest);
    } catch {
      // Not installed. A capability package usually is not.
    }
  }
  const extraNodeModules = {...linked, ...(resolver.extraNodeModules ?? {})};

  // And watched, for the same reason this package's own directory is: Metro
  // refuses to read a file outside `projectRoot` and `watchFolders`, with
  // "Failed to get the SHA-1 for" the file rather than anything naming the
  // cause. A capability package installed normally is inside the app's
  // node_modules and already covered; one resolved through a link is not.
  for (const directory of Object.values(linked)) {
    if (!withOverrides.some(folder => contains(folder, directory))) {
      withOverrides.push(directory);
    }
  }

  return {
    ...config,
    watchFolders: withOverrides,
    server: {
      ...server,
      rewriteRequestUrl: url =>
        correctBundlePlatform(
          existingRewrite ? existingRewrite(url) : url,
          enabled,
          devServerPlatform,
        ),
    },
    resolver: {
      ...resolver,
      platforms: withDesktop,
      nodeModulesPaths: withProject,
      extraNodeModules,
      resolveRequest: (
        context: ResolutionContext,
        moduleName: string,
        platform: string | null,
      ): Resolution => {
        // Metro resolves some of its own requests with no platform at all,
        // and none of those are ours.
        const ours = platform != null && enabled.includes(platform);

        // This package's own overrides reaching React Native's internals. See
        // UPSTREAM_PREFIX: the specifier becomes an absolute path so that
        // neither Babel's deep-import plugin nor Metro's package-exports check
        // has anything to warn about, and resolution is otherwise Metro's.
        // Resolve first, then decide. Rewriting the request instead would mean
        // reimplementing Metro's resolution to know what './Platform' meant
        // from any given file.
        const resolveName = (name: string, target: string | null): Resolution =>
          existingResolveRequest
            ? existingResolveRequest(context, name, target)
            : context.resolveRequest(context, name, target);

        // This package's own overrides reaching React Native's internals. See
        // UPSTREAM_PREFIX: the specifier becomes an absolute path so that
        // neither Babel's deep-import plugin nor Metro's package-exports check
        // has anything to warn about, and resolution is otherwise Metro's.
        let request = moduleName;
        if (request.startsWith(UPSTREAM_PREFIX)) {
          const root = reactNativeRoot(() => resolveName('react-native', platform));
          const within = request.slice(UPSTREAM_PREFIX.length);
          const absolute = path.join(root, within);
          const fallback = UPSTREAM_FALLBACKS.get(within);
          // Only when the file is genuinely absent: a supported React Native
          // that has it gets it. `.js` because that is what React Native's
          // source is; a directory would have an index.js inside it.
          request =
            fallback != null && !fs.existsSync(`${absolute}.js`) && !fs.existsSync(absolute)
              ? fallback
              : absolute;
        }

        // This package and its capability packages, answered here rather than
        // left to whatever resolver the app has.
        //
        // `nodeModulesPaths` and `extraNodeModules` above are the polite way to
        // say where these are, and an app is free to have a resolver that does
        // not read either -- @rnx-kit's does not, and kino uses it. The result
        // was an import of `basalt-core` from a file one directory
        // deeper than the app's source resolving nowhere, with a message about
        // node_modules directories that named neither this package nor the link
        // it was installed through.
        //
        // Answered by Node's own algorithm from the project root, which follows
        // a linked checkout's symlink. Only for *our* names, and only when Node
        // finds them: anything else, including an app that shadows one on
        // purpose, falls through to the resolution it would have had.
        const owned = ownedPackage(request);
        if (owned != null) {
          try {
            return {
              type: 'sourceFile',
              filePath: require.resolve(request, {paths: [projectRoot]}),
            };
          } catch {
            // Not installed, or a subpath its exports map does not offer. The
            // app's resolver gets to produce the error, which will name the
            // import rather than this.
          }
        }

        const resolveAs = (target: string | null): Resolution => resolveName(request, target);

        let resolution;
        try {
          resolution = resolveAs(platform);
        } catch (error) {
          if (!ours) {
            throw error;
          }

          if (moduleName.startsWith('.')) {
            const requested = path.resolve(
              path.dirname(context.originModulePath ?? ''),
              moduleName,
            );
            const forMissing = matchTail(requested, MISSING_MODULES, platform);
            if (forMissing != null) {
              return {type: 'sourceFile', filePath: forMissing};
            }
          }

          // Kind 4. The original error is what gets thrown if every fallback
          // fails too: it names the platform the app actually asked for.
          resolution = null;
          for (const fallback of fallbacks) {
            try {
              resolution = resolveAs(fallback);
            } catch {
              continue;
            }
            const key = `${moduleName}\u0000${context.originModulePath ?? ''}`;
            if (!reported.has(key)) {
              reported.add(key);
              console.warn(
                `basalt: '${moduleName}' has no ${platform} implementation; ` +
                  `using its ${fallback} one (from ${context.originModulePath ?? '?'})`,
              );
            }
            break;
          }
          if (resolution == null) {
            throw error;
          }
        }

        if (!ours || resolution?.type !== 'sourceFile') {
          return resolution;
        }

        const replacement = replacementFor(resolution.filePath, platform);
        if (replacement == null || replacement === resolution.filePath) {
          return resolution;
        }

        // An override importing the thing it overrides would loop forever.
        if (path.resolve(context.originModulePath ?? '') === replacement) {
          return resolution;
        }

        return {type: 'sourceFile', filePath: replacement};
      },
    },
  };
}

/**
 * The original name, kept working. Enables only `linux`, which is what it
 * always did, so an app that has this in its metro.config.js keeps the exact
 * behaviour it had.
 */
export function withLinuxPlatform(config: MetroConfig = {}): MetroConfig {
  return withDesktopPlatforms(config, {platforms: ['linux']});
}

