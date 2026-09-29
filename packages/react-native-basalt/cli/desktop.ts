/**
 * `react-native run-linux`, `run-macos` and `run-windows`, which are one
 * command with three names.
 *
 * Everything a desktop run does is the same on all three: find or build a host
 * binary, work out the module name, make sure a packager is running, write a
 * bundle, launch the host in the foreground and hand back its exit status. What
 * differs is four strings -- the platform, the command name, the binary, and
 * where its native sources are -- so that is what a platform package passes in.
 *
 * The same division as the C++ half, for the same reason. Three copies of this
 * file would be three places for `--mode release` to mean something slightly
 * different, and the first anyone would hear of it is an app that bundles
 * without assets on one desktop.
 *
 * @format
 */


import type {ChildProcess} from 'node:child_process';
import {spawn, spawnSync} from 'node:child_process';
import * as fs from 'node:fs';
import * as path from 'node:path';

import {bundleWithAssets, writeExpoAppConfig} from './bundleWithAssets';
import {packageApp} from './packageApp';
import type {PackagePlatform} from './packageApp';
import {isPortTaken, startMetro} from './metro';

const DEFAULT_PORT = 8081;

/**
 * A target is the whole of what a platform package contributes.
 *
 *   platform     what Metro bundles for, and what Platform.OS will say
 *   command      the CLI command name
 *   binary       the host executable, with the extension the platform uses
 *   nativeDir    that package's native/ directory, for --build
 *   coreDir      react-native-basalt/native, where bootstrap.sh lives
 *   toolchain    one sentence naming what --build needs installed
 *   generator    optional CMake generator, when the default is wrong
 */

/**
 * What a platform package contributes. The fields are documented above; this
 * is the same list, so that a package leaving one out is a compile error
 * rather than an undefined read a long way from here.
 */
export type Target = {
  platform: PackagePlatform;
  command: string;
  binary: string;
  nativeDir: string;
  coreDir: string;
  toolchain: string;
  label?: string;
  generator?: string;
  /** Extra CMake arguments this platform always needs. */
  configureArgs?: string[];
};

/** What React Native's CLI hands a command. Only the fields this file reads. */
/**
 * Where React Native's C++ is, and how to ask bootstrap for it. A checkout and
 * an installed package are different layouts, and the difference is what
 * bootstrap has to be told.
 */
export type ReactNativeSources = {
  layout: 'checkout' | 'installed';
  bootstrapArg: string;
  rnDir: string;
};

/**
 * CMake arguments, and the one-line notes the run prints to say why each is
 * there -- "capability package, from ...". The notes exist so that a build
 * picking something up from the app's dependencies says so rather than doing
 * it silently.
 */
export type ContributedArgs = {
  args: string[];
  notes: string[];
};

export type CliContext = {
  root: string;
  reactNativePath: string;
};

/** The flags a run command accepts. */
export type RunOptions = {
  mode?: string;
  build?: boolean;
  packager?: boolean;
  port?: number;
  entryFile?: string;
  /** `--module`: the name the app registered with AppRegistry. */
  module?: string;
  /** `--host-binary`: a host to run instead of the one that would be found. */
  hostBinary?: string;
  /** `--bundle`: where the bundle is, or should be written. */
  bundle?: string;
  /** `--jobs`: parallelism for the build. */
  jobs?: number | string;
  generator?: string;
};

export class MissingHost extends Error {}

/**
 * Executable, on a platform that has an opinion about it.
 *
 * `X_OK` is meaningless on Windows -- every readable file answers yes -- so the
 * question there is only whether it is a file. Asking the same question of a
 * Unix binary would call a non-executable one runnable, which is worse than
 * either mistake this avoids.
 */
export function isExecutable(file: string): boolean {
  try {
    if (!fs.statSync(file).isFile()) {
      return false;
    }
    if (process.platform !== 'win32') {
      fs.accessSync(file, fs.constants.X_OK);
    }
    return true;
  } catch {
    return false;
  }
}

/**
 * Where to look for the host, in order of how explicit the answer is.
 *
 * The last entry is the development checkout: when the platform package is
 * being used from inside its own repository, the build tree is four
 * directories up. That is what makes the demo app in examples/ work without
 * configuration.
 */
export function candidates(projectRoot: string, options: RunOptions, target: Target): string[] {
  const found: string[] = [];

  if (options.hostBinary) {
    found.push(path.resolve(projectRoot, options.hostBinary));
  }
  if (process.env.BASALT_HOST) {
    found.push(path.resolve(process.env.BASALT_HOST));
  }
  // Where `--build` puts one, per target; see buildHost.
  found.push(path.join(projectRoot, '.basalt', 'build', target.platform, target.binary));
  // And where it used to, before the directory was split by target. Kept so
  // that a checkout built before that change still runs without rebuilding.
  found.push(path.join(projectRoot, '.basalt', 'build', target.binary));
  found.push(path.join(projectRoot, target.platform, 'build', target.binary));
  found.push(path.join(projectRoot, 'build', target.binary));
  found.push(path.join(target.nativeDir, '..', '..', '..', 'build', target.binary));

  return found.map(entry => path.normalize(entry));
}

