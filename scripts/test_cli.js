/**
 * Tests for the desktop CLI: `run-linux`, `run-macos` and `run-windows`.
 *
 * The three are one command with three names, and everything they share lives
 * in basalt-core's cli/desktop.js. That sharing is the thing worth
 * testing: a change made for one desktop lands on all three, and the failure
 * mode is an app that bundles for the wrong platform or looks for the wrong
 * binary -- neither of which any C++ test can see.
 *
 * Pure functions only. Nothing here spawns cmake, starts Metro or launches a
 * host; what is checked is the decisions those steps are handed.
 *
 * Run with:  node --test scripts/test_cli.js
 *
 * @format
 */

'use strict';

const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {test} = require('node:test');

const REPO = path.resolve(__dirname, '..');

// The built package, not the source. basalt-core is TypeScript and
// `main` points into dist/; requiring the source would either miss a
// conversion or fail on a .ts import. scripts/build_ts.sh runs before this in
// both CI and test_all.sh.
const DIST = path.join(REPO, 'packages/basalt-core/dist');
if (!fs.existsSync(DIST)) {
  throw new Error(`${DIST} does not exist. Run scripts/build_ts.sh first.`);
}
const desktop = require(path.join(DIST, 'cli/desktop.js'));

// The three real command modules, loaded the way React Native's CLI loads them:
// through each platform package's react-native.config.js. That this resolves at
// all is half the test -- the shared half is reached by a two-step require that
// only works in the two layouts described in cli/shared.js.
const CONFIGS = {
  linux: path.join(REPO, 'packages/basalt-gtk/react-native.config.js'),
  macos: path.join(REPO, 'packages/basalt-appkit/react-native.config.js'),
  windows: path.join(REPO, 'packages/basalt-win32/react-native.config.js'),
};

const EXPECTED = {
  linux: {command: 'run-linux', binary: 'basalt_gtk'},
  macos: {command: 'run-macos', binary: 'basalt_appkit'},
  windows: {command: 'run-windows', binary: 'basalt_win32.exe'},
};

function scratch() {
  // Resolved, because the code under test resolves too. `monorepoRoot` calls
  // realpathSync deliberately -- a workspace, a pnpm store and `npm link` all
  // put a symlink in node_modules, and walking up from the link lands in the
  // app rather than in the checkout it points at -- so it returns a real path
  // and a test comparing against an unresolved one fails.
  //
  // Only on macOS, where os.tmpdir() is /var/folders/... and /var is a symlink
  // to /private/var. Linux's /tmp is not a symlink and Windows has no such
  // thing, so this passed in CI and failed on the machine the macOS work was
  // being done on.
  return fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-cli-')));
}

// A target of the shape a platform package passes in, for the pieces that take
// one directly.
function target(platform) {
  return {
    platform,
    label: platform,
    command: EXPECTED[platform].command,
    binary: EXPECTED[platform].binary,
    nativeDir: path.join(REPO, 'packages', 'somewhere', 'native'),
    coreDir: path.join(REPO, 'packages/basalt-core/native'),
    toolchain: 'a compiler',
  };
}

test('every platform package contributes exactly one command', () => {
  for (const [platform, configPath] of Object.entries(CONFIGS)) {
    const config = require(configPath);
    assert.equal(config.commands.length, 1, `${platform} should export one command`);

    const command = config.commands[0];
    assert.equal(command.name, EXPECTED[platform].command);
    assert.equal(typeof command.func, 'function');

    // The binary name reaches the user, in --host-binary's help and in the
    // message when no host is found. Getting it wrong sends them looking for a
    // file that was never going to exist.
    const hostOption = command.options.find(option =>
      option.name.startsWith('--host-binary'),
    );
    assert.ok(
      hostOption.description.includes(EXPECTED[platform].binary),
      `${platform} should name ${EXPECTED[platform].binary}`,
    );
  }
});

test('the three commands offer the same options', () => {
  const names = platform =>
    require(CONFIGS[platform]).commands[0].options.map(option => option.name).sort();

  // Not a tautology now that they are one function, but it is the property the
  // sharing exists to guarantee -- and the one a future per-platform special
  // case would quietly break.
  assert.deepEqual(names('linux'), names('macos'));
  assert.deepEqual(names('linux'), names('windows'));
  assert.ok(names('linux').includes('--mode <string>'));
  assert.ok(names('linux').includes('--build'));
});

test('a directory is not an executable, and a missing file is not either', () => {
  const directory = scratch();
  assert.equal(desktop.isExecutable(directory), false);
  assert.equal(desktop.isExecutable(path.join(directory, 'nothing')), false);

  const file = path.join(directory, 'basalt_win32.exe');
  fs.writeFileSync(file, '');
  // Executable on Windows, where the question is only whether it is a file;
  // not on Unix, where it was written without the bit.
  assert.equal(desktop.isExecutable(file), process.platform === 'win32');

  fs.rmSync(directory, {recursive: true, force: true});
});

test('the host is looked for in the explicit places first', () => {
  const project = scratch();
  const looked = desktop.candidates(project, {hostBinary: 'somewhere/host'}, target('linux'));

  assert.equal(looked[0], path.join(project, 'somewhere', 'host'));
  // .basalt/build is where --build puts one, and it must beat the checkout's
  // build tree or a developer's stale binary would win over their fresh one.
  assert.ok(looked.some(entry => entry.includes(path.join('.basalt', 'build'))));
  assert.ok(looked.every(entry => entry.endsWith('basalt_gtk') || entry.endsWith('host')));

  fs.rmSync(project, {recursive: true, force: true});
});

test('the per-target build directory is looked in first, and the old one still counts', () => {
  // `.basalt/build` was the build directory; it is now a directory of them, one
  // per target, because CMake refuses to reuse one configured for a different
  // host -- so building a macOS app for Linux used to destroy the macOS build.
  const project = scratch();
  const looked = desktop.candidates(project, {}, target('linux'));

  const perTarget = path.join(project, '.basalt', 'build', 'linux', 'basalt_gtk');
  const old = path.join(project, '.basalt', 'build', 'basalt_gtk');
  assert.ok(looked.includes(perTarget), 'the per-target path is looked in');
  assert.ok(looked.includes(old), 'and the old one, so an existing build still runs');
  assert.ok(
    looked.indexOf(perTarget) < looked.indexOf(old),
    'the per-target one first: a fresh build must beat one from before the split',
  );

  fs.rmSync(project, {recursive: true, force: true});
});

