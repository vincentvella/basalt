// cmake/Skia.cmake refuses a Skia it cannot build, by version.
//
// The CLI declines a 3.x install before configuring (scripts/test_cli.js), and
// this is the same refusal one layer down, for the hand-written
// `-DBASALT_SKIA=`. Worth its own test because of what it replaces: without it
// the configure fails on `MetalContext.mm does not exist` out of a source list,
// naming a file that 3.x deleted and reading like a broken install.
//
// Run with `cmake -P`, which is script mode: the file is included with the few
// variables it needs set, and the guard is early enough to fire before anything
// script mode cannot do. A 2.x install is expected to fail *later* and
// differently, which is the negative control -- it proves the refusal is about
// the version rather than about everything.

const assert = require('node:assert');
const {execFileSync} = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {test} = require('node:test');

const REPO = path.resolve(__dirname, '..');
const SKIA_CMAKE = path.join(REPO, 'packages/basalt-core/native/cmake/Skia.cmake');

// A package tree with just enough in it to reach the version guard: the file
// the C++ check looks for, and a version.
function fakeSkia(version) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-skia-'));
  fs.mkdirSync(path.join(dir, 'cpp/rnskia'), {recursive: true});
  fs.writeFileSync(path.join(dir, 'cpp/rnskia/RNSkManager.cpp'), '');
  fs.writeFileSync(
    path.join(dir, 'package.json'),
    JSON.stringify({name: 'react-native-skia', version}),
  );
  return dir;
}

function configure(skiaDir) {
  const script = path.join(skiaDir, 'include.cmake');
  fs.writeFileSync(
    script,
    `set(BASALT_SKIA "${skiaDir.split(path.sep).join('/')}")\n` +
      `include("${SKIA_CMAKE.split(path.sep).join('/')}")\n`,
  );
  try {
    execFileSync('cmake', ['-P', script], {encoding: 'utf8', stdio: 'pipe'});
    return {failed: false, output: ''};
  } catch (error) {
    return {failed: true, output: `${error.stdout ?? ''}${error.stderr ?? ''}`};
  }
}

test('Skia.cmake refuses a Graphite major and says which', () => {
  const dir = fakeSkia('3.4.0');
  const {failed, output} = configure(dir);
  assert.ok(failed, 'a 3.x install configured against the Ganesh half');
  assert.ok(output.includes('3.4.0'), `the refusal does not say the version:\n${output}`);
  assert.ok(output.includes('Graphite'), `the refusal does not say why:\n${output}`);
  assert.ok(
    output.includes('release-2.x'),
    `the refusal does not say what to install instead:\n${output}`,
  );
  fs.rmSync(dir, {recursive: true, force: true});
});

test('Skia.cmake does not refuse the major it builds', () => {
  // 2.x gets past the guard. It does not get far in script mode -- there are no
  // archives and no add_library outside a project -- so this asserts only that
  // whatever stops it is not the version, which is the thing being tested.
  for (const version of ['2.11.1', '2.15.1']) {
    const dir = fakeSkia(version);
    const {output} = configure(dir);
    assert.ok(
      !output.includes('Graphite'),
      `${version} was refused as a Graphite major:\n${output}`,
    );
    fs.rmSync(dir, {recursive: true, force: true});
  }
});

test('Skia.cmake survives a package that declares no version', () => {
  // An unreadable version must not read as a major to refuse: the regex finds
  // nothing, SKIA_MAJOR is empty, and an empty string is falsy to `if`.
  const dir = fakeSkia(undefined);
  const {output} = configure(dir);
  assert.ok(
    !output.includes('Graphite'),
    `a versionless package was refused as a Graphite major:\n${output}`,
  );
  fs.rmSync(dir, {recursive: true, force: true});
});
