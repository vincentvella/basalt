/**
 * Checks the backlog's own arithmetic: every file's open count against the
 * entries it lists, and docs/BACKLOG.md's table against every file.
 *
 * The numbers are load-bearing. `docs/BACKLOG.md` is a table of areas and how
 * much is open in each, and each file repeats its own count in an `**Open (n)**`
 * header above the list -- so a person deciding what to pick up is reading three
 * numbers that are maintained by hand, in a repository where a commit routinely
 * strikes one entry and adds another.
 *
 * They drift, which is the reason this exists rather than a worry about it. On
 * 2026-10-08, counting them for the first time found `accessibility.md` saying
 * two with four open, and the commit that found it had already nearly shipped a
 * wrong count of its own in `correctness.md`. Neither would ever fail a test,
 * because nothing read them.
 *
 * ## What counts as open
 *
 * An entry is a numbered line in the list under the `**Open (n):**` header. It
 * is closed when its own text is struck -- `3. ~~thing~~, done on both hosts` --
 * and open otherwise. A struck note *inside* an open entry does not close it,
 * which is a distinction these files rely on: "accessibilityActions is ignored
 * (~~accessibilityLabelledBy~~ and ~~accessibilityLiveRegion~~ are done)" is one
 * open entry with two finished notes in it.
 *
 * Deliberately not checked: whether the list matches the prose below it. The
 * entries are summaries and the prose is the work, and several files say so
 * outright by writing one entry for a group of related props. A checker that
 * insisted on a one-to-one mapping would be wrong about the files rather than
 * the other way round.
 *
 * Run with:  node scripts/check_backlog.js
 *
 * @format
 */

'use strict';

const fs = require('node:fs');
const path = require('node:path');

const DOCS = path.join(__dirname, '..', 'docs');
const BACKLOG = path.join(DOCS, 'BACKLOG.md');
const BACKLOG_DIR = path.join(DOCS, 'backlog');

// The header each file repeats its count in.
const OPEN_HEADER = /\*\*Open \((\d+)\):\*\*/;
// A numbered entry, and the same line struck.
const ENTRY = /^(\d+)\. /;
const STRUCK_ENTRY = /^\d+\. ~~/;
// One row of BACKLOG.md's table: a linked area and its count.
const TABLE_ROW = /^\| \[([^\]]+)\]\(backlog\/([a-z0-9-]+\.md)\) \| (\d+) \|/;

// The entries listed under the header, and how many of them are open. Returns
// null when a file has no header at all, which is not an error: a few of these
// files are prose with no list.
function openEntries(text) {
  const header = text.match(OPEN_HEADER);
  if (header === null) {
    return null;
  }
  const after = text.slice(header.index + header[0].length).split('\n');
  const entries = [];
  let started = false;
  for (const line of after) {
    if (ENTRY.test(line)) {
      entries.push(line);
      started = true;
    } else if (started && (line.trim() === '' || !/^\s/.test(line))) {
      // The list ends at the first line that is neither an entry nor the
      // continuation of one, so a wrapped entry stays part of it.
      break;
    }
  }
  return {
    said: Number(header[1]),
    entries,
    open: entries.filter((line) => !STRUCK_ENTRY.test(line)).length,
  };
}

function main() {
  const problems = [];
  const counts = new Map();
  const files = fs
    .readdirSync(BACKLOG_DIR)
    .filter((name) => name.endsWith('.md'))
    .sort();

  for (const name of files) {
    const file = path.join(BACKLOG_DIR, name);
    const counted = openEntries(fs.readFileSync(file, 'utf8'));
    if (counted === null) {
      continue;
    }
    counts.set(name, counted.said);
    if (counted.said !== counted.open) {
      problems.push(
        `docs/backlog/${name}: the header says ${counted.said} open, `
          + `${counted.open} of its ${counted.entries.length} entries are not struck`,
      );
    }
  }

  const table = fs.readFileSync(BACKLOG, 'utf8').split('\n');
  let rows = 0;
  for (const line of table) {
    const row = line.match(TABLE_ROW);
    if (row === null) {
      continue;
    }
    rows++;
    const [, title, name, count] = row;
    if (!counts.has(name)) {
      problems.push(`docs/BACKLOG.md: ${title} links backlog/${name}, which has no open header`);
      continue;
    }
    if (Number(count) !== counts.get(name)) {
      problems.push(
        `docs/BACKLOG.md: ${title} says ${count}, backlog/${name} says ${counts.get(name)}`,
      );
    }
  }

  for (const problem of problems) {
    console.log(problem);
  }
  console.log(
    `${counts.size} backlog file${counts.size === 1 ? '' : 's'} checked, `
      + `${rows} table row${rows === 1 ? '' : 's'}, `
      + `${problems.length} count${problems.length === 1 ? '' : 's'} out of step`,
  );
  return problems.length === 0 ? 0 : 1;
}

// Required by scripts/test_check_backlog.js, which feeds text through the
// counting directly: a guard with no test of its own can stop guarding quietly.
module.exports = {openEntries, OPEN_HEADER, TABLE_ROW};

if (require.main === module) {
  process.exitCode = main();
}