test('two targets do not share a build directory', () => {
  const project = scratch();
  const forLinux = desktop.candidates(project, {}, target('linux'))[0];
  const forWindows = desktop.candidates(project, {}, target('windows'))[0];

  assert.notEqual(path.dirname(forLinux), path.dirname(forWindows));

  fs.rmSync(project, {recursive: true, force: true});
});

test('a build from before the split is moved aside, not salvaged', () => {
  // It cannot be salvaged: a CMake build directory records its own path and
  // refuses to run from anywhere else. Moving it under its target was the first
  // attempt and produced "The current CMakeCache.txt directory ... is different
  // than the directory ..." on the very next configure.
  const work = scratch();
  const build = path.join(work, 'build');
  fs.mkdirSync(build, {recursive: true});
  fs.writeFileSync(path.join(build, 'CMakeCache.txt'), 'CMAKE_HOME_DIRECTORY:INTERNAL=/x-appkit/native\n');
  fs.writeFileSync(path.join(build, 'basalt_appkit'), '');

  const said = [];
  desktop.migrateBuildDirectory(work, line => said.push(line));

  assert.ok(fs.existsSync(path.join(work, 'build.before-split', 'basalt_appkit')), 'kept');
  assert.ok(!fs.existsSync(path.join(build, 'CMakeCache.txt')), 'and out of the way');
  assert.ok(fs.existsSync(build), 'leaving a directory for the per-target ones');
  assert.ok(said.some(line => line.includes('build.before-split')), 'and said where it went');

  fs.rmSync(work, {recursive: true, force: true});
});

test('the new layout is left alone', () => {
  // It runs on every build, so doing nothing when there is nothing to do is the
  // case that matters most.
  const work = scratch();
  const linux = path.join(work, 'build', 'linux');
  fs.mkdirSync(linux, {recursive: true});
  fs.writeFileSync(path.join(linux, 'CMakeCache.txt'), 'CMAKE_HOME_DIRECTORY:INTERNAL=/x-gtk/native\n');

  const said = [];
  desktop.migrateBuildDirectory(work, line => said.push(line));

  assert.ok(fs.existsSync(path.join(linux, 'CMakeCache.txt')), 'untouched');
  assert.equal(said.length, 0, 'and silent');

  fs.rmSync(work, {recursive: true, force: true});
});

test('a missing host explains how to make one rather than printing a path', () => {
  const project = scratch();
  // The package's native directory is a scratch one too. The last place the
  // host is looked for is the development checkout's build tree, found from
  // nativeDir -- and with nativeDir inside this repository, that is this
  // repository's build/, which on CI holds a real basalt_win32.exe by the time
  // this runs. The test passed for as long as it ran before the build, and
  // failed the first time it ran after one.
  const windows = {...target('windows'), nativeDir: path.join(project, 'pkg', 'native')};
  const previous = process.env.BASALT_HOST;
  delete process.env.BASALT_HOST;
  let thrown = null;
  try {
    desktop.resolveHost(project, {}, windows);
  } catch (error) {
    thrown = error;
  } finally {
    if (previous !== undefined) {
      process.env.BASALT_HOST = previous;
    }
  }

  assert.ok(thrown instanceof desktop.MissingHost);
  assert.ok(thrown.message.includes('react-native run-windows --build'));
  assert.ok(thrown.message.includes('basalt_win32.exe'));
  // The toolchain sentence is the platform's own, and it is the only part of
  // that message a reader can act on if they have nothing installed.
  assert.ok(thrown.message.includes('a compiler'));

  fs.rmSync(project, {recursive: true, force: true});
});

test('BASALT_HOST wins over everything but --host-binary', () => {
  const project = scratch();
  const host = path.join(project, 'from-the-environment');
  fs.writeFileSync(host, '');
  fs.chmodSync(host, 0o755);

  const previous = process.env.BASALT_HOST;
  process.env.BASALT_HOST = host;
  try {
    assert.equal(desktop.resolveHost(project, {}, target('linux')), path.normalize(host));
  } finally {
    if (previous === undefined) {
      delete process.env.BASALT_HOST;
    } else {
      process.env.BASALT_HOST = previous;
    }
    fs.rmSync(project, {recursive: true, force: true});
  }
});

test('the module name comes from --module, then app.json', () => {
  const project = scratch();

  assert.equal(desktop.resolveModuleName(project, {module: 'Explicit'}), 'Explicit');

  // No app.json and no --module: the error has to say what a module name *is*,
  // because "no module name" is meaningless to someone who has never written
  // an AppRegistry call by hand.
  assert.throws(
    () => desktop.resolveModuleName(project, {}),
    /AppRegistry\.registerComponent/,
  );

  fs.writeFileSync(path.join(project, 'app.json'), JSON.stringify({name: 'FromAppJson'}));
  assert.equal(desktop.resolveModuleName(project, {}), 'FromAppJson');
  // Still second to --module: an Expo app's app.json says one thing and
  // registerRootComponent registers "main".
  assert.equal(desktop.resolveModuleName(project, {module: 'main'}), 'main');

  fs.writeFileSync(path.join(project, 'app.json'), 'not json');
  assert.throws(() => desktop.resolveModuleName(project, {}), /could not read/);

  fs.rmSync(project, {recursive: true, force: true});
});

test('a monorepo root is only found above a real checkout', () => {
  const root = scratch();

  // What an installed react-native looks like: no packages/react-native above.
  const installed = path.join(root, 'app', 'node_modules', 'react-native');
  fs.mkdirSync(installed, {recursive: true});
  assert.equal(desktop.monorepoRoot(installed), null);

  // And what a checkout looks like.
  const checkout = path.join(root, 'checkout', 'packages', 'react-native');
  fs.mkdirSync(checkout, {recursive: true});
  assert.equal(desktop.monorepoRoot(checkout), path.join(root, 'checkout'));

  fs.rmSync(root, {recursive: true, force: true});
});

test('an installed react-native is built from, not refused', () => {
  const project = scratch();

  // What every app has: the package, with ReactCommon in it and no monorepo
  // above it.
  const installed = path.join(project, 'node_modules', 'react-native');
  fs.mkdirSync(path.join(installed, 'ReactCommon'), {recursive: true});
  const fromPackage = desktop.reactNativeSources(installed);
  assert.equal(fromPackage.layout, 'installed');
  assert.equal(fromPackage.bootstrapArg, fs.realpathSync(installed));
  assert.equal(fromPackage.rnDir, fs.realpathSync(installed));

  // A checkout still hands bootstrap the root and CMake the package under it.
  const checkout = path.join(project, 'react-native');
  fs.mkdirSync(path.join(checkout, 'packages', 'react-native', 'ReactCommon'), {recursive: true});
  const fromCheckout = desktop.reactNativeSources(
    path.join(checkout, 'packages', 'react-native'),
  );
  assert.equal(fromCheckout.layout, 'checkout');
  assert.equal(fromCheckout.bootstrapArg, fs.realpathSync(checkout));
  assert.equal(
    fromCheckout.rnDir,
    path.join(fs.realpathSync(checkout), 'packages', 'react-native'),
  );

  // Neither: say so, rather than handing bootstrap a directory it will refuse.
  const empty = path.join(project, 'empty');
  fs.mkdirSync(empty);
  assert.throws(() => desktop.reactNativeSources(empty), /no ReactCommon/);

  fs.rmSync(project, {recursive: true, force: true});
});

