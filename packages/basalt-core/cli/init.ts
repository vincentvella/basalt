#!/usr/bin/env node
/**
 * `npx basalt-core init`, which adds a desktop to an app that exists.
 *
 * It does not create an app. `create-expo-app` does that, and the division is
 * the same one `run-linux` already assumes: this platform is something you add
 * to a React Native app, not a way to start one.
 *
 * ## Why a bin rather than a CLI command
 *
 * The run commands are React Native CLI commands, registered from the host
 * packages, because the CLI has to be able to find them. This one cannot be:
 * before it runs, the package is not a dependency yet, so there is no config
 * for the CLI to read. `npx basalt-core init` works from nothing, which
 * is the point -- one command, in an app that has never heard of this platform.
 *
 * ## Idempotent, and refusing beats half-configuring
 *
 * Every step reports one of three things: it was already right, it was changed,
 * or it could not be done and why. Running twice is safe and says so. An app
 * this cannot recognise is refused before anything is written, because a
 * half-configured app fails later and further from the cause than one that was
 * turned away -- and the failure it produces names a Metro or Babel internal
 * rather than the step that was skipped.
 *
 * @format
 */

import * as fs from 'node:fs';
import * as path from 'node:path';

import {BLOCKED, CHANGED, DONE, report, step} from './steps';
import type {NamedStep, Step, StepState} from './steps';

// Re-exported because they were this module's before `doctor` needed them too.
export type {Step, StepState};

// eslint-disable-next-line @typescript-eslint/no-var-requires
const {DESKTOP_PLATFORMS} = require('../metro-config') as {
  DESKTOP_PLATFORMS: ReadonlyArray<string>;
};

// The dev dependencies an app needs to bundle for a desktop. Both are React
// Native's own: the Metro config this platform wraps, and the CLI that carries
// the run commands. An Expo app has neither, because `expo` is its CLI.
export const DEV_DEPENDENCIES = [
  '@react-native/metro-config',
  '@react-native-community/cli',
] as const;

const PACKAGE_NAME = 'basalt-core';

/**
 * The host packages, one per desktop, and all three by default.
 *
 * Each carries its own `run-<desktop>` command -- React Native's CLI reads a
 * `react-native.config.js` out of every dependency, and the commands live
 * beside the host they need rather than in core, so an app with only the
 * AppKit package never sees a command it could not have used. Which means an
 * app that installed core alone has no desktop commands at all, and `init`'s
 * own closing line tells the person to run one.
 *
 * All three rather than the one this machine runs, because `package.json` is
 * committed: which desktops an app builds for is a property of the project and
 * not of whoever set it up. Narrowing by the current platform would configure
 * an app on a Mac that failed on a contributor's Linux box, reporting an
 * unrecognised command -- which reads as a broken install rather than as a
 * decision somebody made.
 *
 * The cost of the other two is source that is never compiled: the build only
 * configures the host for the platform it is running on. An app that wants
 * fewer says so in app.json; see DESKTOPS_KEY.
 */
export const HOST_PACKAGES: Readonly<Record<string, string>> = {
  linux: 'basalt-gtk',
  macos: 'basalt-appkit',
  windows: 'basalt-win32',
};

/**
 * Where an app narrows that list: `"basalt": {"desktops": ["macos"]}`.
 *
 * app.json rather than package.json, and under the `basalt` key that is
 * already this platform's escape hatch there -- `cli/packageApp.ts` reads
 * `basalt.identifier` and `basalt.scheme` from the same place.
 *
 * Not Expo's own `platforms` field: a bare React Native app does not have one,
 * and it is Expo's to define rather than this platform's to borrow.
 */
const DESKTOPS_KEY = 'desktops';


/** The app's package.json, as far as this reads and writes it. */
type Manifest = {
  dependencies?: Record<string, string>;
  devDependencies?: Record<string, string>;
  peerDependencies?: Record<string, string>;
  scripts?: Record<string, string>;
  [key: string]: unknown;
};

type Project =
  | {ok: false; reason: string}
  | {ok: true; manifest: string; json: Manifest; expo: boolean};

export type InitResult =
  | {ok: false; reason: string; steps: NamedStep[]}
  | {ok: true; steps: NamedStep[]};


/**
 * Reads a JSON file, or returns null when it is absent or unparseable.
 *
 * Unparseable and absent are deliberately the same answer here: both mean "this
 * is not something to edit", and the caller says so with the path.
 */
function readJson<T = Manifest>(file: string): T | null {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8')) as T;
  } catch {
    return null;
  }
}

