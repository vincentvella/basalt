/**
 * Tests for scripts/check_support.js, which renders the platform support page.
 *
 * The data file is correct today, so the cases worth checking are the ones it
 * does not contain: a status nobody has typed yet, a row that is not done
 * everywhere and does not say why, a link to a file that has been renamed, and a
 * prop a host claims and does not read. Each of those is a way the page can
 * start lying, and none of them is reachable by running the real data through
 * the real tree.
 *
 * Run with:  node --test scripts/test_check_support.js
 *
 * @format
 */

'use strict';

const assert = require('node:assert');
const {test} = require('node:test');

const {validate, render, STATUSES} = require('./check_support.js');

const PLATFORMS = [
  {key: 'linux', label: 'Linux', toolkit: 'GTK4'},
  {key: 'macos', label: 'macOS', toolkit: 'AppKit'},
];

function data(rows) {
  return {platforms: PLATFORMS, areas: [{title: 'An area', rows}]};
}

test('a status nobody has typed is a problem', () => {
  const problems = validate(data([{feature: 'a', linux: 'yes', macos: 'maybe'}]));
  // Two: the status, and the missing why that anything short of done needs --
  // which is the point of reporting all of them rather than the first.
  assert.equal(problems.length, 2);
  assert.ok(problems.some((problem) => /not a status/.test(problem)));
});

test('a missing status is a problem of its own', () => {
  const problems = validate(data([{feature: 'a', linux: 'yes'}]));
  assert.equal(problems.length, 2); // no status for macos, and no why
  assert.match(problems[0], /no status for macos/);
});

test('anything short of done everywhere has to say why', () => {
  assert.equal(validate(data([{feature: 'a', linux: 'yes', macos: 'yes'}])).length, 0);

  const problems = validate(data([{feature: 'a', linux: 'yes', macos: 'no'}]));
  assert.equal(problems.length, 1);
  assert.match(problems[0], /says no why/);

  // Including when every host is the same kind of not-done, which is the shape
  // a deliberately ignored prop has.
  const ignored = validate(data([{feature: 'a', linux: 'ignored', macos: 'ignored'}]));
  assert.equal(ignored.length, 1);
});

test('a why needs both halves', () => {
  const problems = validate(
    data([{feature: 'a', linux: 'yes', macos: 'no', why: {text: 'because'}}]),
  );
  assert.equal(problems.length, 1);
  assert.match(problems[0], /text and a link/);
});

test('a why that links into this repository has to point at a file that is here', () => {
  const gone = validate(
    data([
      {
        feature: 'a',
        linux: 'yes',
        macos: 'no',
        why: {text: 'x', link: 'https://github.com/vincentvella/basalt/blob/main/docs/backlog/gone.md'},
      },
    ]),
  );
  assert.equal(gone.length, 1);
  assert.match(gone[0], /not in the repository/);

  // One that is here passes, and so does a link that leaves the repository.
  const here = validate(
    data([
      {
        feature: 'a',
        linux: 'yes',
        macos: 'no',
        why: {text: 'x', link: 'https://github.com/vincentvella/basalt/blob/main/docs/TESTING.md'},
      },
    ]),
  );
  assert.equal(here.length, 0);

  const elsewhere = validate(
    data([{feature: 'a', linux: 'yes', macos: 'no', why: {text: 'x', link: 'https://example.com/'}}]),
  );
  assert.equal(elsewhere.length, 0);
});

test('an empty area is a problem', () => {
  assert.match(validate({platforms: PLATFORMS, areas: [{title: 'Empty', rows: []}]})[0], /no rows/);
});

test('the page carries a mark for every status and a bar per platform', () => {
  const page = render(
    data([
      {feature: 'done', linux: 'yes', macos: 'yes'},
      {feature: 'not', linux: 'no', macos: 'no', why: {text: 'x', link: 'https://example.com/'}},
    ]),
  );
  for (const {mark} of Object.values(STATUSES)) {
    assert.ok(page.includes(mark), `the legend is missing ${mark}`);
  }
  // One of two rows done, so the bar is half full and says so.
  assert.ok(page.includes('1 of 2'), 'the summary does not count the rows');
  assert.match(page, /█{10}░{10}/);
  // And the why is a link rather than bare text.
  assert.ok(page.includes('[x](https://example.com/)'));
});

test('an unknown status renders rather than throwing', () => {
  // The validator reports it; the renderer must not hide that behind a stack.
  const page = render(data([{feature: 'a', linux: 'yes', macos: 'maybe'}]));
  assert.ok(page.includes('?'));
});