test('the native halves an app has installed are the ones built', () => {
  const project = scratch();
  const install = (under, name) => {
    const dir = path.join(under, 'node_modules', name);
    fs.mkdirSync(dir, {recursive: true});
    fs.writeFileSync(path.join(dir, 'package.json'), JSON.stringify({name}));
    return dir;
  };
  const cmakePath = dir => dir.split(path.sep).join('/');

  // A plain React Native app builds none of them.
  assert.deepEqual(desktop.optionalNativeModules(project, 'macos').args, []);

  // expo-modules-core beneath expo, where a package manager that does not
  // hoist leaves it -- found all the same.
  const expo = install(project, 'expo');
  const expoCore = install(expo, 'expo-modules-core');
  assert.deepEqual(desktop.optionalNativeModules(project, 'macos').args, [
    `-DBASALT_EXPO_MODULES_CORE=${cmakePath(expoCore)}`,
  ]);

  // Reanimated without worklets is skipped with a note, not passed to a
  // configure that would refuse it.
  const reanimated = install(project, 'react-native-reanimated');
  let found = desktop.optionalNativeModules(project, 'macos');
  assert.ok(!found.args.some(arg => arg.startsWith('-DBASALT_REANIMATED')));
  assert.ok(found.notes.some(note => note.includes('react-native-worklets')));

  const worklets = install(project, 'react-native-worklets');
  found = desktop.optionalNativeModules(project, 'macos');
  assert.ok(found.args.includes(`-DBASALT_WORKLETS=${cmakePath(worklets)}`));
  assert.ok(found.args.includes(`-DBASALT_REANIMATED=${cmakePath(reanimated)}`));

  // Skia, found under its scope. Whether it is built is a question about the
  // archives on disk, not about the platform: macOS has published ones, and
  // Linux and Windows have whatever scripts/build_skia_linux.sh and its
  // Windows counterpart left in the package. Asking the filesystem is what
  // lets a host that built them use them.
  //
  // This test used to assert the opposite -- that Linux and Windows never get
  // Skia -- which was true of the platform gate it was written against and
  // became false the day those hosts got an RNSkiaModule. Asserting a rule
  // rather than the behaviour behind it is how a test outlives its subject.
  const skia = install(project, '@shopify/react-native-skia');
  const archives = {
    macos: ['libs', 'macos', 'libskia.xcframework'],
    windows: ['libs', 'windows', 'x86_64', 'skia.lib'],
    // The same derivation the CLI makes: an ELF archive is per architecture,
    // and a Linux host is built natively, so this machine's architecture is
    // the one that matters.
    linux: [
      'libs',
      'linux',
      process.arch === 'arm64' ? 'aarch64' : 'x86_64',
      'libskia.a',
    ],
  };

  // The package is installed but nobody has built or downloaded anything, so
  // no target gets Skia and each says where it looked.
  for (const platform of ['macos', 'linux', 'windows']) {
    found = desktop.optionalNativeModules(project, platform);
    assert.ok(
      !found.args.some(arg => arg.startsWith('-DBASALT_SKIA')),
      `${platform} was configured with Skia it does not have`,
    );
    const note = found.notes.find(n => n.includes('react-native-skia'));
    assert.ok(note, `${platform} dropped Skia without saying so`);
    assert.ok(
      note.includes(archives[platform][archives[platform].length - 1]),
      'the note does not say which archive was missing',
    );
    assert.ok(note.includes('RNSkiaModule'), 'the note does not name the failure');
  }

  // Put the archive where each target looks, one at a time, and that target --
  // and only that target -- builds Skia.
  for (const platform of ['macos', 'linux', 'windows']) {
    const archive = path.join(skia, ...archives[platform]);
    fs.mkdirSync(path.dirname(archive), {recursive: true});
    // macOS's is a directory and the other two are files; existsSync is happy
    // with either, and so is the check it stands in for.
    if (platform === 'macos') {
      fs.mkdirSync(archive, {recursive: true});
    } else {
      fs.writeFileSync(archive, '');
    }
    found = desktop.optionalNativeModules(project, platform);
    assert.ok(
      found.args.includes(`-DBASALT_SKIA=${cmakePath(skia)}`),
      `${platform} has its archive and still did not build Skia`,
    );
    fs.rmSync(archive, {recursive: true, force: true});
  }

  fs.rmSync(project, {recursive: true, force: true});
});

test('the build names clang rather than taking the system compiler', () => {
  // Linux and macOS: clang, unless the environment names a compiler itself.
  assert.deepEqual(desktop.compilerArgs('linux', {}), [
    '-DCMAKE_C_COMPILER=clang',
    '-DCMAKE_CXX_COMPILER=clang++',
  ]);
  assert.deepEqual(desktop.compilerArgs('macos', {CXX: 'g++-14'}), []);

  // Windows: clang-cl and a build type always; the compiler yields to CC/CXX,
  // the build type does not.
  const bare = desktop.compilerArgs('windows', {});
  assert.ok(bare.includes('-DCMAKE_CXX_COMPILER=clang-cl'));
  assert.ok(bare.includes('-DCMAKE_BUILD_TYPE=RelWithDebInfo'));
  const named = desktop.compilerArgs('windows', {CC: 'cl', CXX: 'cl'});
  assert.ok(!named.some(arg => arg.startsWith('-DCMAKE_CXX_COMPILER')));
  assert.ok(named.includes('-DCMAKE_BUILD_TYPE=RelWithDebInfo'));

  // vcpkg is chosen by whether its packages are installed, not by VCPKG_ROOT:
  // vcvars64.bat points that at Visual Studio's empty copy, and the one with
  // glog in it is elsewhere.
  const home = scratch();
  const empty = path.join(home, 'vs-bundled-vcpkg');
  fs.mkdirSync(path.join(empty, 'scripts', 'buildsystems'), {recursive: true});
  fs.writeFileSync(path.join(empty, 'scripts', 'buildsystems', 'vcpkg.cmake'), '');
  const real = path.join(home, 'Tools', 'vcpkg');
  fs.mkdirSync(path.join(real, 'scripts', 'buildsystems'), {recursive: true});
  fs.writeFileSync(path.join(real, 'scripts', 'buildsystems', 'vcpkg.cmake'), '');
  fs.mkdirSync(path.join(real, 'installed', 'x64-windows', 'include', 'glog'), {recursive: true});
  fs.writeFileSync(path.join(real, 'installed', 'x64-windows', 'include', 'glog', 'logging.h'), '');

  const toolchain = desktop
    .compilerArgs('windows', {VCPKG_ROOT: empty, USERPROFILE: home})
    .find(arg => arg.startsWith('-DCMAKE_TOOLCHAIN_FILE='));
  assert.equal(
    toolchain,
    `-DCMAKE_TOOLCHAIN_FILE=${path.join(real, 'scripts', 'buildsystems', 'vcpkg.cmake').split(path.sep).join('/')}`,
  );

  fs.rmSync(home, {recursive: true, force: true});
});

