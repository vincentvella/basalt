/**
 * Tests for scripts/check_backlog.js, which checks the backlog's own counts.
 *
 * The counting is fed text here rather than files, for the reason
 * scripts/test_check_includes.js gives: the cases worth checking are the ones
 * the tree does not contain, and a tree that happens to be consistent today
 * proves nothing about the rule that keeps it so. Every shape below has been in
 * one of these files at some point -- the struck entry, the struck note inside
 * an open entry, the wrapped entry, the prose file with no list.
 *
 * Run with:  node --test scripts/test_check_backlog.js
 *
 * @format
 */

'use strict';

const assert = require('node:assert');
const {test} = require('node:test');

const {openEntries} = require('./check_backlog.js');

test('an entry struck in its own text is closed', () => {
  const counted = openEntries(
    ['**Open (2):**', '', '1. ~~done~~, on both hosts', '2. open', '3. also open', ''].join('\n'),
  );
  assert.equal(counted.said, 2);
  assert.equal(counted.entries.length, 3);
  assert.equal(counted.open, 2);
});

test('a struck note inside an entry leaves the entry open', () => {
  // accessibility.md's fourth entry, which is the shape that made the naive
  // rule -- "any strikethrough on the line" -- give the wrong answer.
  const counted = openEntries(
    [
      '**Open (1):**',
      '',
      '1. accessibilityActions is ignored (~~accessibilityLabelledBy~~ and',
      '   ~~accessibilityLiveRegion~~ are done on GTK and AppKit)',
      '',
    ].join('\n'),
  );
  assert.equal(counted.entries.length, 1);
  assert.equal(counted.open, 1);
});

test('a wrapped entry is one entry', () => {
  const counted = openEntries(
    [
      '**Open (2):**',
      '',
      '1. a long entry that runs onto',
      '   a second line, indented',
      '2. another',
      '',
      '- the prose below, which is not counted',
    ].join('\n'),
  );
  assert.equal(counted.entries.length, 2);
  assert.equal(counted.open, 2);
});

test('the list ends before the prose, even when the prose is numbered later', () => {
  const counted = openEntries(
    [
      '**Open (1):**',
      '',
      '1. the only entry',
      '',
      '- **The prose.** Which mentions 1. and 2. in passing.',
      '',
      '1. a numbered line in the prose, long after the list',
    ].join('\n'),
  );
  assert.equal(counted.entries.length, 1);
});

test('a file with no open header is not a file with no entries', () => {
  assert.equal(openEntries('# Prose\n\nNo list here.\n'), null);
});

test('a count that disagrees with the entries is what this is for', () => {
  const counted = openEntries(['**Open (2):**', '', '1. one', '2. two', '3. three', ''].join('\n'));
  assert.equal(counted.said, 2);
  assert.notEqual(counted.said, counted.open);
});
