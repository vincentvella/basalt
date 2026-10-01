/**
 * Tests for basalt-core's JavaScript that does not need React Native to
 * run: today, the title bar's request stack.
 *
 * Run with:  node --test scripts/test_platform_js.js
 *
 * @format
 */

'use strict';

const assert = require('node:assert');
const path = require('node:path');
const {test} = require('node:test');

// The built output, not the source: this is TypeScript now, and `main` points
// into `dist/`. Run scripts/build_ts.sh first -- which is what the failure
// below says, because "cannot find module" on a path that plainly exists in the
// repository is the most confusing way to learn it.
const built = path.join(
  __dirname,
  '..',
  'packages/basalt-core/dist/src/titleBarState.js',
);
if (!require('node:fs').existsSync(built)) {
  throw new Error(`${built} does not exist. Run scripts/build_ts.sh first.`);
}
const {createTitleBarStack, sameRequest} = require(built);

test('the most recently mounted title bar request wins, key by key', () => {
  const stack = createTitleBarStack();
  stack.push({title: 'App', backgroundColor: '#111111', style: 'hidden'});
  stack.push({title: 'Settings'});

  // The screen's title, over the shell's colour and style, which it did not
  // mention and so does not reset.
  assert.deepEqual(stack.resolve(), {
    title: 'Settings',
    backgroundColor: '#111111',
    style: 'hidden',
  });
});

test("unmounting a request restores what was beneath it", () => {
  const stack = createTitleBarStack();
  stack.push({title: 'App'});
  const screen = stack.push({title: 'Settings', textColor: 'white'});

  stack.remove(screen);
  assert.deepEqual(stack.resolve(), {title: 'App'});

  stack.remove(screen); // twice is harmless
  assert.equal(stack.size, 1);
});

test('updating a request replaces its keys rather than adding to them', () => {
  const stack = createTitleBarStack();
  const id = stack.push({title: 'Inbox', backgroundColor: 'red'});

  // The component re-rendered without a background colour: it no longer asks
  // for one, so none is resolved.
  stack.update(id, {title: 'Inbox (3)'});
  assert.deepEqual(stack.resolve(), {title: 'Inbox (3)'});
});

test('keys that are not title bar options are ignored', () => {
  const stack = createTitleBarStack();
  stack.push({title: 'App', children: 'nope', onPress() {}});
  assert.deepEqual(stack.resolve(), {title: 'App'});
});

test('two requests for the same thing are the same request', () => {
  assert.ok(sameRequest({title: 'A', style: 'hidden'}, {style: 'hidden', title: 'A'}));
  assert.ok(!sameRequest({title: 'A'}, {title: 'B'}));
  assert.ok(sameRequest({}, null));
});

// --------------------------------------------------------------------------
// --- What a blob response actually contains ---------------------------------
//
// The override asks for base64 because that is the only response encoding that
// carries bytes through a JavaScript string intact. ReactCxxPlatform only
// applies it after 0.86, and the copy in use is the app's, so both behaviours
// are live. Handing a raw body to the base64 decoder is what turned every one of
// kino's daemon responses into "JSON Parse error: Unexpected character: v".

const responseBody = require(path.join(
  __dirname,
  '..',
  'packages/basalt-core/dist/src/responseBody.js',
));

test('a base64 body is decoded as base64', () => {
  const encoded = Buffer.from('{"durationInFrames":36}').toString('base64');
  const {part, size, encoded: wasEncoded} = responseBody.createResponseBodyReader()(encoded);

  assert.strictEqual(wasEncoded, true);
  assert.strictEqual(part.type, 'base64');
  assert.strictEqual(part.data, encoded);
  assert.strictEqual(size, '{"durationInFrames":36}'.length, 'the decoded length');
});

test('a raw JSON body is taken as the text it is', () => {
  // The failure this exists for. Decoded as base64 it loses almost everything,
  // because `{`, `"` and `:` are not in the alphabet.
  const raw = '{"durationInFrames":36,"durationSeconds":1.2,"fps":30}';
  const {part, size, encoded} = responseBody.createResponseBodyReader()(raw);

  assert.strictEqual(encoded, false, 'the platform did not encode it');
  assert.strictEqual(part.type, 'string');
  assert.strictEqual(part.data, raw);
  assert.strictEqual(size, raw.length);
});