test('Git Bash is found beside git, and a WSL launcher is never it', () => {
  const root = scratch();
  const make = file => {
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, '');
    return file;
  };

  // Git for Windows: git.exe in <Git>\cmd, bash.exe in <Git>\bin.
  const gitCmd = path.join(root, 'Git', 'cmd');
  make(path.join(gitCmd, 'git.exe'));
  const bash = make(path.join(root, 'Git', 'bin', 'bash.exe'));
  // WSL's launcher, first on PATH the way it is on a real machine.
  const system32 = path.join(root, 'Windows', 'System32');
  make(path.join(system32, 'bash.exe'));

  const found = desktop.findGitBash({PATH: [system32, gitCmd].join(';')});
  assert.equal(found, path.normalize(bash));

  // BASALT_BASH wins, but not if it names a launcher.
  assert.equal(desktop.findGitBash({BASALT_BASH: bash, PATH: ''}), bash);
  assert.equal(
    desktop.findGitBash({BASALT_BASH: path.join(system32, 'bash.exe'), PATH: gitCmd}),
    null,
  );

  // Nothing but a launcher: no answer, rather than the wrong one.
  assert.equal(desktop.findGitBash({PATH: system32}), null);

  fs.rmSync(root, {recursive: true, force: true});
});

test('the environment vcvars prints is read as NAME=value lines only', () => {
  const parsed = desktop.parseSetOutput(
    '**********\r\n' +
      'INCLUDE=C:\SDK\include;C:\VC\include\r\n' +
      'ProgramFiles(x86)=C:\Program Files (x86)\r\n' +
      'EMPTY=\r\n' +
      '=C:=C:\\r\n',
  );
  assert.equal(parsed.INCLUDE, 'C:\SDK\include;C:\VC\include');
  assert.equal(parsed['ProgramFiles(x86)'], 'C:\Program Files (x86)');
  assert.equal(parsed.EMPTY, '');
  // cmd's hidden per-drive variables start with "=", and are not variables a
  // child process can be given.
  assert.ok(!Object.keys(parsed).some(name => name.startsWith('=')));
});

test('a Metro that dies on start is reported at once, with its own error', async () => {
  const {startMetro} = require(path.join(DIST, 'cli/metro.js'));
  const project = scratch();

  // A stand-in for react-native's cli.js that fails the way Metro does in an
  // Expo app with no @react-native/metro-config: one error line, then exit.
  const reactNative = path.join(project, 'node_modules', 'react-native');
  fs.mkdirSync(reactNative, {recursive: true});
  fs.writeFileSync(
    path.join(reactNative, 'cli.js'),
    "console.error('error Cannot resolve `@react-native/metro-config`.'); process.exit(1);\n",
  );

  // A port nothing listens on, so only the child's exit can end the wait.
  const port = await new Promise(resolve => {
    const server = require('node:net').createServer();
    server.listen(0, () => {
      const {port: free} = server.address();
      server.close(() => resolve(free));
    });
  });

  const started = Date.now();
  let thrown = null;
  try {
    await startMetro({root: project, reactNativePath: reactNative}, port);
  } catch (error) {
    thrown = error;
  }
  const elapsed = Date.now() - started;

  assert.ok(thrown, 'startMetro should reject when Metro exits');
  assert.ok(elapsed < 15000, `reported after ${elapsed}ms, not at once`);
  assert.match(thrown.message, /exited before listening/);
  assert.match(thrown.message, /Cannot resolve `@react-native\/metro-config`/);

  fs.rmSync(project, {recursive: true, force: true});
});

test('a development run names the Metro config to install before starting Metro', () => {
  const project = scratch();
  const install = (name, version) => {
    const dir = path.join(project, 'node_modules', ...name.split('/'));
    fs.mkdirSync(dir, {recursive: true});
    fs.writeFileSync(path.join(dir, 'package.json'), JSON.stringify({name, version}));
  };

  // What an Expo app has: react-native, and no @react-native/metro-config.
  install('react-native', '0.86.3');
  const message = desktop.missingMetroConfig(project);
  assert.ok(
    message.includes('npm install --save-dev @react-native/metro-config@0.86.3'),
    message,
  );
  assert.ok(message.includes('--mode release'));

  install('@react-native/metro-config', '0.86.3');
  assert.equal(desktop.missingMetroConfig(project), null);

  fs.rmSync(project, {recursive: true, force: true});
});

// ---------------------------------------------------------------------------
// Packaging: turning the host binary into what each desktop calls an app
// ---------------------------------------------------------------------------

const packageApp = require(
  path.join(DIST, 'cli/packageApp.js'),
);

/** A throwaway project directory with the given files in it. */
function projectWith(files) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-package-'));
  for (const [name, contents] of Object.entries(files)) {
    fs.writeFileSync(path.join(root, name), contents);
  }
  return root;
}

test('an app that names itself is not given a derived identifier', () => {
  const root = projectWith({
    'app.json': JSON.stringify({
      name: 'demo',
      displayName: 'The Demo',
      basalt: {identifier: 'com.example.demo', scheme: 'demo'},
    }),
  });
  const config = packageApp.readAppConfig(root);
  assert.strictEqual(config.name, 'The Demo');
  assert.strictEqual(config.identifier, 'com.example.demo');
  assert.deepStrictEqual(config.schemes, ['demo']);
});

test('an app that names nothing still gets a usable identifier', () => {
  // Derived rather than absent, and deliberately recognisable: an app shipping
  // as com.basalt.<slug> should be able to tell that nobody chose it.
  const root = projectWith({'app.json': JSON.stringify({name: 'My App'})});
  const config = packageApp.readAppConfig(root);
  assert.strictEqual(config.identifier, 'com.basalt.my-app');
  assert.deepStrictEqual(config.schemes, []);
});

