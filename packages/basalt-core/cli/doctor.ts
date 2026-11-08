// What a build is going to need, said before the build needs it.
//
// A first desktop build compiles Hermes and React Native's C++ from source. It
// takes tens of minutes, and every prerequisite it is missing is discovered at
// the moment it is reached rather than at the start: no cmake, no ninja, GTK's
// development headers absent, vcpkg not set up, a React Native nothing here
// supports. Each arrives as a CMake or compiler error naming a path, twenty
// minutes in, to somebody who has not seen this build before.
//
// So this asks the same questions first, and cheaply. It is `init` with its
// hands behind its back -- the same steps, reported the same way, writing
// nothing -- plus the checks `init` has no fix for, which are the expensive
// ones.
//
// ## What it will not do
//
// Claim a desktop it cannot see. Whether GTK's headers are installed is not a
// question a Mac can answer, and reporting "fine" for a platform this machine
// cannot check would make the command worse than not running it. Those report
// as unchecked, with the reason.
//
// ## Where the list comes from
//
// getting-started's prerequisites are the list a reader is given, CONTRIBUTING
// holds the same one per packager, and release.yml's install proof runs the
// documented command and nothing else -- so a package missing from the docs
// fails a release rather than somebody's first build. These checks encode the
// same list a third time; if it grows there and not here, this command gets
// quieter rather than wrong, which is the failure worth knowing about.

import {execFileSync} from 'node:child_process';
import * as fs from 'node:fs';
import * as path from 'node:path';

import {init, HOST_PACKAGES, desktopsFor} from './init';
import {BLOCKED, CHANGED, DONE, UNCHECKED, report, step} from './steps';
import type {NamedStep, Step} from './steps';

export type DoctorResult = {
  ok: boolean;
  /** Absent only when the directory is not an app at all. */
  reason?: string;
  steps: NamedStep[];
};

/** Whether a program answers on this machine, without caring what it says. */
function onPath(program: string, args: string[] = ['--version']): boolean {
  try {
    execFileSync(program, args, {stdio: 'ignore'});
    return true;
  } catch {
    return false;
  }
}