/**
 * Is this a React Native app at all?
 *
 * `react-native` in any dependency field, or `expo` -- an Expo app has React
 * Native transitively and may not name it. Anything else is refused.
 */
function describeProject(root: string): Project {
  const manifest = path.join(root, 'package.json');
  if (!fs.existsSync(manifest)) {
    return {ok: false, reason: `no package.json in ${root}`};
  }
  const json = readJson(manifest);
  if (json == null) {
    return {ok: false, reason: `${manifest} is not readable JSON`};
  }
  const all = {
    ...(json.dependencies || {}),
    ...(json.devDependencies || {}),
    ...(json.peerDependencies || {}),
  };
  if (all['react-native'] == null && all.expo == null) {
    return {
      ok: false,
      reason:
        `${manifest} lists neither react-native nor expo. This adds a desktop ` +
        'to an app that exists; create one with `npx create-expo-app` first.',
    };
  }
  return {ok: true, manifest, json, expo: all.expo != null};
}

/**
 * The Metro config to edit, or null with the reason.
 *
 * Only the two filenames Metro itself looks for. A project using
 * `metro.config.ts` is a real case and not one this writes into blindly: the
 * wrapper is a require() call, and a TypeScript config may be doing something
 * this cannot preserve.
 */
function findMetroConfig(root: string): {file: string | null; reason?: string; missing?: boolean} {
  for (const name of ['metro.config.js', 'metro.config.cjs']) {
    const file = path.join(root, name);
    if (fs.existsSync(file)) {
      return {file};
    }
  }
  for (const name of ['metro.config.ts', 'metro.config.mjs']) {
    if (fs.existsSync(path.join(root, name))) {
      return {
        file: null,
        reason:
          `found ${name}, which this cannot edit safely. Add ` +
          `\`withDesktopPlatforms\` from \`${PACKAGE_NAME}/metro-config\` by hand.`,
      };
    }
  }
  // Absent rather than unreadable, which the caller writes one for.
  return {file: null, reason: 'no metro.config.js', missing: true};
}

/**
 * The desktops this app builds for: every one, unless app.json narrows it.
 *
 * An unknown name is refused rather than ignored. A typo -- "mac", "win" --
 * would otherwise quietly install one package fewer and fail much later, at
 * the command that is missing, which is the failure this whole step exists to
 * prevent.
 */
export function desktopsFor(root: string): {desktops: string[]; reason?: string} {
  const all = Object.keys(HOST_PACKAGES);
  const appJson = readJson<{basalt?: {desktops?: unknown}}>(path.join(root, 'app.json'));
  const named = appJson?.basalt?.[DESKTOPS_KEY];
  if (named == null) {
    return {desktops: all};
  }
  if (!Array.isArray(named) || named.some(entry => typeof entry !== 'string')) {
    return {
      desktops: all,
      reason: `app.json's "basalt.${DESKTOPS_KEY}" is not a list of names; using all of them`,
    };
  }
  const unknown = named.filter(entry => !(entry in HOST_PACKAGES));
  if (unknown.length > 0) {
    return {
      desktops: all,
      reason:
        `app.json's "basalt.${DESKTOPS_KEY}" names ${unknown.join(', ')}, which ` +
        `${unknown.length === 1 ? 'is not a desktop' : 'are not desktops'} this ` +
        `platform has. Expected any of ${all.join(', ')}; using all of them`,
    };
  }
  return {desktops: named as string[]};
}

/** Adds this package, the host packages, and the two dev dependencies. */
function addDependencies(
  project: Extract<Project, {ok: true}>,
  version: string,
  desktops: string[],
  write: boolean,
): Step {
  const json = project.json;
  const changes: string[] = [];

  json.dependencies = json.dependencies || {};
  if (json.dependencies[PACKAGE_NAME] == null) {
    json.dependencies[PACKAGE_NAME] = version;
    changes.push(PACKAGE_NAME);
  }

  // The same version as core: they share a C++ ABI with the host, and
  // docs/DECISIONS.md says they version together until that stops being true.
  for (const desktop of desktops) {
    const host = HOST_PACKAGES[desktop];
    if (host != null && json.dependencies[host] == null) {
      json.dependencies[host] = version;
      changes.push(host);
    }
  }

  json.devDependencies = json.devDependencies || {};
  for (const name of DEV_DEPENDENCIES) {
    // Not if it is already a dependency of any kind: an app that has it in
    // `dependencies` made that choice, and moving it is not this command's
    // business.
    const present =
      json.devDependencies[name] != null || (json.dependencies || {})[name] != null;
    if (!present) {
      json.devDependencies[name] = '*';
      changes.push(name);
    }
  }

  if (changes.length === 0) {
    return step(DONE, 'dependencies are already present');
  }
  // Phrased for what actually happened. `doctor` runs this same function with
  // writing off, and a report that said "added" when nothing was added would
  // be the one thing a read-only command must never do.
  return write
    ? step(CHANGED, `added ${changes.join(', ')} -- run your package manager to install`)
    : step(CHANGED, `would add ${changes.join(', ')}`);
}