test('the Info.plist carries the identity macOS needs to notify', () => {
  const config = {
    name: 'The Demo',
    identifier: 'com.example.demo',
    schemes: ['demo', 'demo2'],
    version: '2.1.0',
  };
  const plist = packageApp.infoPlist(config, 'basalt_appkit');
  // The identifier is the whole point: without one UNUserNotificationCenter
  // raises rather than failing. See appkit/AppKitNotifications.mm.
  assert.match(plist, /<key>CFBundleIdentifier<\/key>\s*<string>com\.example\.demo<\/string>/);
  assert.match(plist, /<key>CFBundleExecutable<\/key>\s*<string>basalt_appkit<\/string>/);
  assert.match(plist, /<string>2\.1\.0<\/string>/);
  // Both schemes, so a link opens the app.
  assert.match(plist, /<string>demo<\/string>/);
  assert.match(plist, /<string>demo2<\/string>/);
});

test('a name with an ampersand in it does not produce broken XML', () => {
  const plist = packageApp.infoPlist(
    {name: 'Ben & Co', identifier: 'com.example.benco', schemes: [], version: '1.0.0'},
    'basalt_appkit',
  );
  assert.match(plist, /<string>Ben &amp; Co<\/string>/);
  assert.doesNotMatch(plist, /<string>Ben & Co<\/string>/);
});

test('packaging for macOS returns the executable inside the bundle', {
  skip: process.platform !== 'darwin' ? 'needs macOS, for codesign' : false,
}, () => {
  const root = projectWith({'app.json': JSON.stringify({name: 'demo'})});
  const binary = path.join(root, 'basalt_appkit');
  fs.writeFileSync(binary, '#!/bin/sh\nexit 0\n');
  fs.chmodSync(binary, 0o755);

  const out = path.join(root, 'build');
  const result = packageApp.packageApp({
    platform: 'macos',
    hostBinary: binary,
    outputDir: out,
    projectRoot: root,
  });

  // What is launched has to be the copy *inside* the bundle: NSBundle.mainBundle
  // comes from where the executable sits, so running the original would be
  // running an unbundled process with a bundle sitting beside it.
  assert.strictEqual(
    result.launchPath,
    path.join(out, 'demo.app', 'Contents', 'MacOS', 'basalt_appkit'),
  );
  assert.ok(fs.existsSync(path.join(out, 'demo.app', 'Contents', 'Info.plist')));
  assert.ok(fs.existsSync(result.launchPath));
});

test('packaging for Linux writes a desktop entry and launches the binary itself', () => {
  const root = projectWith({
    'app.json': JSON.stringify({name: 'demo', basalt: {identifier: 'com.example.demo', scheme: 'demo'}}),
  });
  const binary = path.join(root, 'basalt_gtk');
  fs.writeFileSync(binary, '');

  const out = path.join(root, 'build');
  const result = packageApp.packageApp({
    platform: 'linux',
    hostBinary: binary,
    outputDir: out,
    projectRoot: root,
  });

  // Unlike macOS, nothing about where the binary sits decides anything, so the
  // original is what runs.
  assert.strictEqual(result.launchPath, binary);
  const entry = fs.readFileSync(result.desktopPath, 'utf8');
  assert.match(entry, /^Name=demo$/m);
  // Compared as a string rather than as a pattern: a temporary directory's path
  // is full of backslashes on Windows, and a RegExp built from one is a regex
  // full of escapes. This test runs in the Windows job too, because the CLI is
  // one command with three names.
  assert.ok(
    entry.split('\n').includes(`Exec=${binary} %U`),
    `no Exec line for ${binary} in:\n${entry}`,
  );
  // The scheme, which is what makes a link open the app.
  assert.match(entry, /^MimeType=x-scheme-handler\/demo;$/m);
});

test('packaging for Windows changes nothing, because the host does it', () => {
  const root = projectWith({'app.json': JSON.stringify({name: 'demo'})});
  const binary = path.join(root, 'basalt_win32.exe');
  fs.writeFileSync(binary, '');
  const result = packageApp.packageApp({
    platform: 'windows',
    hostBinary: binary,
    outputDir: path.join(root, 'build'),
    projectRoot: root,
  });
  // A Start Menu shortcut carrying an AppUserModelID needs IPropertyStore,
  // which is COM; win32/Win32Packaging.h does it at startup instead.
  assert.strictEqual(result.launchPath, binary);
  assert.strictEqual(result.appPath, undefined);
  assert.strictEqual(result.desktopPath, undefined);
});

// --- Capability packages ----------------------------------------------------
//
// A package that ships native code says so in its own manifest, and the build
// reads that rather than the CLI knowing its name. This is the mechanism that
// lets a capability live outside core; see
// openspec/changes/split-optional-capabilities-into-packages.

function appWith(dependencies, packages) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-packages-'));
  fs.writeFileSync(
    path.join(root, 'package.json'),
    JSON.stringify({name: 'an-app', dependencies}),
  );
  for (const [name, manifest] of Object.entries(packages)) {
    const dir = path.join(root, 'node_modules', name);
    fs.mkdirSync(dir, {recursive: true});
    fs.writeFileSync(path.join(dir, 'package.json'), JSON.stringify(manifest));
    if (typeof manifest?.basalt?.native === 'string' && manifest.present !== false) {
      const target = path.join(dir, manifest.basalt.native);
      fs.mkdirSync(path.dirname(target), {recursive: true});
      fs.writeFileSync(target, '# native\n');
    }
  }
  return root;
}

test('a package declaring native code is discovered by its manifest', () => {
  const root = appWith(
    {'basalt-notifications': '*', 'left-pad': '*'},
    {
      'basalt-notifications': {
        name: 'basalt-notifications',
        basalt: {native: 'native/CMakeLists.txt'},
      },
      'left-pad': {name: 'left-pad'},
    },
  );
  const found = desktop.capabilityPackages(root);
  assert.equal(found.length, 1, 'only the package declaring native code');
  assert.ok(found[0].endsWith('basalt-notifications'));
});

// A capability package compiles into the same binary, so its compiler errors
// arrive looking exactly like the platform's own -- a path, in a node_modules
// directory nobody was thinking about. The build cannot say which package
// broke it, because the output is streamed rather than captured, but it can
// say what was in it.
test('a failed build names the packages that contributed native code', () => {
  const root = appWith(
    {'basalt-notifications': '*'},
    {
      'basalt-notifications': {
        name: 'basalt-notifications',
        basalt: {native: 'native/CMakeLists.txt'},
      },
    },
  );

  const explained = desktop.explainContributedFailure(root, new Error('cmake --build failed'));
  assert.match(explained.message, /cmake --build failed/, 'the original error survives');
  assert.match(explained.message, /basalt-notifications/);
  assert.match(explained.message, /1 contributor:/, 'singular for one');
});

