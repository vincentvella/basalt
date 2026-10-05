#!/usr/bin/env node
// Starts Metro for the demo app, so the host can load its bundle over http and
// get Fast Refresh.
//
// Usage:  node scripts/metro.js [/path/to/react-native] [--port 8081]
//
// Then, in another terminal:
//
//   BASALT_DEV=1 ./build/basalt_gtk
//
// This exists in Node rather than in shell because Windows has to run it too.
// scripts/metro.sh was the only way to start a packager, so the two scenarios
// that need one -- Fast Refresh and the developer menu -- skipped on the one
// desktop that had most recently gained development mode.
//
// It serves in-process rather than spawning `metro serve`, which matters for
// stopping it: a parent that spawns Metro leaves it running when it is killed
// on Windows, where terminating a process does not terminate its children. One
// process means the caller's kill is the whole of the cleanup.
//
// The host asks for "http://localhost:8081/index.bundle?platform=...", a URL
// DevServerHelper builds from ReactInstanceConfig plus the source path. Metro
// must therefore be serving e2e/ as its project root, which is what the config
// below arranges.

const fs = require('fs');
const os = require('os');
const path = require('path');
const {createRequire} = require('module');

const REPO_ROOT = path.resolve(__dirname, '..');

let port = 8081;
const positional = [];
for (let i = 2; i < process.argv.length; i++) {
  if (process.argv[i] === '--port') {
    port = Number(process.argv[++i]);
  } else {
    positional.push(process.argv[i]);
  }
}
if (!Number.isInteger(port) || port <= 0) {
  console.error(`error: --port wants a port number, not '${process.argv[3]}'`);
  process.exit(1);
}

// react-native-src too, for the reason bundle.sh gives: it is where CI and
// wsl_setup.sh put the checkout. Without it the two scenarios that start their
// own Metro failed with "metro exited before it started serving" anywhere
// RN_DIR was not set by hand.
const candidates = [
  positional[0],
  process.env.RN_DIR,
  path.join(REPO_ROOT, '..', 'react-native'),
  path.join(REPO_ROOT, 'react-native-src'),
  path.join(os.homedir(), 'Workspace', 'react-native'),
].filter(candidate => candidate != null && candidate !== '');

const rnDir = candidates
  .map(candidate => path.resolve(candidate))
  .find(candidate =>
    fs.existsSync(path.join(candidate, 'packages', 'react-native', 'ReactCommon')),
  );

if (rnDir == null) {
  console.error(
    'error: pass the React Native checkout path, or set RN_DIR.\n' +
      `Looked in:\n${candidates.map(c => `  ${path.resolve(c)}`).join('\n')}`,
  );
  process.exit(1);
}
if (!fs.existsSync(path.join(rnDir, 'node_modules', 'react-native'))) {
  console.error(
    `error: ${rnDir} has no installed dependencies; run scripts/bootstrap.sh first`,
  );
  process.exit(1);
}

// e2e/metro.config.js reads this, and so does CMake. Set rather than required so
// that a caller who passed the path as an argument does not also have to export
// it.
process.env.RN_DIR = rnDir;

// Resolved against the checkout, not against this repo: nothing is installed
// here, and the point of the whole arrangement is that the JavaScript side runs
// against the same source tree the C++ side is built from.
const rnRequire = createRequire(path.join(rnDir, 'package.json'));
const {loadConfig, runServer} = rnRequire('metro');

const configPath = path.join(REPO_ROOT, 'e2e', 'metro.config.js');

async function main() {
  console.log(`==> Metro on port ${port}, project root ${REPO_ROOT}/e2e`);
  // What `metro serve` does, minus the config-file watching that restarts the
  // server: loadConfig takes the port from here and puts it in config.server,
  // which is where runServer looks for it. Fast Refresh needs nothing switched
  // on -- runServer's HMR endpoint is not optional.
  const config = await loadConfig({config: configPath, port});
  await runServer(config, {host: 'localhost'});
}

main().catch(error => {
  console.error(error);
  process.exit(1);
});