/** Adds a script per desktop, leaving any the app already defined alone. */
function addScripts(project: Extract<Project, {ok: true}>, write: boolean): Step {
  const json = project.json;
  json.scripts = json.scripts || {};
  const added: string[] = [];
  for (const platform of DESKTOP_PLATFORMS) {
    const name = platform;
    if (json.scripts[name] == null) {
      json.scripts[name] = `react-native run-${platform}`;
      added.push(name);
    }
  }
  if (added.length === 0) {
    return step(DONE, 'scripts are already present');
  }
  return step(CHANGED, write ? `added scripts: ${added.join(', ')}` : `would add scripts: ${added.join(', ')}`);
}

/**
 * Writes a Metro config for an app that has none.
 *
 * A stock `create-expo-app --template blank` has no `metro.config.js` at all:
 * Expo's default is implicit, and an app only grows the file when it has
 * something to say. That is the common case rather than an edge one, and
 * blocking on it left the command reporting by hand the single step it exists
 * to do -- which is what `release.yml` had been working around with a heredoc
 * of exactly these three lines.
 *
 * Which default to start from is the app's own: `expo/metro-config` reads
 * app.json and the Expo plugins, and a bare React Native app has neither.
 */
function writeMetroConfig(root: string, expo: boolean): Step {
  const file = path.join(root, 'metro.config.js');
  const defaults = expo ? 'expo/metro-config' : '@react-native/metro-config';
  const contents =
    `const {getDefaultConfig} = require('${defaults}');\n` +
    `const {withDesktopPlatforms} = require('${PACKAGE_NAME}/metro-config');\n` +
    '\n' +
    'module.exports = withDesktopPlatforms(getDefaultConfig(__dirname));\n';

  fs.writeFileSync(file, contents);
  return step(CHANGED, `wrote metro.config.js, on ${defaults}`);
}

/** Whether the app's Metro config is already wrapped, changing nothing. */
function inspectMetroConfig(file: string): Step {
  const name = path.basename(file);
  const before = fs.readFileSync(file, 'utf8');
  if (before.includes('withDesktopPlatforms')) {
    return step(DONE, `${name} already wraps the config`);
  }
  if (!/^module\.exports\s*=\s*([\s\S]+?);$/m.test(before)) {
    return step(
      BLOCKED,
      `could not find a \`module.exports =\` in ${name}. Wrap its export in ` +
        '`withDesktopPlatforms(...)` by hand.',
    );
  }
  return step(CHANGED, `${name} would be wrapped in withDesktopPlatforms`);
}

/**
 * Wraps the app's Metro config, preserving what is there.
 *
 * Textual rather than by parsing: a Metro config is a small file people edit,
 * and rewriting it through an AST would reformat the whole thing to change one
 * line. What that costs is certainty, so anything not recognised is refused
 * rather than guessed at.
 */
function wrapMetroConfig(file: string): Step {
  const before = fs.readFileSync(file, 'utf8');

  if (before.includes('withDesktopPlatforms')) {
    return step(DONE, `${path.basename(file)} already wraps the config`);
  }

  const requireLine =
    `const {withDesktopPlatforms} = require('${PACKAGE_NAME}/metro-config');\n`;

  // `module.exports = <something>;` on one line, which is what both the Expo
  // and the bare template produce.
  // Non-greedy up to the semicolon, and deliberately not swallowing what
  // follows it: `\s*$` would eat the file's trailing newline, and a command
  // that reformats the end of a file it was asked to edit one line of is a
  // command people stop trusting.
  const assignment = /^module\.exports\s*=\s*([\s\S]+?);$/m;
  const match = before.match(assignment);
  if (match == null) {
    return step(
      BLOCKED,
      `could not find a \`module.exports =\` in ${path.basename(file)}. Wrap ` +
        'its export in `withDesktopPlatforms(...)` by hand.',
    );
  }

  const wrapped = before.replace(
    assignment,
    `module.exports = withDesktopPlatforms(${match[1].trim()});`,
  );

  // After the last require at the top, so the file still reads top to bottom.
  const requires = [...wrapped.matchAll(/^(?:const|let|var)\s.*require\(.*\);\s*$/gm)];
  const after =
    requires.length > 0
      ? wrapped.slice(0, requires[requires.length - 1].index + requires[requires.length - 1][0].length) +
        '\n' +
        requireLine +
        wrapped.slice(requires[requires.length - 1].index + requires[requires.length - 1][0].length)
      : requireLine + wrapped;

  fs.writeFileSync(file, after);
  return step(CHANGED, `wrapped ${path.basename(file)} in withDesktopPlatforms`);
}