test('a failed build with no contributing packages says nothing extra', () => {
  const root = appWith({'left-pad': '*'}, {'left-pad': {name: 'left-pad'}});
  const original = new Error('cmake --build failed');

  // The same error object, not a copy that happens to read the same: there is
  // nothing to add, and wrapping it would lose its stack for no reason.
  assert.equal(desktop.explainContributedFailure(root, original), original);
});

test('a package that declares native code and has none fails the build early', () => {
  // Rather than a host quietly built without the capability, which fails later
  // and further away -- at `requireNativeModule`, in JavaScript, at runtime.
  const root = appWith(
    {'basalt-core-ghost': '*'},
    {
      'basalt-core-ghost': {
        name: 'basalt-core-ghost',
        basalt: {native: 'native/CMakeLists.txt'},
        present: false,
      },
    },
  );
  assert.throws(() => desktop.capabilityPackages(root), /does not exist/);
});

test('a transitive dependency does not contribute native code on its own', () => {
  // Installed, declaring native code, and not asked for by the app. Compiling
  // C++ into the host binary is not something a dependency of a dependency
  // should be able to arrange.
  const root = appWith(
    {'left-pad': '*'},
    {
      'left-pad': {name: 'left-pad'},
      'basalt-core-sneaky': {
        name: 'basalt-core-sneaky',
        basalt: {native: 'native/CMakeLists.txt'},
      },
    },
  );
  assert.deepEqual(desktop.capabilityPackages(root), []);
});

// --- The app's own native modules -------------------------------------------
//
// `<projectRoot>/modules/*` is where Expo's autolinking looks for a module the
// app wrote and never published. A desktop half there is declared by being
// there, because a local module has no manifest to declare it in.

function appWithLocalModules(modules) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-local-modules-'));
  fs.writeFileSync(path.join(root, 'package.json'), JSON.stringify({name: 'an-app'}));
  for (const [name, files] of Object.entries(modules)) {
    for (const file of files) {
      const target = path.join(root, 'modules', name, file);
      fs.mkdirSync(path.dirname(target), {recursive: true});
      fs.writeFileSync(target, '# native\n');
    }
  }
  return root;
}

test("a local module with a native/CMakeLists.txt is the app's own capability", () => {
  const root = appWithLocalModules({
    'kino-process': ['expo-module.config.json', 'native/CMakeLists.txt'],
  });
  assert.deepEqual(desktop.localNativeModules(root), [
    path.join(root, 'modules', 'kino-process'),
  ]);
});

test('a local module with no desktop half contributes nothing', () => {
  // The shape every Expo local module starts as: an iOS half, an Android half,
  // and nothing for a desktop. It must not break the build, and it must not be
  // claimed as contributing either.
  const root = appWithLocalModules({
    'kino-audio': ['expo-module.config.json', 'ios/KinoAudioModule.swift'],
  });
  assert.deepEqual(desktop.localNativeModules(root), []);
});

test('an app with no modules directory is not an error', () => {
  const root = appWithLocalModules({});
  assert.deepEqual(desktop.localNativeModules(root), []);
});

test("the app's own modules are configured alongside its packages", () => {
  // Both lists reach CMake through the one -D, because the host's loop over
  // BASALT_PACKAGES does not care which of the two a directory came from.
  const root = appWithLocalModules({
    'kino-process': ['native/CMakeLists.txt'],
    'kino-audio': ['native/CMakeLists.txt'],
  });
  const packages = desktop.optionalNativeModules(root, 'macos').args.filter(arg =>
    arg.startsWith('-DBASALT_PACKAGES='),
  );
  assert.equal(packages.length, 1);
  // Sorted, so that two machines configure the same build: readdir order is the
  // filesystem's and CMake compiles in the order it is given.
  assert.equal(
    packages[0],
    `-DBASALT_PACKAGES=${[
      path.join(root, 'modules', 'kino-audio'),
      path.join(root, 'modules', 'kino-process'),
    ]
      .map(dir => dir.split(path.sep).join('/'))
      .join(';')}`,
  );
});

test("a compiler error names the app's own module as a contributor", () => {
  // The whole point of the note: an error with `modules/kino-process` in the
  // path reads exactly like one from the platform itself.
  const root = appWithLocalModules({'kino-process': ['native/CMakeLists.txt']});
  const explained = desktop.explainContributedFailure(root, new Error('cmake --build failed'));
  assert.match(explained.message, /kino-process/);
});

// --------------------------------------------------------------------------
// `init`
// --------------------------------------------------------------------------
//
// The command that puts this package into somebody else's app, which is the
// one place a mistake lands in a repository that is not ours. Run against a
// real temporary directory rather than a mocked filesystem: what is being
// tested is what it writes.

const init = require(path.join(DIST, 'cli/init.js')).init;

function scratchApp(contents) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-init-'));
  for (const [name, body] of Object.entries(contents)) {
    fs.writeFileSync(path.join(dir, name), body);
  }
  return dir;
}

const EXPO_APP = {
  'package.json': JSON.stringify({
    name: 'my-app',
    version: '1.0.0',
    dependencies: {expo: '^54.0.0', 'react-native': '0.87.1'},
  }),
  'metro.config.js':
    "const {getDefaultConfig} = require('expo/metro-config');\n\n" +
    'const config = getDefaultConfig(__dirname);\n\n' +
    'module.exports = config;\n',
};

function manifestOf(dir) {
  return JSON.parse(fs.readFileSync(path.join(dir, 'package.json'), 'utf8'));
}

test('init configures an app that has none', () => {
  const dir = scratchApp(EXPO_APP);
  const result = init(dir);
  assert.equal(result.ok, true);

  const manifest = manifestOf(dir);
  assert.ok(manifest.dependencies['basalt-core'] != null);
  assert.ok(manifest.devDependencies['@react-native/metro-config'] != null);
  assert.ok(manifest.devDependencies['@react-native-community/cli'] != null);
  for (const platform of ['linux', 'macos', 'windows']) {
    assert.equal(manifest.scripts[platform], `react-native run-${platform}`);
  }

  const metro = fs.readFileSync(path.join(dir, 'metro.config.js'), 'utf8');
  assert.match(metro, /withDesktopPlatforms/);
  assert.match(metro, /module\.exports = withDesktopPlatforms\(config\);/);
  // What was already in the file is still in it, and so is its last newline:
  // a command that reformats a file it was asked to edit one line of is a
  // command people stop trusting.
  assert.match(metro, /expo\/metro-config/);
  assert.ok(metro.endsWith('\n'));
});