export function resolveHost(projectRoot: string, options: RunOptions, target: Target): string {
  const looked = candidates(projectRoot, options, target);
  const found = looked.find(isExecutable);
  if (found) {
    return found;
  }

  throw new MissingHost(
    `could not find ${target.binary}.\n\n` +
      'Looked in:\n' +
      looked.map(entry => `  ${entry}`).join('\n') +
      '\n\n' +
      'Build one with:\n\n' +
      `  react-native ${target.command} --build\n\n` +
      `That needs cmake, ninja, and ${target.toolchain}. ` +
      'Or build it\nyourself and point at it with --host-binary <path> or BASALT_HOST.',
  );
}

function run(
  command: string,
  args: string[],
  options: {cwd?: string; env?: NodeJS.ProcessEnv},
  toolchain: string,
): void {
  const result = spawnSync(command, args, {stdio: 'inherit', ...options});
  if (result.error) {
    if ((result.error as NodeJS.ErrnoException).code === 'ENOENT') {
      throw new Error(
        `${command} is not installed. Building the host needs cmake, ninja, and ${toolchain}.`,
      );
    }
    throw result.error;
  }
  if (result.status !== 0) {
    throw new Error(`${command} ${args[0] ?? ''} failed`);
  }
}

/**
 * The React Native to build against, in either of the two shapes bootstrap
 * accepts.
 *
 * A checkout, where bootstrap is handed the monorepo root and CMake the
 * packages/react-native beneath it; or an installed package, where both are
 * handed the package itself. The second is what every app has. React Native's
 * npm package ships everything the host compiles against except
 * ReactCxxPlatform, and bootstrap fetches that at the app's exact version --
 * see docs/PORTING.md.
 *
 * This used to refuse an installed package outright, saying the npm package
 * lacked the C++, long after bootstrap and CMake had learned to build from one.
 * The command was the only part that had not.
 */
export function reactNativeSources(reactNativePath: string): ReactNativeSources {
  const root = monorepoRoot(reactNativePath);
  if (root != null) {
    return {
      layout: 'checkout',
      bootstrapArg: root,
      rnDir: path.join(root, 'packages', 'react-native'),
    };
  }

  let real = reactNativePath;
  try {
    real = fs.realpathSync(reactNativePath);
  } catch {
    // Keep the original; the check below reports it.
  }
  if (!fs.existsSync(path.join(real, 'ReactCommon'))) {
    throw new Error(
      `react-native resolves to ${reactNativePath}, which has no ReactCommon: it is ` +
        'neither a React Native checkout nor an installed react-native package.',
    );
  }
  return {layout: 'installed', bootstrapArg: real, rnDir: real};
}

/**
 * A package's directory, found the way Node finds one: node_modules/<name> in
 * `from` or in any directory above it.
 *
 * Walked by hand rather than through require.resolve, because resolving
 * `<name>/package.json` throws for any package whose `exports` does not list
 * it, and the only question here is where the package is on disk.
 */
function findPackage(name: string, from: string): string | null {
  let dir = path.resolve(from);
  for (;;) {
    const candidate = path.join(dir, 'node_modules', name);
    if (fs.existsSync(path.join(candidate, 'package.json'))) {
      return candidate;
    }
    const parent = path.dirname(dir);
    if (parent === dir) {
      return null;
    }
    dir = parent;
  }
}

/**
 * The optional native halves an app brings with it, as CMake definitions.
 *
 * Expo's runtime, worklets and Reanimated each compile from the app's own copy
 * of the package (cmake/Expo.cmake and its siblings), and each is off unless
 * named. `--build` never named them, so the host it built for an Expo app had
 * no Expo runtime and the app failed at its first Expo import. Now whatever the
 * app has installed is what gets built.
 *
 * expo-modules-core is looked for beside `expo` as well, since it is `expo`'s
 * dependency rather than the app's, and a package manager that does not hoist
 * leaves it there. Reanimated without worklets is skipped with a note rather
 * than failing the configure: Reanimated 4 is built on worklets and
 * Reanimated.cmake requires both.
 */
/**
 * Capability packages: anything installed that declares native code of its own.
 *
 * A package says so in its own manifest -- `"basalt": {"native": "..."}` -- and
 * the build reads that rather than this file knowing its name. Which is the
 * difference between this and the three special cases below: Expo, worklets and
 * Reanimated are third-party packages that will never carry the key, so they
 * stay hardcoded; anything shipped for this platform does not have to be.
 *
 * Scanned from the app's own dependencies rather than the whole of
 * node_modules: a transitive dependency contributing C++ to the host binary
 * without the app asking is not a thing to make easy.
 */
export function capabilityPackages(projectRoot: string): string[] {
  const manifestPath = path.join(projectRoot, 'package.json');
  if (!fs.existsSync(manifestPath)) {
    return [];
  }
  let manifest;
  try {
    manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  } catch {
    return [];
  }
  const names = [
    ...Object.keys(manifest.dependencies ?? {}),
    ...Object.keys(manifest.devDependencies ?? {}),
  ];
  const found = [];
  for (const name of names) {
    const dir = findPackage(name, projectRoot);
    if (dir == null) {
      continue;
    }
    let declared;
    try {
      declared = JSON.parse(fs.readFileSync(path.join(dir, 'package.json'), 'utf8'));
    } catch {
      continue;
    }
    const native = declared?.basalt?.native;
    if (typeof native !== 'string') {
      continue;
    }
    if (!fs.existsSync(path.join(dir, native))) {
      throw new Error(
        `${name} declares "basalt.native": ${JSON.stringify(native)} and that file ` +
          `does not exist. A package that says it has native code and does not is a ` +
          `host built without it, which fails later and further away.`,
      );
    }
    found.push(dir);
  }
  return found;
}