test('one undecodable body settles it for the ones that are ambiguous', () => {
  // `null` is four characters from the base64 alphabet, so it cannot be told
  // apart on its own -- and a daemon answering `null` for something it does not
  // have is ordinary. What separates them is that the platform's behaviour does
  // not change between responses.
  const read = responseBody.createResponseBodyReader();

  assert.strictEqual(read('null').encoded, true, 'ambiguous, and nothing known yet');
  read('{"a":1}'); // decisive: a platform that encodes could not produce this
  assert.strictEqual(read('null').part.type, 'string', 'now known not to encode');
  assert.strictEqual(read('true').part.type, 'string');
  assert.strictEqual(read('1234').part.type, 'string');
});

test('a platform that does encode is never talked out of it', () => {
  // Every body it produces is canonical base64, so nothing decisive can arrive.
  const read = responseBody.createResponseBodyReader();
  for (const body of ['{}', 'null', '{"a":1}', 'hello, world', '\u00e9\u00e9']) {
    const encoded = Buffer.from(body).toString('base64');
    assert.strictEqual(read(encoded).encoded, true, body);
  }
});

test('a JSON object or array is always decisive', () => {
  // Which is why the window above is narrow: these are what an app fetches.
  for (const body of ['{}', '{"a":1}', '[1,2,3]', '{"v":"vvvv"}']) {
    assert.strictEqual(responseBody.looksLikeBase64(body), false, body);
  }
});

test('base64 is recognised only when it is canonical', () => {
  assert.strictEqual(responseBody.looksLikeBase64('aGVsbG8='), true);
  assert.strictEqual(responseBody.looksLikeBase64('aGVsbG8'), false, 'not a multiple of four');
  assert.strictEqual(responseBody.looksLikeBase64('aGVs bG8='), false, 'a space is not base64');
  assert.strictEqual(responseBody.looksLikeBase64('aGV=sbG8='), false, 'padding only at the end');
  assert.strictEqual(responseBody.looksLikeBase64(''), false, 'nothing is not base64');
});

test('a size is bytes rather than characters', () => {
  // A Blob's size is a byte count, and the raw branch is handed a JavaScript
  // string. One is not the other the moment anything is not ASCII.
  assert.strictEqual(responseBody.utf8Length('abc'), 3);
  assert.strictEqual(responseBody.utf8Length('\u00e9'), 2, 'e-acute is two bytes');
  assert.strictEqual(responseBody.utf8Length('\u20ac'), 3, 'a euro sign is three');
  assert.strictEqual(responseBody.utf8Length('\ud83d\ude80'), 4, 'a rocket is four');
  const body = '{"name":"caf\u00e9"}';
  assert.strictEqual(responseBody.createResponseBodyReader()(body).size, body.length + 1);
});

test('an empty body is text rather than base64', () => {
  const {part, size, encoded} = responseBody.createResponseBodyReader()('');
  assert.strictEqual(encoded, false);
  assert.strictEqual(part.type, 'string');
  assert.strictEqual(size, 0);
});

test('a byte count from the server settles it with nothing learned', () => {
  // Exact rather than inferred, and it works on the very first response --
  // including the ambiguous ones the learned verdict cannot help with yet.
  const read = responseBody.createResponseBodyReader();
  assert.strictEqual(read('null', 4).part.type, 'string', 'four bytes means the body itself');

  const read2 = responseBody.createResponseBodyReader();
  const encoded = Buffer.from('null').toString('base64'); // "bnVsbA==", 8 chars
  assert.strictEqual(read2(encoded, 4).encoded, true, 'four bytes means this is its base64');
});

test('a byte count teaches the responses that arrive without one', () => {
  // A chunked response has no Content-Length, and kino's daemon sends every
  // response chunked -- so what one sized response settles has to carry over.
  const read = responseBody.createResponseBodyReader();
  read('{"a":1}', 7); // decisive: raw
  assert.strictEqual(read('true').part.type, 'string', 'now known, with no length given');
});