test('init pins itself to a real version, from either layout', () => {
  const dir = scratchApp(EXPO_APP);
  init(dir);
  // Not '*'. It reads its own package.json by walking up, because this file
  // runs from dist/cli once built and cli/ in a checkout, and any fixed number
  // of `..` is wrong in one of the two.
  assert.match(manifestOf(dir).dependencies['basalt-core'], /^\^\d/);
});

test('init changes nothing the second time', () => {
  const dir = scratchApp(EXPO_APP);
  init(dir);
  const after = fs.readFileSync(path.join(dir, 'metro.config.js'), 'utf8');
  const manifest = JSON.stringify(manifestOf(dir));

  const again = init(dir);
  assert.equal(again.ok, true);
  assert.ok(again.steps.every(([, step]) => step.state === 'done'));
  assert.equal(fs.readFileSync(path.join(dir, 'metro.config.js'), 'utf8'), after);
  assert.equal(JSON.stringify(manifestOf(dir)), manifest);
});

test('init refuses a directory that is not an app, and writes nothing', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-init-'));
  const result = init(dir);
  assert.equal(result.ok, false);
  assert.match(result.reason, /no package\.json/);
  assert.deepEqual(fs.readdirSync(dir), []);
});

test('init refuses an app that is not a React Native one', () => {
  const dir = scratchApp({
    'package.json': JSON.stringify({name: 'x', version: '1.0.0', dependencies: {lodash: '*'}}),
  });
  const result = init(dir);
  assert.equal(result.ok, false);
  assert.match(result.reason, /neither react-native nor expo/);
  // Untouched, rather than half-configured.
  assert.equal(manifestOf(dir).devDependencies, undefined);
});

test('init reports a metro config it cannot safely edit', () => {
  const dir = scratchApp({
    'package.json': JSON.stringify({
      name: 'x',
      version: '1.0.0',
      dependencies: {'react-native': '0.87.1'},
    }),
    'metro.config.ts': 'module.exports = {};\n',
  });
  const result = init(dir);
  assert.equal(result.ok, true);
  const [name, step] = result.steps.find(([label]) => label === 'metro config');
  assert.equal(name, 'metro config');
  assert.equal(step.state, 'blocked');
  assert.match(step.message, /metro\.config\.ts/);
  // And it did not rewrite the TypeScript config it just said it could not edit.
  assert.equal(fs.readFileSync(path.join(dir, 'metro.config.ts'), 'utf8'), 'module.exports = {};\n');
});

// An app with no metro.config.js at all, which is what a stock
// `create-expo-app --template blank` is: Expo's default is implicit, and an
// app only grows the file when it has something to say. Every fixture above
// hands init a config to edit, which is how the command came to report by
// hand the single step it exists to do -- found by running it against a real
// create-expo-app rather than by reading it.
test('init writes a metro config for an app that has none', () => {
  const dir = scratchApp({
    'package.json': JSON.stringify({
      name: 'my-app',
      version: '1.0.0',
      dependencies: {expo: '^54.0.0'},
    }),
  });
  const result = init(dir);
  assert.equal(result.ok, true);

  const [, step] = result.steps.find(([label]) => label === 'metro config');
  assert.equal(step.state, 'changed');

  const metro = fs.readFileSync(path.join(dir, 'metro.config.js'), 'utf8');
  assert.match(metro, /withDesktopPlatforms/);
  assert.match(metro, /module\.exports = withDesktopPlatforms\(getDefaultConfig\(__dirname\)\);/);
  // Expo's defaults, because this app is an Expo app: expo/metro-config reads
  // app.json and the Expo plugins, and @react-native/metro-config does not.
  assert.match(metro, /require\('expo\/metro-config'\)/);
  assert.ok(metro.endsWith('\n'));
});

test('init writes a bare React Native app the React Native defaults', () => {
  const dir = scratchApp({
    'package.json': JSON.stringify({
      name: 'my-app',
      version: '1.0.0',
      dependencies: {'react-native': '0.87.1'},
    }),
  });
  init(dir);

  const metro = fs.readFileSync(path.join(dir, 'metro.config.js'), 'utf8');
  assert.match(metro, /require\('@react-native\/metro-config'\)/);
  // Not `/expo/`, which `module.exports` contains.
  assert.doesNotMatch(metro, /expo\/metro-config/);
});

// And what it writes is what it can read back: a second run reports the
// config as already wrapped rather than writing a second one.
test('init leaves the metro config it wrote alone', () => {
  const dir = scratchApp({
    'package.json': JSON.stringify({
      name: 'my-app',
      version: '1.0.0',
      dependencies: {expo: '^54.0.0'},
    }),
  });
  init(dir);
  const written = fs.readFileSync(path.join(dir, 'metro.config.js'), 'utf8');

  const again = init(dir);
  const [, step] = again.steps.find(([label]) => label === 'metro config');
  assert.equal(step.state, 'done');
  assert.equal(fs.readFileSync(path.join(dir, 'metro.config.js'), 'utf8'), written);
});

// `npx basalt-core init` is how the README spells it, and `init` is
// the verb rather than the directory to configure. The command used to
// resolve ./init, find no package.json there, and refuse to configure the app
// it was standing in -- which is every invocation the documentation gives.
test('the init verb is not mistaken for a directory', () => {
  const dir = scratchApp(EXPO_APP);
  const cli = path.join(DIST, 'cli/init.js');
  const run = (args) =>
    require('node:child_process').spawnSync(process.execPath, [cli, ...args], {
      cwd: dir,
      encoding: 'utf8',
    });

  const withVerb = run(['init']);
  assert.equal(withVerb.status, 0, withVerb.stderr);
  assert.match(withVerb.stdout, /metro config/);

  // And a directory argument still works, which is what the verb must not
  // have taken away.
  const elsewhere = scratchApp(EXPO_APP);
  const withPath = run(['init', elsewhere]);
  assert.equal(withPath.status, 0, withPath.stderr);
  assert.match(
    fs.readFileSync(path.join(elsewhere, 'metro.config.js'), 'utf8'),
    /withDesktopPlatforms/,
  );
});