/**
 * The app's own native modules: `<projectRoot>/modules/*` with a
 * `native/CMakeLists.txt` in it.
 *
 * Expo's autolinking looks in exactly this directory for an app's local modules
 * -- `expo-module.config.json` there is how `requireNativeModule('KinoProcess')`
 * finds native code that was never published -- and an app that has written an
 * iOS half and an Android half of one has nowhere to put the desktop half. A
 * package would be the alternative, and publishing a package to yourself, so
 * that one binary can compile a file already sitting in the repo, is not a
 * reasonable thing to ask.
 *
 * **Presence is the declaration**, unlike a capability package, which says so in
 * its manifest. A local module has no manifest to say it in: `create-expo-module
 * --local` writes a config, an index and a platform directory, and no
 * package.json. So the desktop half is declared the way the Apple half is --
 * by being there -- and `native/CMakeLists.txt` is the name, because that is
 * already what a host's package loop looks for.
 *
 * The rule capabilityPackages exists to keep is kept here for free: this is the
 * app's own source tree, so nothing gets compiled into the binary that the app
 * did not write.
 */
export function localNativeModules(projectRoot: string): string[] {
  const modulesDir = path.join(projectRoot, 'modules');
  let entries;
  try {
    entries = fs.readdirSync(modulesDir, {withFileTypes: true});
  } catch {
    // No modules directory, which is every app that has not made one.
    return [];
  }
  const found = [];
  for (const entry of entries.sort((a, b) => a.name.localeCompare(b.name))) {
    if (!entry.isDirectory() && !entry.isSymbolicLink()) {
      continue;
    }
    const dir = path.join(modulesDir, entry.name);
    if (fs.existsSync(path.join(dir, 'native', 'CMakeLists.txt'))) {
      found.push(dir);
    }
  }
  return found;
}

export function optionalNativeModules(
  projectRoot: string,
  platform: PackagePlatform,
): ContributedArgs {
  const args = [];
  const notes = [];
  // Forward slashes: CMake reads a backslash in a -D value as an escape.
  const define = (name: string, dir: string): number =>
    args.push(`-D${name}=${dir.split(path.sep).join('/')}`);

  const expo = findPackage('expo', projectRoot);
  const expoCore =
    findPackage('expo-modules-core', projectRoot) ??
    (expo != null ? findPackage('expo-modules-core', expo) : null);
  if (expoCore != null) {
    define('BASALT_EXPO_MODULES_CORE', expoCore);
    notes.push(`Expo's runtime, from ${expoCore}`);
  }

  const worklets = findPackage('react-native-worklets', projectRoot);
  if (worklets != null) {
    define('BASALT_WORKLETS', worklets);
    notes.push(`worklets, from ${worklets}`);
  }

  // @shopify/react-native-skia. Only on macOS, and it says so rather than
  // silently doing nothing: the published Skia binaries are Apple's, and an app
  // that draws with Skia will start on Linux or Windows and then fail at
  // `getEnforcing('RNSkiaModule')`, which is a long way from the cause.
  //
  // The question is about the *target*, not the machine doing the building.
  // This read `process.platform === 'darwin'` and so a GTK host cross-built on
  // a Mac was configured with Apple's Skia, linked nothing, and produced a
  // binary with no RNSkiaModule in it -- the exact start-up failure the note
  // exists to pre-empt, reported as if all was well. Found by building kino
  // for Linux.
  const skia = findPackage('@shopify/react-native-skia', projectRoot);
  if (skia != null && platform === 'macos') {
    define('BASALT_SKIA', skia);
    notes.push(`Skia, from ${skia}`);
  } else if (skia != null) {
    notes.push(
      `not building @shopify/react-native-skia (${skia}): the published binaries ` +
        `are Apple's, and ${platform} needs Skia built from source first. This ` +
        'host will have no RNSkiaModule, and importing the package at all -- not ' +
        'rendering with it, importing it -- ends the app at ' +
        "getEnforcing('RNSkiaModule'). Import it behind a check if this target matters.",
    );
  }

  const reanimated = findPackage('react-native-reanimated', projectRoot);
  if (reanimated != null && worklets != null) {
    define('BASALT_REANIMATED', reanimated);
    notes.push(`Reanimated, from ${reanimated}`);
  } else if (reanimated != null) {
    notes.push(
      `not building react-native-reanimated (${reanimated}): it needs ` +
        'react-native-worklets, which is not installed',
    );
  }

  // And anything that declares native code: a dependency that says so in its own
  // manifest, and a module the app wrote under `modules/`. One -D holding a list,
  // because CMake reads a semicolon-separated value as a list and the root
  // CMakeLists iterates it.
  // And anything the app wrote itself under `modules/`, which is where Expo's
  // autolinking looks for an app's own native modules.
  const packages = [...capabilityPackages(projectRoot), ...localNativeModules(projectRoot)];
  if (packages.length > 0) {
    const dirs = packages.map(dir => dir.split(path.sep).join('/'));
    args.push(`-DBASALT_PACKAGES=${dirs.join(';')}`);
    for (const dir of packages) {
      notes.push(`capability package, from ${dir}`);
    }
  }

  return {args, notes};
}