/**
 * This package's own version, for the dependency `init` adds.
 *
 * Found by walking up rather than by a fixed number of `..`: this file runs
 * from `dist/cli/` once built and from `cli/` in a checkout, so any hardcoded
 * depth is wrong in one of the two -- which is exactly the bug that shipped the
 * first time, as a dependency pinned to `*`.
 */
function ownVersion(): string {
  let dir = __dirname;
  for (let up = 0; up < 5; up++) {
    const candidate = path.join(dir, 'package.json');
    const json = readJson<{name?: string; version?: string}>(candidate);
    if (json?.name === PACKAGE_NAME) {
      return json.version != null ? `^${json.version}` : '*';
    }
    const parent = path.dirname(dir);
    if (parent === dir) {
      break;
    }
    dir = parent;
  }
  return '*';
}

export function init(root: string = process.cwd(), {write = true}: {write?: boolean} = {}): InitResult {
  const project = describeProject(root);
  if (!project.ok) {
    return {ok: false, reason: project.reason, steps: []};
  }

  const metro = findMetroConfig(root);
  const steps: NamedStep[] = [];

  const {desktops, reason} = desktopsFor(root);
  if (reason != null) {
    // Reported rather than refused: the app is configurable, and what is wrong
    // is one field. Refusing would leave it configured for nothing.
    steps.push(['desktops', step(BLOCKED, reason)]);
  }
  steps.push(['dependencies', addDependencies(project, ownVersion(), desktops, write)]);
  steps.push(['scripts', addScripts(project, write)]);

  if (metro.file == null && metro.missing) {
    // No config at all, which is what a stock Expo app looks like: write one
    // rather than refuse. A config that exists and cannot be edited is a
    // different case and still refused below.
    steps.push([
      'metro config',
      write ? writeMetroConfig(root, project.expo) : step(CHANGED, 'would write metro.config.js'),
    ]);
  } else if (metro.file == null) {
    steps.push(['metro config', step(BLOCKED, metro.reason ?? 'no metro config')]);
  } else if (write) {
    steps.push(['metro config', wrapMetroConfig(metro.file)]);
  } else {
    // The read-only form, which this branch did not have: with writing off it
    // pushed no step at all, so `doctor` would have said nothing about the one
    // file most likely to be wrong.
    steps.push(['metro config', inspectMetroConfig(metro.file)]);
  }

  if (write) {
    // Two spaces and a trailing newline, which is what npm itself writes, so an
    // app's package.json does not churn in the next unrelated diff.
    fs.writeFileSync(project.manifest, `${JSON.stringify(project.json, null, 2)}\n`);
  }

  return {ok: true, steps};
}

export function main(argv: string[]): number {
  // `npx basalt-core init` is how the README and the proposal both
  // spell this, and it is how react-native-windows and react-native-macos
  // spell theirs -- so `init` is the verb, not the directory to configure.
  // Without this the command resolved ./init, found no package.json there,
  // and refused to configure the app it was standing in.
  const args = argv[0] === 'init' ? argv.slice(1) : argv;
  const root = args[0] != null && !args[0].startsWith('-') ? path.resolve(args[0]) : process.cwd();

  const result = init(root);
  if (!result.ok) {
    process.stderr.write(`basalt-core init: ${result.reason}\n`);
    return 1;
  }

  const {changed, blocked} = report(result.steps, line => process.stdout.write(`${line}\n`));

  if (blocked > 0) {
    process.stdout.write('\nSome steps need doing by hand; see above.\n');
    return 1;
  }
  process.stdout.write(
    changed === 0
      ? '\nAlready configured. Nothing to do.\n'
      : '\nInstall dependencies, then `npm run linux` (or macos, or windows).\n',
  );
  return 0;
}

if (require.main === module) {
  process.exitCode = main(process.argv.slice(2));
}
