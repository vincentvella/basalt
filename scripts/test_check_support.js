/**
 * Tests for scripts/check_support.js, which renders the platform support page
 * from React Native's scraped prop list and this repository's statuses.
 *
 * The real data is correct today, so what is worth testing is the ways it can
 * start lying: a prop upstream has added and nobody has judged, a status for a
 * prop that no longer exists, a row that is not done everywhere and says no why,
 * a why that links a file that has been renamed, and a status nobody has typed
 * before. None of those is reachable by running the real files through the real
 * tree, which is the argument for feeding them as data.
 *
 * Run with:  node --test scripts/test_check_support.js
 *
 * @format
 */

'use strict';

const assert = require('node:assert');
const {test} = require('node:test');

const {validate, render, counts, STATUSES} = require('./check_support.js');

const PLATFORMS = [
  {key: 'linux', label: 'Linux', toolkit: 'GTK4'},
  {key: 'macos', label: 'macOS', toolkit: 'AppKit'},
];

function inventoryOf(props) {
  return {
    reactNative: '0.87.1',
    structs: [{name: 'BaseViewProps', header: 'a/header.h', note: 'A note.', props}],
  };
}

function dataOf(statuses, whys = {because: {text: 'because', link: 'https://example.com/'}}) {
  return {platforms: PLATFORMS, whys, statuses, areas: []};
}

test('a prop React Native declares and nobody has judged is a problem', () => {
  const problems = validate(inventoryOf(['opacity', 'newUpstreamProp']), dataOf({
    'BaseViewProps.opacity': {linux: 'yes', macos: 'yes'},
  }));
  assert.equal(problems.length, 1);
  assert.match(problems[0], /newUpstreamProp.*has no status/);
});

test('a status for a prop that is no longer declared is a problem', () => {
  const problems = validate(inventoryOf(['opacity']), dataOf({
    'BaseViewProps.opacity': {linux: 'yes', macos: 'yes'},
    'BaseViewProps.goneUpstream': {linux: 'yes', macos: 'yes'},
  }));
  assert.equal(problems.length, 1);
  assert.match(problems[0], /goneUpstream.*not declared/);
});

test('anything short of done everywhere has to say why', () => {
  const fine = validate(inventoryOf(['opacity']), dataOf({
    'BaseViewProps.opacity': {linux: 'yes', macos: 'yes'},
  }));
  assert.equal(fine.length, 0);

  const problems = validate(inventoryOf(['opacity']), dataOf({
    'BaseViewProps.opacity': {linux: 'yes', macos: 'no'},
  }));
  assert.equal(problems.length, 1);
  assert.match(problems[0], /says no why/);

  // Including when every host is the same kind of not-done, which is the shape
  // a deliberately ignored prop has.
  const ignored = validate(inventoryOf(['opacity']), dataOf({
    'BaseViewProps.opacity': {linux: 'ignored', macos: 'ignored'},
  }));
  assert.equal(ignored.length, 1);
});

test('a status nobody has typed before is a problem', () => {
  const problems = validate(inventoryOf(['opacity']), dataOf({
    'BaseViewProps.opacity': {linux: 'yes', macos: 'maybe', why: 'because'},
  }));
  assert.equal(problems.length, 1);
  assert.match(problems[0], /not a status/);
});

test('a why has to name a key that exists', () => {
  const problems = validate(inventoryOf(['opacity']), dataOf({
    'BaseViewProps.opacity': {linux: 'yes', macos: 'no', why: 'nosuchkey'},
  }));
  assert.equal(problems.length, 1);
  assert.match(problems[0], /not in whys/);
});

test('a why that links into this repository has to point at a file that is here', () => {
  const base = {'BaseViewProps.opacity': {linux: 'yes', macos: 'no', why: 'because'}};
  const gone = validate(
    inventoryOf(['opacity']),
    dataOf(base, {
      because: {
        text: 'x',
        link: 'https://github.com/vincentvella/basalt/blob/main/docs/backlog/gone.md',
      },
    }),
  );
  assert.equal(gone.length, 1);
  assert.match(gone[0], /not in the repository/);

  const here = validate(
    inventoryOf(['opacity']),
    dataOf(base, {
      because: {
        text: 'x',
        link: 'https://github.com/vincentvella/basalt/blob/main/docs/TESTING.md',
      },
    }),
  );
  assert.equal(here.length, 0);
});

test('a prop that ReactCommon consumes counts as done', () => {
  const inventory = inventoryOf(['onLayout', 'cursor']);
  const data = dataOf({
    'BaseViewProps.onLayout': {linux: 'upstream', macos: 'upstream', why: 'because'},
    'BaseViewProps.cursor': {linux: 'yes', macos: 'no', why: 'because'},
  });
  assert.deepEqual(counts(inventory, data, 'linux'), {done: 2, total: 2});
  assert.deepEqual(counts(inventory, data, 'macos'), {done: 1, total: 2});
});

test('the page carries one row per attribute, a mark per status and a bar per platform', () => {
  const page = render(
    inventoryOf(['opacity', 'cursor']),
    dataOf({
      'BaseViewProps.opacity': {linux: 'yes', macos: 'yes'},
      'BaseViewProps.cursor': {linux: 'no', macos: 'no', why: 'because'},
    }),
  );
  assert.ok(page.includes('| `opacity` |'));
  assert.ok(page.includes('| `cursor` |'));
  for (const {mark} of Object.values(STATUSES)) {
    assert.ok(page.includes(mark), `the legend is missing ${mark}`);
  }
  // One of two done, so the bar is half full and the count says so.
  assert.ok(page.includes('1 of 2'));
  assert.match(page, /█{10}░{10}/);
  // The why is a link rather than bare text, and the struct's header is named.
  assert.ok(page.includes('[because](https://example.com/)'));
  assert.ok(page.includes('a/header.h'));
});

test('an unknown status renders rather than throwing', () => {
  const page = render(
    inventoryOf(['opacity']),
    dataOf({'BaseViewProps.opacity': {linux: 'yes', macos: 'maybe', why: 'because'}}),
  );
  assert.ok(page.includes('?'));
});