/**
 * What the configure step has to name rather than leave to CMake's defaults.
 *
 * **The compiler.** React Native's C++ is built -Wall -Werror -Wpedantic and is
 * warning-clean only under clang, which is why bootstrap's own configure line,
 * CI and the README all name it. Left alone, CMake picks the system default --
 * g++ on Ubuntu, cl on Windows -- and `--build` left it alone: a fresh Expo
 * app's first build on Ubuntu 24.04 compiled Hermes (bootstrap names clang)
 * and then stopped in ReactCommon on GCC's class-memaccess and pedantic
 * __int128 errors. CC and CXX still win, as they would for a plain cmake.
 *
 * **On Windows, two more.** CMAKE_BUILD_TYPE, because unset MSVC picks the debug
 * runtime and vcpkg follows it, while Hermes was built Release, and lld-link's
 * complaint about _ITERATOR_DEBUG_LEVEL names neither. And vcpkg's toolchain
 * file, found the way bootstrap.sh finds vcpkg: by a header it cannot do
 * without, not by VCPKG_ROOT, which vcvars64.bat points at an empty copy.
 */
export function compilerArgs(
  platform: string,
  env: NodeJS.ProcessEnv = process.env,
): string[] {
  const args: string[] = [];
  const named = Boolean(env.CC || env.CXX);

  if (platform !== 'windows') {
    if (!named) {
      args.push('-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++');
    }
    return args;
  }

  if (!named) {
    args.push('-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl');
  }
  args.push('-DCMAKE_BUILD_TYPE=RelWithDebInfo');

  const home = env.USERPROFILE || env.HOME || '';
  const roots = [
    env.VCPKG_ROOT,
    home && path.join(home, 'Tools', 'vcpkg'),
    home && path.join(home, 'vcpkg'),
    'C:/vcpkg',
  ].filter((root): root is string => typeof root === 'string' && root.length > 0);
  for (const root of roots) {
    const toolchain = path.join(root, 'scripts', 'buildsystems', 'vcpkg.cmake');
    const glog = path.join(root, 'installed', 'x64-windows', 'include', 'glog', 'logging.h');
    if (fs.existsSync(toolchain) && fs.existsSync(glog)) {
      args.push(`-DCMAKE_TOOLCHAIN_FILE=${toolchain.split(path.sep).join('/')}`);
      break;
    }
  }
  return args;
}

/**
 * Git Bash, found rather than assumed.
 *
 * bootstrap.sh runs under Git Bash on Windows, and `bash` by name is not it on
 * any machine with WSL: System32\bash.exe and WindowsApps\bash.exe both come
 * first on PATH, and both launch Linux. Run there, bootstrap sees a Linux host
 * and a Windows checkout and fails in ways that name neither.
 *
 * So: BASALT_BASH if set; else the bash.exe beside whichever git.exe is on PATH
 * (Git for Windows puts git.exe in <Git>\cmd and bash.exe in <Git>\bin); else
 * the standard install locations. Anything under System32 or WindowsApps is
 * refused wherever it came from.
 */
export function findGitBash(env: NodeJS.ProcessEnv = process.env): string | null {
  // Either separator: a path handed in through BASALT_BASH or PATH may use
  // forward slashes, and the tests run this on Linux too.
  const isLauncher = (candidate: string): boolean =>
    /[\\/](system32|windowsapps)[\\/]/i.test(candidate);
  const usable = (candidate: string | undefined): boolean =>
    candidate != null && !isLauncher(candidate) && fs.existsSync(candidate);

  if (env.BASALT_BASH) {
    return usable(env.BASALT_BASH) ? env.BASALT_BASH : null;
  }

  const found = [];
  for (const dir of (env.PATH || env.Path || '').split(';').filter(Boolean)) {
    if (fs.existsSync(path.join(dir, 'git.exe'))) {
      // <Git>\cmd\git.exe, or <Git>\mingw64\bin\git.exe.
      found.push(path.join(dir, '..', 'bin', 'bash.exe'));
      found.push(path.join(dir, '..', '..', 'bin', 'bash.exe'));
    }
  }
  for (const root of [env.ProgramW6432, env.ProgramFiles, env.LOCALAPPDATA && path.join(env.LOCALAPPDATA, 'Programs')]) {
    if (root) {
      found.push(path.join(root, 'Git', 'bin', 'bash.exe'));
    }
  }
  return found.map(candidate => path.normalize(candidate)).find(usable) ?? null;
}

/**
 * The environment `set` prints, as an object. Only lines of the form NAME=value;
 * cmd prints nothing else there, but a banner from a profile script might.
 */
export function parseSetOutput(output: string): Record<string, string> {
  const env: Record<string, string> = {};
  for (const line of output.split(/\r?\n/)) {
    const match = /^([^=\s][^=]*)=(.*)$/.exec(line);
    if (match) {
      env[match[1] as string] = match[2] as string;
    }
  }
  return env;
}