/** The first line a program prints, trimmed, or null if it did not run. */
function versionOf(program: string, args: string[] = ['--version']): string | null {
  try {
    const out = execFileSync(program, args, {encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore']});
    return out.split('\n')[0]?.trim() ?? null;
  } catch {
    return null;
  }
}

/** The desktop this machine builds for, or null where it builds for none. */
export function currentDesktop(platform: string = process.platform): string | null {
  if (platform === 'linux') return 'linux';
  if (platform === 'darwin') return 'macos';
  if (platform === 'win32') return 'windows';
  return null;
}

/**
 * The React Native the app has, against the ones this platform is tested on.
 *
 * `supported-versions.json` is the same file bootstrap reads before it
 * downloads anything, and it refuses an unsupported version in seconds. This
 * asks the same question earlier still, and reports rather than refuses --
 * nothing has been downloaded yet, so there is nothing to protect.
 */
export function checkReactNative(projectRoot: string, versionsFile: string): Step {
  const installed = path.join(projectRoot, 'node_modules', 'react-native', 'package.json');
  if (!fs.existsSync(installed)) {
    return step(BLOCKED, 'react-native is not installed; run your package manager first');
  }

  let version: string;
  try {
    version = JSON.parse(fs.readFileSync(installed, 'utf8')).version;
  } catch {
    return step(BLOCKED, `could not read a version out of ${installed}`);
  }

  let table;
  try {
    table = JSON.parse(fs.readFileSync(versionsFile, 'utf8'));
  } catch {
    return step(BLOCKED, `could not read ${path.basename(versionsFile)}`);
  }

  const minor = version.split('.').slice(0, 2).join('.');
  const supported: string[] = (table.supported ?? []).map(
    (entry: {reactNative: string}) => entry.reactNative,
  );
  if (supported.includes(minor)) {
    return step(DONE, `react-native ${version}`);
  }
  if (table.development?.reactNative === version) {
    return step(DONE, `react-native ${version} (the development version)`);
  }
  return step(
    BLOCKED,
    `react-native ${version} is not supported. Supported: ${supported.join(', ')}. ` +
      'See supported-versions.json for why a version is listed rather than a range.',
  );
}

/** cmake and ninja, which every desktop's build needs. */
export function checkBuildTools(
  present: (program: string) => boolean = onPath,
): NamedStep[] {
  const steps: NamedStep[] = [];
  for (const [tool, how] of [
    ['cmake', 'apt install cmake, or brew install cmake'],
    ['ninja', 'apt install ninja-build, or brew install ninja'],
  ] as const) {
    // ninja answers to --version and is spelled ninja-build by apt but ninja
    // on the path, which is why this asks the path rather than the packager.
    steps.push([
      tool,
      present(tool) ? step(DONE, versionOf(tool) ?? 'present') : step(BLOCKED, `not installed. ${how}`),
    ]);
  }
  return steps;
}

/**
 * What the desktop this machine builds for needs, beyond cmake and ninja.
 *
 * One entry per desktop, and only ever the current one: see the header.
 */
export function checkDesktop(
  desktop: string | null,
  wanted: readonly string[],
  present: (program: string, args?: string[]) => boolean = onPath,
): NamedStep[] {
  const steps: NamedStep[] = [];

  for (const other of wanted) {
    if (other === desktop) {
      continue;
    }
    steps.push([
      other,
      step(UNCHECKED, `not checked from ${desktop ?? 'a machine with no desktop'}`),
    ]);
  }

  if (desktop == null) {
    return steps;
  }
  if (!wanted.includes(desktop)) {
    // The app narrowed itself to desktops that do not include this one, which
    // is allowed and worth saying: nothing here can be built or run.
    steps.push([
      desktop,
      step(UNCHECKED, "this machine's desktop is not in app.json's basalt.desktops"),
    ]);
    return steps;
  }

  if (desktop === 'linux') {
    steps.push([
      'gtk4',
      present('pkg-config', ['--exists', 'gtk4'])
        ? step(DONE, versionOf('pkg-config', ['--modversion', 'gtk4']) ?? 'present')
        : step(BLOCKED, "GTK 4's development headers are missing. apt install libgtk-4-dev"),
    ]);
    steps.push([
      'clang',
      present('clang')
        ? step(DONE, versionOf('clang') ?? 'present')
        : step(
            BLOCKED,
            'not installed. apt install clang -- React Native builds -Werror and is a ' +
              'clang codebase; see docs/DECISIONS.md',
          ),
    ]);
  }

  if (desktop === 'macos') {
    steps.push([
      'command line tools',
      present('xcrun', ['--show-sdk-path'])
        ? step(DONE, versionOf('xcrun', ['--show-sdk-version']) ?? 'present')
        : step(BLOCKED, 'no macOS SDK. Run xcode-select --install'),
    ]);
  }

  if (desktop === 'windows') {
    const root = process.env.VCPKG_INSTALLATION_ROOT;
    steps.push([
      'vcpkg',
      root != null && fs.existsSync(root)
        ? step(DONE, root)
        : step(
            BLOCKED,
            'VCPKG_INSTALLATION_ROOT is not set to a directory that exists. The ' +
              'Windows build takes its dependencies from vcpkg; see docs/PORTING.md',
          ),
    ]);
    steps.push([
      'clang-cl',
      present('clang-cl', ['--version'])
        ? step(DONE, versionOf('clang-cl') ?? 'present')
        : step(
            BLOCKED,
            'not installed. Hermes needs clang-cl rather than cl -- cl has no ' +
              '__builtin_expect; see docs/PORTING.md',
          ),
    ]);
  }

  return steps;
}

/**
 * The C++ libraries React Native's own code links against.
 *
 * These are the expensive ones to get wrong. React Native does not vendor
 * glog, Boost, fmt, double-conversion or OpenSSL, so `native/cmake/ThirdParty.cmake`
 * goes looking for them on the machine with `find_library` and `find_package`,
 * and a missing one stops CMake *after* bootstrap has compiled Hermes. That is
 * what happened to a release run on 2026-11-08: twenty minutes of Hermes, then
 * `Could not find GLOG_LIBRARY using the following names: glog`, because the
 * macOS tab of getting-started asked for cmake and ninja and nothing else.
 *
 * ## Why this asks twice
 *
 * `pkg-config` is the cheap question and does not always have the answer.
 * Debian's libdouble-conversion-dev ships a .pc file and Homebrew's
 * double-conversion does not; Boost ships one nowhere. So each library is a
 * pkg-config name *and* a header, either of which is enough, and the header
 * search is the same set of prefixes a compiler would look in.
 *
 * ## Why it is not CMake
 *
 * Running the real `find_library` would be exact and would need a configure
 * step, which is most of what this command exists to run ahead of. This is a
 * cheaper question with a looser answer: it finds a header where CMake wants a
 * library, so it can in principle say present about a headers-only install.
 * That trade is deliberate -- being quiet about a broken install is a worse
 * failure than being quiet about a weird one, and the build still says so.
 */
export function checkCxxLibraries(
  desktop: string | null,
  present: (program: string, args?: string[]) => boolean = onPath,
  exists: (file: string) => boolean = fs.existsSync,
  env: NodeJS.ProcessEnv = process.env,
): NamedStep[] {
  if (desktop == null) {
    return [];
  }

  // Where a header lives, by platform. Not a guess at the compiler's search
  // path so much as the three places a packager puts things: apt's, Homebrew's
  // on either architecture, and the triplet vcpkg installs into.
  const prefixes: string[] = [];
  if (desktop === 'linux') {
    prefixes.push('/usr/include', '/usr/local/include');
  } else if (desktop === 'macos') {
    const brew = versionOf('brew', ['--prefix']);
    if (brew != null && brew !== '') {
      prefixes.push(path.join(brew, 'include'));
    }
    prefixes.push('/opt/homebrew/include', '/usr/local/include', '/usr/include');
  } else if (desktop === 'windows') {
    const root = env.VCPKG_INSTALLATION_ROOT;
    if (root != null) {
      prefixes.push(path.join(root, 'installed', 'x64-windows', 'include'));
    }
  }

  // One line per library: what pkg-config calls it, the header to fall back
  // on, and what to type. The install advice is per platform because that is
  // the only part of this that differs.
  const all: ReadonlyArray<{
    name: string;
    modules: readonly string[];
    header: string;
    /** Platforms that do not need it, because the system already has it. */
    shipped?: readonly string[];
    how: Record<string, string>;
  }> = [
    {
      name: 'glog',
      modules: ['libglog', 'glog'],
      header: 'glog/logging.h',
      how: {
        linux: 'apt install libgoogle-glog-dev',
        macos: 'brew install glog',
        windows: 'vcpkg install --triplet x64-windows glog',
      },
    },
    {
      name: 'boost',
      modules: [],
      header: 'boost/version.hpp',
      how: {
        linux: 'apt install libboost-dev libboost-regex-dev',
        macos: 'brew install boost',
        windows: 'vcpkg install --triplet x64-windows boost-regex boost-thread',
      },
    },
    {
      name: 'fmt',
      modules: ['fmt'],
      header: 'fmt/format.h',
      how: {
        linux: 'apt install libfmt-dev',
        macos: 'brew install fmt',
        windows: 'vcpkg install --triplet x64-windows fmt',
      },
    },
    {
      name: 'double-conversion',
      modules: ['double-conversion'],
      header: 'double-conversion/double-conversion.h',
      how: {
        linux: 'apt install libdouble-conversion-dev',
        macos: 'brew install double-conversion',
        windows: 'vcpkg install --triplet x64-windows double-conversion',
      },
    },
    {
      name: 'openssl',
      modules: ['openssl'],
      header: 'openssl/ssl.h',
      how: {
        linux: 'apt install libssl-dev',
        macos: 'brew install openssl',
        windows: 'vcpkg install --triplet x64-windows openssl',
      },
    },
    {
      name: 'curl',
      modules: ['libcurl'],
      header: 'curl/curl.h',
      // macOS ships libcurl, which is why the macOS prerequisites do not name
      // it and CI's macOS job has never installed it.
      shipped: ['macos'],
      how: {
        linux: 'apt install libcurl4-openssl-dev',
        windows: 'vcpkg install --triplet x64-windows curl',
      },
    },
  ];

  return all
    .filter(({shipped}) => !(shipped ?? []).includes(desktop))
    .map(({name, modules, header, how}): NamedStep => {
      const module = modules.find(m => present('pkg-config', ['--exists', m]));
      if (module != null) {
        // Debian's double-conversion.pc declares no version, so an empty
        // answer here means found-and-silent rather than not found.
        const version = versionOf('pkg-config', ['--modversion', module]);
        return [name, step(DONE, version != null && version !== '' ? version : 'present')];
      }
      const prefix = prefixes.find(p => exists(path.join(p, header)));
      if (prefix != null) {
        return [name, step(DONE, path.join(prefix, header))];
      }
      return [
        name,
        step(
          BLOCKED,
          `not found. ${how[desktop] ?? 'see the getting-started prerequisites'} -- ` +
            "React Native's C++ links against it, so CMake stops without it.",
        ),
      ];
    });
}

/** Whether the host packages the app asked for are actually installed. */
export function checkHostPackages(projectRoot: string, wanted: readonly string[]): NamedStep[] {
  return wanted.map((desktop): NamedStep => {
    const name = HOST_PACKAGES[desktop];
    const installed = fs.existsSync(path.join(projectRoot, 'node_modules', name, 'package.json'));
    return [
      name,
      installed
        ? step(DONE, 'installed')
        : step(
            BLOCKED,
            `not installed, so there is no run-${desktop} command. Run your ` +
              'package manager, or `npx basalt-core init` if it is not a dependency yet.',
          ),
    ];
  });
}

export function doctor(
  root: string = process.cwd(),
  versionsFile: string = path.join(__dirname, '..', '..', 'supported-versions.json'),
): DoctorResult {
  // The app half, which is exactly what `init` would do, minus the doing.
  const configured = init(root, {write: false});
  if (!configured.ok) {
    return {ok: false, reason: configured.reason, steps: []};
  }

  const {desktops} = desktopsFor(root);
  const steps: NamedStep[] = [
    ...configured.steps,
    ...checkHostPackages(root, desktops),
    ['react-native', checkReactNative(root, versionsFile)],
    ...checkBuildTools(),
    ...checkDesktop(currentDesktop(), desktops),
    ...checkCxxLibraries(currentDesktop()),
  ];

  return {ok: steps.every(([, outcome]) => outcome.state !== BLOCKED), steps};
}

export function main(argv: string[]): number {
  const args = argv[0] === 'doctor' ? argv.slice(1) : argv;
  const root = args[0] != null && !args[0].startsWith('-') ? path.resolve(args[0]) : process.cwd();

  const result = doctor(root);
  if (result.reason != null) {
    process.stderr.write(`basalt-core doctor: ${result.reason}\n`);
    return 1;
  }

  const {changed, blocked} = report(result.steps, line => process.stdout.write(`${line}\n`));
  if (blocked > 0) {
    process.stdout.write(`\n${blocked} thing${blocked === 1 ? '' : 's'} to fix; see above.\n`);
    return 1;
  }
  // `changed` here means "init would change it", which is not a failure: an
  // app that has never been configured is not a broken machine.
  process.stdout.write(
    changed === 0
      ? '\nReady to build.\n'
      : `\nNothing is wrong; ${changed} thing${changed === 1 ? '' : 's'} ` +
          'would change. Run `npx basalt-core init`.\n',
  );
  return 0;
}

if (require.main === module) {
  process.exitCode = main(process.argv.slice(2));
}