// The host packages carry the `run-<desktop>` commands -- React Native's CLI
// reads a react-native.config.js out of every dependency, and they live beside
// the host they need rather than in core. So an app given core alone has no
// desktop commands at all, while `init` signs off by telling the person to run
// one. Found by installing the tarballs by hand during verification, which is
// exactly what hid it.
test('init adds a host package per desktop', () => {
  const dir = scratchApp(EXPO_APP);
  init(dir);

  const manifest = manifestOf(dir);
  for (const host of [
    'basalt-gtk',
    'basalt-appkit',
    'basalt-win32',
  ]) {
    assert.ok(manifest.dependencies[host] != null, `${host} is a dependency`);
  }
  // The same version as core: they share a C++ ABI with the host.
  assert.equal(
    manifest.dependencies['basalt-gtk'],
    manifest.dependencies['basalt-core'],
  );
});

// All three by default, because package.json is committed and which desktops
// an app builds for is the project's business rather than that of whoever ran
// the command. An app that wants fewer says so.
test('app.json narrows which desktops are installed', () => {
  const dir = scratchApp({
    ...EXPO_APP,
    'app.json': JSON.stringify({name: 'my-app', basalt: {desktops: ['macos']}}),
  });
  init(dir);

  const manifest = manifestOf(dir);
  assert.ok(manifest.dependencies['basalt-appkit'] != null);
  assert.equal(manifest.dependencies['basalt-gtk'], undefined);
  assert.equal(manifest.dependencies['basalt-win32'], undefined);
});

// A typo would otherwise install one package fewer and fail much later, at the
// command that is missing -- the failure this step exists to prevent.
test('a misspelled desktop is reported rather than ignored', () => {
  const dir = scratchApp({
    ...EXPO_APP,
    'app.json': JSON.stringify({name: 'my-app', basalt: {desktops: ['mac']}}),
  });
  const result = init(dir);

  const found = result.steps.find(([label]) => label === 'desktops');
  assert.ok(found != null, 'the bad field is reported');
  assert.equal(found[1].state, 'blocked');
  assert.match(found[1].message, /mac/);

  // And it fell back to all three rather than to none, so the app is still
  // configured while the person fixes the field.
  assert.ok(manifestOf(dir).dependencies['basalt-gtk'] != null);
});

// `doctor`
//
// The machine checks take their "is this program here" question as a
// parameter, so the interesting cases -- a missing cmake, a Linux box with no
// GTK headers -- are testable from a Mac. Asking the real PATH would make
// these tests say different things on different machines, which is the one
// thing a test of a diagnostic must not do.

const doctorCli = require(path.join(DIST, 'cli/doctor.js'));

const VERSIONS = path.join(
  __dirname,
  '..',
  'packages',
  'basalt-core',
  'supported-versions.json',
);

function stateOf(steps, name) {
  const found = steps.find(([label]) => label === name);
  return found == null ? null : found[1].state;
}

test('doctor reports a missing build tool and does not pass it', () => {
  const steps = doctorCli.checkBuildTools(program => program !== 'ninja');
  assert.equal(stateOf(steps, 'cmake'), 'done');
  assert.equal(stateOf(steps, 'ninja'), 'blocked');
  const [, ninja] = steps.find(([name]) => name === 'ninja');
  assert.match(ninja.message, /ninja-build/, 'says how to install it');
});

test('doctor does not claim a desktop it cannot check', () => {
  const steps = doctorCli.checkDesktop('macos', ['linux', 'macos', 'windows'], () => true);
  // Neither passing nor failing: a Mac cannot answer for GTK's headers, and
  // saying either would be worse than saying nothing.
  assert.equal(stateOf(steps, 'linux'), 'unchecked');
  assert.equal(stateOf(steps, 'windows'), 'unchecked');
  assert.equal(stateOf(steps, 'command line tools'), 'done');
});

test('doctor checks GTK on Linux and says how to get it', () => {
  const steps = doctorCli.checkDesktop('linux', ['linux'], () => false);
  assert.equal(stateOf(steps, 'gtk4'), 'blocked');
  const [, gtk] = steps.find(([name]) => name === 'gtk4');
  assert.match(gtk.message, /libgtk-4-dev/);
  assert.equal(stateOf(steps, 'clang'), 'blocked');
});

test('doctor reports an unsupported React Native with what is supported', () => {
  const dir = scratchApp({'package.json': JSON.stringify({name: 'x'})});
  fs.mkdirSync(path.join(dir, 'node_modules', 'react-native'), {recursive: true});
  fs.writeFileSync(
    path.join(dir, 'node_modules', 'react-native', 'package.json'),
    JSON.stringify({name: 'react-native', version: '0.71.0'}),
  );

  const outcome = doctorCli.checkReactNative(dir, VERSIONS);
  assert.equal(outcome.state, 'blocked');
  assert.match(outcome.message, /0\.71\.0/, 'the version it found');
  assert.match(outcome.message, /0\.87/, 'and one that is supported');
});

test('doctor accepts a React Native that is supported', () => {
  const dir = scratchApp({'package.json': JSON.stringify({name: 'x'})});
  fs.mkdirSync(path.join(dir, 'node_modules', 'react-native'), {recursive: true});
  fs.writeFileSync(
    path.join(dir, 'node_modules', 'react-native', 'package.json'),
    JSON.stringify({name: 'react-native', version: '0.87.1'}),
  );

  assert.equal(doctorCli.checkReactNative(dir, VERSIONS).state, 'done');
});

test('doctor names a host package that is a dependency but not installed', () => {
  const dir = scratchApp(EXPO_APP);
  const steps = doctorCli.checkHostPackages(dir, ['macos']);
  assert.equal(stateOf(steps, 'basalt-appkit'), 'blocked');
  const [, host] = steps[0];
  assert.match(host.message, /run-macos/, 'says what is missing, not just what');
});

test('doctor changes nothing in an app it has never configured', () => {
  const dir = scratchApp({
    'package.json': JSON.stringify({name: 'x', dependencies: {expo: '^54.0.0'}}),
  });
  const before = fs.readFileSync(path.join(dir, 'package.json'), 'utf8');

  const result = doctorCli.doctor(dir, VERSIONS);
  assert.equal(result.ok, false, 'an app with nothing installed is not ready');
  assert.equal(fs.readFileSync(path.join(dir, 'package.json'), 'utf8'), before);
  assert.ok(!fs.existsSync(path.join(dir, 'metro.config.js')), 'and wrote no config');

  // And what it would do is phrased as such, because a read-only command that
  // reports "added" is the one thing it must never say.
  const [, deps] = result.steps.find(([name]) => name === 'dependencies');
  assert.match(deps.message, /would add/);
});

test('doctor refuses a directory that is not an app', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-doctor-'));
  const result = doctorCli.doctor(dir, VERSIONS);
  assert.equal(result.ok, false);
  assert.match(result.reason, /no package.json/);
});