/**
 * The MSVC environment, loaded the way a Developer Command Prompt loads it.
 *
 * clang-cl needs the Windows SDK's headers and libraries, and only
 * vcvars64.bat puts them on INCLUDE and LIB; the Visual Studio CMake component
 * it also puts on PATH brings cmake and ninja. Without this, `--build` worked
 * only from a shell somebody had already prepared, and preparing one from
 * PowerShell meant a wrapper script -- which is how an example app came to
 * forward its arguments into npm's npx.ps1 with @args, and lose every one of
 * them.
 *
 * Skipped when the shell already has it (VCToolsInstallDir is vcvars' own
 * marker). VCPKG_ROOT is left as it was: vcvars points it at the copy bundled
 * with Visual Studio, which has nothing installed in it.
 */
export function msvcEnvironment(env: NodeJS.ProcessEnv = process.env): NodeJS.ProcessEnv {
  if (env.VCToolsInstallDir) {
    return env;
  }
  const programFilesX86 = env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)';
  const vswhere = path.join(programFilesX86, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
  if (!fs.existsSync(vswhere)) {
    throw new Error(
      'no Visual Studio found (vswhere.exe is missing). Building the host on Windows needs ' +
        'the Visual Studio Build Tools with the C++ workload and clang-cl.',
    );
  }
  const where = spawnSync(
    vswhere,
    ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Llvm.Clang', '-property', 'installationPath'],
    {encoding: 'utf8'},
  );
  const installation = (where.stdout || '').trim().split(/\r?\n/)[0];
  if (!installation) {
    throw new Error(
      'no Visual Studio with clang-cl found. Add "C++ Clang Compiler for Windows" to the ' +
        'Build Tools installation.',
    );
  }
  const vcvars = path.join(installation, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat');
  // Quoted twice over. With /s, cmd strips the first and last quote from what
  // follows /c and runs the rest, so a single pair around the path -- which has
  // spaces in it, under Program Files (x86) -- loses both of its quotes and
  // runs `C:\Program`. The outer pair is the one cmd removes.
  const loaded = spawnSync('cmd.exe', ['/d', '/s', '/c', `""${vcvars}" >nul 2>&1 && set"`], {
    encoding: 'utf8',
    env,
    windowsVerbatimArguments: true,
  });
  if (loaded.status !== 0) {
    throw new Error(`${vcvars} failed`);
  }
  const merged = {...env, ...parseSetOutput(loaded.stdout || '')};
  if (env.VCPKG_ROOT === undefined) {
    delete merged.VCPKG_ROOT;
  } else {
    merged.VCPKG_ROOT = env.VCPKG_ROOT;
  }
  return merged;
}

/**
 * The React Native monorepo root, when `reactNativePath` is inside a checkout.
 *
 * `reactNativePath` is the package directory, two levels below the root in a
 * checkout. An installed react-native has no monorepo above it, and this
 * returns null for one; reactNativeSources above decides what to do then.
 */
export function monorepoRoot(reactNativePath: string): string | null {
  // Through the symlink first. A workspace, a pnpm store and `npm link` all
  // put a link in node_modules, and walking up from the link lands in the app
  // rather than in the checkout it points at.
  let real = reactNativePath;
  try {
    real = fs.realpathSync(reactNativePath);
  } catch {
    // Keep the original and let the check below decide.
  }

  const candidate = path.resolve(real, '..', '..');
  if (fs.existsSync(path.join(candidate, 'packages', 'react-native'))) {
    return candidate;
  }
  return null;
}

/**
 * Building the host from an app.
 *
 * This is what `run-android` gets from Gradle and `run-ios` from Xcode. There
 * is no equivalent to hand it to, so the work is here: vendor React Native's
 * C++ dependencies, configure CMake against the app's own React Native, and
 * build.
 *
 * Two things about where output goes. It goes under the app, in `.basalt`, not
 * into `node_modules`, because npm rewrites node_modules on install and a
 * Hermes build is not something to lose that way. And `third_party` is shared
 * between builds of the same app rather than per configuration, because it
 * depends on the React Native version and nothing else; the version stamp
 * inside it is what keeps that honest.
 *
 * The first build is slow in a way `run-android` is not. It compiles Hermes and
 * React Native's C++ core. Saying so before it starts is better than a silent
 * half hour.
 */
/**
 * A build failure, with what was in the build.
 *
 * A capability package -- or one of the app's own modules -- compiles into the
 * same binary as the platform, so its compiler errors arrive looking exactly
 * like the platform's own: a path, in a node_modules directory nobody was
 * thinking about. Naming what contributed is the part that cannot be read off
 * the error.
 *
 * Which package broke it is deliberately not claimed. The build output is
 * streamed rather than captured, because a person watching a twenty-minute
 * compile should see it happen, and buffering it to grep for a directory would
 * trade that for a guess. Naming the contributors is what can be said honestly
 * without giving that up.
 */
export function explainContributedFailure(projectRoot: string, error: Error): Error {
  let contributors: string[] = [];
  try {
    contributors = [...capabilityPackages(projectRoot), ...localNativeModules(projectRoot)];
  } catch {
    // The packages could not even be listed, which is its own error and not
    // one to raise from inside the handler for a different one.
    return error;
  }
  if (contributors.length === 0) {
    return error;
  }
  const names = contributors.map(dir => path.basename(dir)).join(', ');
  return new Error(
    `${error.message}\n\n` +
      `This host was built with native code from ${contributors.length} ` +
      `contributor${contributors.length === 1 ? '' : 's'}: ${names}. ` +
      `A compiler error naming a path inside one of those is that contributor's ` +
      `rather than the platform's.`,
  );
}

/**
 * Clears a build directory from before it was split by target.
 *
 * `.basalt/build` used to be the build directory itself; it is now a directory
 * of them, one per target. The old one cannot become the new one: a CMake build
 * directory records its own path, and moving it produces "The current
 * CMakeCache.txt directory ... is different than the directory ..." -- which was
 * this function's first shape, and it did not work.
 *
 * So it is moved aside rather than salvaged, and the target is built once more.
 * That costs less than it sounds: the expensive vendoring -- Hermes, folly,
 * ReactCxxPlatform -- lives in `.basalt/third_party`, which is untouched, and
 * ccache carries most of the rest.
 *
 * Kept rather than deleted, because it is hundreds of megabytes of someone
 * else's disk and the CLI is not the right thing to decide that. The line it
 * prints says where it went.
 */
export function migrateBuildDirectory(workDir: string, log: (line: string) => void): void {
  const build = path.join(workDir, 'build');
  if (!fs.existsSync(path.join(build, 'CMakeCache.txt'))) {
    return;
  }

  const parked = path.join(workDir, 'build.before-split');
  fs.rmSync(parked, {recursive: true, force: true});
  fs.renameSync(build, parked);
  fs.mkdirSync(build, {recursive: true});
  log(
    `builds are per target now, and a CMake build directory cannot be moved. ` +
      `The previous one is at ${parked} -- this target builds once more, then ` +
      `incrementally. Delete it when you are happy.`,
  );
}

export function buildHost(context: CliContext, options: RunOptions, target: Target): string {
  const projectRoot = context.root;
  const workDir = path.join(projectRoot, '.basalt');
  // One per target, because a build directory belongs to the host it was
  // configured for. CMake refuses to reuse one whose source directory has
  // changed -- it says the two paths do not match and explains neither -- so a
  // single `.basalt/build` meant that building for a second desktop from one
  // checkout destroyed the first. Found by building a macOS app for Linux.
  const buildDir = path.join(workDir, 'build', target.platform);
  const thirdParty = path.join(workDir, 'third_party');

  const sources = reactNativeSources(context.reactNativePath);
  const nativeModules = optionalNativeModules(projectRoot, target.platform);

  fs.mkdirSync(workDir, {recursive: true});
  migrateBuildDirectory(workDir, line => console.log(`    ${line}`));

  console.log(`==> React Native from ${sources.layout === 'installed' ? 'the installed package' : 'a checkout'}: ${sources.rnDir}`);
  for (const note of nativeModules.notes) {
    console.log(`    ${note}`);
  }

  console.log("==> vendoring React Native's C++ dependencies");
  console.log('    The first run compiles Hermes and takes a while.');
  const bootstrap = path.join(target.coreDir, 'bootstrap.sh');
  // Through bash by name, everywhere. On Windows a .sh file is not executable
  // and there is no shebang handling, and the shell that runs it is Git Bash,
  // which is what bootstrap.sh detects and expects. And on Linux and macOS the
  // executable bit is only there if whoever packed the package had one to
  // give: a tarball packed on Windows has none, and running the script directly
  // would fail with EACCES for exactly the people installing from npm.
  //
  // On Windows it has to be Git Bash specifically, and everything from here on
  // needs the MSVC environment; see findGitBash and msvcEnvironment.
  let buildEnv = process.env;
  let bash = 'bash';
  if (process.platform === 'win32') {
    buildEnv = msvcEnvironment(process.env);
    const found = findGitBash(buildEnv);
    if (found == null) {
      throw new Error(
        'building the host on Windows needs Git Bash, and none was found -- only a WSL ' +
          'launcher, or nothing. Install Git for Windows, or set BASALT_BASH to its bash.exe.',
      );
    }
  }
  run(
    bash,
    [bootstrap, sources.bootstrapArg],
    {cwd: workDir, env: {...buildEnv, BASALT_THIRD_PARTY: thirdParty}},
    target.toolchain,
  );

  console.log('==> configuring');
  run(
    'cmake',
    [
      '-S',
      target.nativeDir,
      '-B',
      buildDir,
      '-G',
      target.generator ?? 'Ninja',
      `-DRN_DIR=${sources.rnDir}`,
      ...nativeModules.args,
      ...compilerArgs(target.platform),
      `-DBASALT_THIRD_PARTY=${thirdParty}`,
      // Where the shared half is. CMake has a default that covers a checkout
      // and a hoisted node_modules, and naming it here covers pnpm's store too,
      // where the two packages are not siblings on disk at all.
      `-DBASALT_CORE_DIR=${target.coreDir}`,
      ...(target.configureArgs ?? []),
    ],
    {env: buildEnv},
    target.toolchain,
  );

  console.log('==> building');
  const jobs = options.jobs ? ['-j', String(options.jobs)] : [];
  try {
    run('cmake', ['--build', buildDir, ...jobs], {env: buildEnv}, target.toolchain);
  } catch (error) {
    throw explainContributedFailure(projectRoot, error as Error);
  }

  const binary = path.join(buildDir, target.binary);
  if (!isExecutable(binary)) {
    throw new Error(`the build produced no ${target.binary} at ${binary}`);
  }
  return binary;
}

/**
 * The name the app registered with AppRegistry.
 *
 * app.json's `name`, which is what `registerRootComponent` and every template
 * use, and what run-android reads for the same purpose. Expo apps register
 * "main" regardless, so an explicit --module has to win.
 */
export function resolveModuleName(projectRoot: string, options: RunOptions): string {
  if (typeof options.module === 'string') {
    return options.module;
  }
  const appJson = path.join(projectRoot, 'app.json');
  if (fs.existsSync(appJson)) {
    try {
      const parsed = JSON.parse(fs.readFileSync(appJson, 'utf8'));
      if (typeof parsed.name === 'string' && parsed.name.length > 0) {
        return parsed.name;
      }
    } catch (error) {
      throw new Error(`could not read ${appJson}: ${(error as Error).message}`);
    }
  }
  throw new Error(
    'no module name: add a "name" to app.json, or pass --module <name>.\n' +
      'It is the name the app passes to AppRegistry.registerComponent.',
  );
}

/**
 * One bundler for every app.
 *
 * React Native's CLI could do this, and did, but only an app that has that CLI
 * can reach it -- an Expo app cannot. Since the same code has to exist for
 * them, it may as well be the only path, so both kinds of app get identical
 * output and there is one place where assets can go wrong.
 */
async function bundleForRelease(
  context: CliContext,
  options: RunOptions,
  outputPath: string,
  target: Target,
): Promise<void> {
  const result = await bundleWithAssets({
    projectRoot: context.root,
    entryFile: options.entryFile ?? 'index.js',
    bundleOutput: outputPath,
    platform: target.platform,
    dev: false,
  });
  console.log(
    `    ${result.files} asset file${result.files === 1 ? '' : 's'} beside the bundle`,
  );
}

/**
 * Runs the host in the foreground.
 *
 * Foreground on purpose: this is a desktop application, and the terminal that
 * launched it is where its output belongs and where Ctrl-C should reach it.
 */
type LaunchOptions = {
  bundlePath?: string;
  moduleName: string;
  dev: boolean;
  port: number;
  entry?: string;
  cwd: string;
};

function launch(
  hostBinary: string,
  {bundlePath, moduleName, dev, port, entry, cwd}: LaunchOptions,
): Promise<number> {
  const env = {...process.env};
  if (dev) {
    env.BASALT_DEV = '1';
    env.BASALT_DEV_PORT = String(port);
    env.BASALT_DEV_ENTRY = entry;
  } else {
    delete env.BASALT_DEV;
  }

  return new Promise((resolve, reject) => {
    const child = spawn(hostBinary, [bundlePath ?? '', moduleName], {
      cwd,
      env,
      stdio: 'inherit',
    });
    child.on('error', reject);
    child.on('exit', (code, signal) => {
      // A signal is how a windowed app normally ends here, and Ctrl-C reaches
      // the child directly because it shares this terminal's process group.
      resolve(signal != null ? 0 : (code ?? 0));
    });
  });
}

/**
 * Whether React Native's `start` command can load its Metro configuration in
 * this app. Null when it can; otherwise the message to show.
 *
 * Development runs start Metro through that command, and its loader requires
 * @react-native/metro-config from the app whatever the app's own
 * metro.config.js says. A React Native template lists it. An Expo app does
 * not -- Expo's CLI brings its own configuration -- so every Expo app's first
 * development run died inside Metro with "Cannot resolve
 * `@react-native/metro-config`", and only after the port timeout. Asked up
 * front, the answer can name the version to install: the app's react-native
 * version, which is what @react-native/community-cli-plugin pins it to.
 */
export function missingMetroConfig(projectRoot: string): string | null {
  if (findPackage('@react-native/metro-config', projectRoot) != null) {
    return null;
  }
  let spec = '@react-native/metro-config';
  const reactNative = findPackage('react-native', projectRoot);
  if (reactNative != null) {
    try {
      const {version} = JSON.parse(fs.readFileSync(path.join(reactNative, 'package.json'), 'utf8'));
      if (version) {
        spec = `@react-native/metro-config@${version}`;
      }
    } catch {
      // No version to name; the bare package name is still the right advice.
    }
  }
  return (
    "a development run starts Metro through React Native's `start` command, which " +
    'needs @react-native/metro-config installed in the app, and this app has none. ' +
    'Expo apps do not ship it. Install it with:\n\n' +
    `  npm install --save-dev ${spec}\n\n` +
    'or run a bundle, which needs no Metro, with --mode release.'
  );
}

async function runDesktop(
  _argv: string[],
  context: CliContext,
  options: RunOptions,
  target: Target,
): Promise<void> {
  const projectRoot = context.root;
  const port = Number(options.port) || DEFAULT_PORT;
  const dev = options.mode !== 'release';

  // --build is explicit rather than automatic. A first build compiles Hermes
  // and React Native's C++ core, which is not something a command should start
  // on its own because a binary happened to be missing.
  let hostBinary;
  if (options.build) {
    hostBinary = buildHost(context, options, target);
  } else {
    hostBinary = resolveHost(projectRoot, options, target);
  }
  const moduleName = resolveModuleName(projectRoot, options);

  // Named relative to the project so that the message is readable, but passed
  // to the host as an absolute path, since it runs with the project as its
  // working directory and a relative bundle would resolve differently.
  const bundlePath = path.resolve(
    projectRoot,
    options.bundle ?? path.join('build', 'main.jsbundle.js'),
  );

  // The binary becomes an application before it is run, on every run rather
  // than only for a release.
  //
  // That is not thoroughness, it is the only way the two agree: on macOS
  // `NSBundle.mainBundle` comes from where the executable sits, so a host run
  // out of a build directory has no bundle identifier and cannot notify, while
  // the same code inside a `.app` can. Packaging only for release would mean a
  // feature that works in production and not in development, which is the worse
  // half of a difference between them. See cli/packageApp.js.
  const packaged = packageApp({
    platform: target.platform,
    hostBinary,
    outputDir: path.resolve(projectRoot, 'build'),
    projectRoot,
    bundlePath,
  });
  hostBinary = packaged.launchPath;
  if (packaged.appPath != null) {
    console.log(`==> ${path.relative(projectRoot, packaged.appPath)}`);
  }
  if (packaged.desktopPath != null) {
    const relative = path.relative(projectRoot, packaged.desktopPath);
    console.log(
      `==> wrote ${relative}\n` +
        `    install it to give this app its own name and icon on notifications:\n` +
        `      desktop-file-install --dir="$HOME/.local/share/applications" ${relative}`,
    );
  }


  if (dev) {
    if (options.packager === false) {
      console.log(`==> not starting Metro; expecting one on port ${port}`);
    } else if (await isPortTaken(port)) {
      console.log(`==> reusing the packager already on port ${port}`);
    } else {
      const problem = missingMetroConfig(projectRoot);
      if (problem != null) {
        throw new Error(problem);
      }
      const {logPath} = await startMetro(context, port);
      console.log(
        `==> started Metro on port ${port}, logging to ${path.relative(projectRoot, logPath)}`,
      );
    }

    // Still needed in development: the host falls back to this file when Metro
    // cannot be reached, and a missing one turns a packager problem into a
    // confusing failure to load anything at all.
    if (!fs.existsSync(bundlePath)) {
      console.log('==> writing a fallback bundle for when Metro is unreachable');
      fs.mkdirSync(path.dirname(bundlePath), {recursive: true});
      await bundleForRelease(context, options, bundlePath, target);
    }

    // Every dev run, not only the one that wrote the fallback bundle: the host
    // reads app.config.json from beside the bundle, and an app.json edited
    // since would otherwise be invisible in development and applied in release
    // -- the worse half of a difference between the two.
    await writeExpoAppConfig(projectRoot, bundlePath);
  } else {
    console.log('==> bundling for release');
    await bundleForRelease(context, options, bundlePath, target);
  }

  console.log(`==> ${path.basename(hostBinary)} ${moduleName}${dev ? ' (dev)' : ''}`);
  const status = await launch(hostBinary, {
    bundlePath,
    moduleName,
    dev,
    port,
    entry: (options.entryFile ?? 'index.js').replace(/\.[^.]+$/, ''),
    cwd: projectRoot,
  });
  if (status !== 0) {
    throw new Error(`the app exited with status ${status}`);
  }
}

/**
 * The CLI command object React Native's CLI expects, for one target.
 */
export function makeRunCommand(target: Target) {
  return {
    name: target.command,
    description: `builds nothing and runs your app on ${target.label}, against a packager`,
    func: async (argv: string[], context: CliContext, options: RunOptions): Promise<void> => {
      try {
        await runDesktop(argv, context, options, target);
      } catch (error) {
        if (error instanceof MissingHost) {
          // Its message is the instructions; a stack trace on top would bury
          // them.
          console.error(`\nerror: ${error.message}\n`);
          process.exitCode = 1;
          return;
        }
        throw error;
      }
    },
    options: [
      {
        name: '--port <number>',
        description: 'the port Metro is or should be on',
        default: DEFAULT_PORT,
      },
      {
        name: '--mode <string>',
        description: '"dev" to run against Metro, "release" to run a bundle',
        default: 'dev',
      },
      {
        name: '--no-packager',
        description: 'do not start Metro, and do not check for one',
      },
      {
        name: '--build',
        description: 'build the host before running it, into .basalt/build',
      },
      {
        name: '--jobs <number>',
        description: 'parallelism for --build',
      },
      {
        name: '--host-binary <path>',
        description: `the ${target.binary} to run, if it is not somewhere obvious`,
      },
      {
        name: '--module <string>',
        description: "the AppRegistry name to start, if it is not app.json's",
      },
      {
        name: '--bundle <path>',
        description: 'where the release bundle is written and read',
      },
      {
        name: '--entry-file <path>',
        description: 'the app entry point',
        default: 'index.js',
      },
    ],
  };
}