test('a byte count that fits both ways decides nothing', () => {
  const read = responseBody.createResponseBodyReader();
  // An empty body is zero bytes either way, and must not teach anything.
  assert.strictEqual(read('', 0).part.type, 'string');
  assert.strictEqual(read('bnVsbA==').encoded, true, 'still unknown, so still base64');
});

// --- Response headers -------------------------------------------------------
//
// ReactCxxPlatform sends an array of [name, value] pairs and React Native's XHR
// does Object.keys on it, so every lookup missed while every header was present.

const responseHeaders = require(path.join(
  __dirname,
  '..',
  'packages/basalt-core/dist/src/responseHeaders.js',
));

test('pairs from the platform become an object', () => {
  const headers = responseHeaders.headersToObject([
    ['Content-Type', 'application/json'],
    ['Content-Length', '580'],
  ]);
  assert.deepStrictEqual(headers, {
    'Content-Type': 'application/json',
    'Content-Length': '580',
  });
});

test('an object is passed through untouched', () => {
  // So that a ReactCxxPlatform which starts sending one needs no change here.
  const given = {'content-type': 'text/plain'};
  assert.strictEqual(responseHeaders.headersToObject(given), given);
});

test('nothing stays nothing', () => {
  assert.strictEqual(responseHeaders.headersToObject(null), null);
  assert.strictEqual(responseHeaders.headersToObject(undefined), null);
  assert.deepStrictEqual(responseHeaders.headersToObject([]), {});
});

test('a header sent twice is joined, the way XHR reports one', () => {
  assert.deepStrictEqual(
    responseHeaders.headersToObject([
      ['Vary', 'Accept'],
      ['Vary', 'Origin'],
    ]),
    {Vary: 'Accept, Origin'},
  );
});

test('the same header in two casings is one header', () => {
  const headers = responseHeaders.headersToObject([
    ['Vary', 'Accept'],
    ['vary', 'Origin'],
  ]);
  assert.deepStrictEqual(headers, {Vary: 'Accept, Origin'});
});

test('a malformed pair is skipped rather than stringified', () => {
  // This is parsing something that crossed a bridge.
  const headers = responseHeaders.headersToObject([
    ['Content-Type', 'application/json'],
    ['Oops'],
    [],
    ['Bad', 7],
    [null, 'x'],
  ]);
  assert.deepStrictEqual(headers, {'Content-Type': 'application/json'});
});

// The package's require()-able surface
// --------------------------------------------------------------------------
//
// Every one of these is reached by `require()` from a file the type checker
// never sees -- an app's metro.config.js, this package's own
// react-native.config.js, the host packages' CLI. So dropping one compiles
// cleanly and fails at runtime somewhere else, which is exactly what happened
// once: converting metro-config.js to TypeScript replaced its `module.exports`
// block, and five of its eight exports went with it. The build was green.

test('metro-config exports everything that requires it expects', () => {
  const metroConfig = require(
    path.join(__dirname, '..', 'packages/basalt-core/dist/metro-config.js'),
  );
  for (const name of [
    'withDesktopPlatforms',
    'withLinuxPlatform',
    'appIdFor',
    'APP_ID_PREFIX',
    'DESKTOP_PLATFORMS',
    'SELF_IMPORTING_SHIMS',
    'PLATFORM_OVERRIDES',
    'MISSING_MODULES',
  ]) {
    assert.notEqual(metroConfig[name], undefined, `metro-config no longer exports ${name}`);
  }
});

test('react-native.config.js loads with nothing but Node', () => {
  // React Native's CLI reads this from the package root by path convention
  // rather than through `exports`, so it has to resolve without a bundler,
  // without a transpiler, and without this repository's own layout.
  const config = require(
    path.join(__dirname, '..', 'packages/basalt-core/react-native.config.js'),
  );
  assert.deepEqual(Object.keys(config.platforms).sort(), ['linux', 'macos', 'windows']);
  for (const platform of Object.values(config.platforms)) {
    assert.equal(typeof platform.projectConfig, 'function');
    assert.equal(platform.dependencyConfig(), null);
  }
});
