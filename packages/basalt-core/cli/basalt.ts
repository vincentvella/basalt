#!/usr/bin/env node
// `npx basalt-core <verb>`.
//
// Two verbs, and the same shape react-native-windows and react-native-macos
// use: a bin named for the platform, taking the thing to do. `init` was the
// first and was the bin itself, which is why it had to learn that `init` is a
// word and not a directory.
//
// Not a React Native CLI command, for the reason init.ts gives: before this
// runs, the package is not a dependency yet and the CLI has no config to read.

import {main as initMain} from './init';
import {main as doctorMain} from './doctor';

const VERBS: Readonly<Record<string, (argv: string[]) => number>> = {
  init: initMain,
  doctor: doctorMain,
};

// The binary, not the package. They differ: the package had to be basalt-core
// because the bare name was taken on npm, and bin names are a separate
// namespace where it was not. The rename rewrote this string along with every
// other mention of the old package name, and it is the one place that wanted
// the command a person types.
const USAGE = `basalt <command> [directory]

  init     configure an app to build for the desktop
  doctor   report what a build needs, changing nothing
`;

export function main(argv: string[]): number {
  const verb = argv[0];

  if (verb === '--help' || verb === '-h' || verb == null) {
    process.stdout.write(USAGE);
    // No verb is a question, not a mistake, and a question was answered.
    return verb == null ? 1 : 0;
  }

  const run = VERBS[verb];
  if (run == null) {
    // A path where a verb was expected is the old spelling of this command,
    // when `init` was the bin. Saying so beats "unknown command ./my-app".
    process.stderr.write(`basalt-core: no such command ${JSON.stringify(verb)}\n\n${USAGE}`);
    return 1;
  }

  // The verb is passed through: each main() already skips its own name, so
  // that `node dist/cli/init.js init` keeps working for anything that calls
  // the file directly.
  return run(argv);
}

if (require.main === module) {
  process.exitCode = main(process.argv.slice(2));
}
