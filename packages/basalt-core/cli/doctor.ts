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
// `docs/PORTING.md` says what each platform's build needs, and CI installs
// exactly that list. These checks encode the same one; if it grows there and
// not here, this command gets quieter rather than wrong, which is the failure
// worth knowing about.

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
