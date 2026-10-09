/**
 * Renders the platform support page, and checks what a script can check.
 *
 * `docs/platform-support.json` says what each host supports; this turns it into
 * `website/docs/support.mdx` and, with `--check`, fails when the two differ.
 * One source, one rendering, so three columns of ticks cannot drift from each
 * other the way `docs/BACKLOG.md`'s counts drifted from the files they index --
 * see scripts/check_backlog.js, which exists because they did.
 *
 * ## What it can and cannot verify
 *
 * It can verify the shape: every status is one of the five legal ones, anything
 * short of `yes` says why and links somewhere that exists, and no area is empty.
 *
 * It can also do one real check, for any row that names a C++ prop: the host
 * sources are searched for that prop, and a `yes` that no host source mentions
 * fails, as does a `no` in a host that mentions it. That catches the two
 * mistakes a hand-maintained matrix actually makes -- a box ticked before the
 * work, and work finished without the box.
 *
 * What it cannot do is prove behaviour. A mention is not an implementation, and
 * the page says so: what proves a prop behaves is the scenario in
 * `scripts/integration_test.py` that exercises it on every host. This is the
 * index, not the evidence.
 *
 * Run with:  node scripts/check_support.js            # rewrite the page
 *            node scripts/check_support.js --check    # fail if it is stale
 *
 * @format
 */

'use strict';

const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const DATA = path.join(ROOT, 'docs', 'platform-support.json');
const PAGE = path.join(ROOT, 'website', 'docs', 'support.mdx');

// Where a prop is read, per platform: the mounting manager and the view layer of
// each host, which is where every prop in the matrix is either handled or not.
const HOST_DIRS = {
  linux: ['packages/basalt-gtk/native/gtk'],
  macos: ['packages/basalt-appkit/native/appkit'],
  windows: ['packages/basalt-win32/native/win32'],
};

const STATUSES = {
  yes: {mark: '✅', label: 'done'},
  partial: {mark: '◐', label: 'partly done'},
  no: {mark: '✖', label: 'not yet'},
  upstream: {mark: '△', label: "ReactCommon's"},
  ignored: {mark: '–', label: 'deliberately not done'},
};

function platformKeys(data) {
  return data.platforms.map((platform) => platform.key);
}

// Every file of a host's sources, flattened once: the matrix is small and the
// directories are two deep, so a read of each beats a grep per row.
function hostSources(dirs) {
  const files = [];
  for (const dir of dirs) {
    const full = path.join(ROOT, dir);
    if (!fs.existsSync(full)) {
      continue;
    }
    for (const name of fs.readdirSync(full)) {
      const file = path.join(full, name);
      if (fs.statSync(file).isFile()) {
        files.push(fs.readFileSync(file, 'utf8'));
      }
    }
  }
  return files;
}

// What counts as a host reading a prop. `props->x` and `props.x` by default,
// which is how a mounting manager reads one -- and whatever `reads` says when it
// is read through something else: the border radii arrive through
// `resolveBorderMetrics`, the shadows through core's `allShadows`, the transform
// through `resolveTransform`. A row that needs `reads` is a row where the prop
// name alone would be the only evidence, and the data file says so rather than
// this file guessing.
function mentionsProp(sources, row) {
  const needles = row.reads !== undefined
    ? row.reads
    : [`props->${row.prop}`, `props.${row.prop}`];
  return sources.some((text) => needles.some((needle) => text.includes(needle)));
}

function validate(data) {
  const problems = [];
  const keys = platformKeys(data);

  for (const area of data.areas) {
    if (!area.rows || area.rows.length === 0) {
      problems.push(`area ${area.title!= null ? area.title : '(unnamed)'} has no rows`);
      continue;
    }
    for (const row of area.rows) {
      for (const key of keys) {
        const status = row[key];
        if (status === undefined) {
          problems.push(`${row.feature}: no status for ${key}`);
          continue;
        }
        if (!(status in STATUSES)) {
          problems.push(`${row.feature}: ${key} is ${JSON.stringify(status)}, which is not a status`);
        }
      }
      const everything = keys.every((key) => row[key] === 'yes');
      if (!everything && row.why === undefined) {
        problems.push(`${row.feature}: not done everywhere and says no why`);
      }
      if (row.why !== undefined && (!row.why.text || !row.why.link)) {
        problems.push(`${row.feature}: a why needs text and a link`);
      }
      // A link into this repository has to point at a file that is here. The
      // backlog is deliberately not on the documentation site -- see
      // website/docusaurus.config.ts, which excludes it -- so these are links to
      // the repository, and a renamed file would otherwise 404 quietly.
      if (row.why !== undefined && typeof row.why.link === 'string') {
        const inRepo = row.why.link.match(/blob\/main\/(.*)$/);
        if (inRepo !== null && !fs.existsSync(path.join(ROOT, inRepo[1]))) {
          problems.push(`${row.feature}: its why links ${inRepo[1]}, which is not in the repository`);
        }
      }
    }
  }
  return problems;
}

// The one check that reads the code rather than the file.
function audit(data) {
  const problems = [];
  const sources = {};
  for (const [key, dirs] of Object.entries(HOST_DIRS)) {
    sources[key] = hostSources(dirs);
  }

  for (const area of data.areas) {
    for (const row of area.rows) {
      if (!row.prop && !row.reads) {
        continue;
      }
      const what = row.reads !== undefined ? row.reads.join(' or ') : `props->${row.prop}`;
      for (const [key, text] of Object.entries(sources)) {
        if (text.length === 0) {
          continue;
        }
        const mentioned = mentionsProp(text, row);
        if (row[key] === 'yes' && !mentioned) {
          problems.push(`${row.feature}: ${key} says done and no source there reads ${what}`);
        }
        if (row[key] === 'no' && mentioned) {
          problems.push(`${row.feature}: ${key} says not yet and a source there reads ${what}`);
        }
      }
    }
  }
  return problems;
}

function countsFor(data, key) {
  const counts = {};
  for (const status of Object.keys(STATUSES)) {
    counts[status] = 0;
  }
  for (const area of data.areas) {
    for (const row of area.rows) {
      counts[row[key]] = (counts[row[key]] || 0) + 1;
    }
  }
  return counts;
}

function bar(done, total) {
  // Twenty cells, so one cell is one twentieth and the bar is the same width on
  // every row: a chart in a markdown table, which renders on the site and on
  // GitHub without a chart library.
  const filled = total === 0 ? 0 : Math.round((done / total) * 20);
  return '█'.repeat(filled) + '░'.repeat(20 - filled);
}

function render(data) {
  const keys = platformKeys(data);
  const lines = [];

  lines.push('---');
  lines.push('title: Platform support');
  lines.push('sidebar_position: 4');
  lines.push('---');
  lines.push('');
  lines.push('{/* Generated by scripts/check_support.js from docs/platform-support.json. */}');
  lines.push('{/* Edit that file, then run `node scripts/check_support.js`. */}');
  lines.push('');
  lines.push('# Platform support');
  lines.push('');
  lines.push(
    'Three desktops, three native toolkits, and not the same amount finished on',
  );
  lines.push(
    'each. This is the index of what works where; anything short of done links to',
  );
  lines.push('the entry that says what is missing and which call would do it.');
  lines.push('');
  lines.push('| | ' + keys.map((key) => data.platforms.find((p) => p.key === key).label).join(' | ') + ' |');
  lines.push('| --- | ' + keys.map(() => '---').join(' | ') + ' |');
  lines.push(
    '| Toolkit | ' + keys.map((key) => data.platforms.find((p) => p.key === key).toolkit).join(' | ') + ' |',
  );

  const total = data.areas.reduce((sum, area) => sum + area.rows.length, 0);
  const doneRow = [];
  const barRow = [];
  for (const key of keys) {
    const counts = countsFor(data, key);
    const done = counts.yes + counts.upstream;
    doneRow.push(`${done} of ${total}`);
    barRow.push('`' + bar(done, total) + '`');
  }
  lines.push('| Done | ' + doneRow.join(' | ') + ' |');
  lines.push('| | ' + barRow.join(' | ') + ' |');
  lines.push('');
  lines.push('Counting a row as done when the host implements it or ReactCommon does.');
  lines.push('');

  lines.push('| | |');
  lines.push('| --- | --- |');
  for (const [status, {mark, label}] of Object.entries(STATUSES)) {
    lines.push(`| ${mark} | ${label} |`);
  }
  lines.push('');
  lines.push(
    '**A tick is an index entry, not a proof.** What proves a prop behaves is the',
  );
  lines.push(
    'end-to-end scenario that exercises it on every host, in',
  );
  lines.push(
    '`scripts/integration_test.py`; `scripts/check_support.js` keeps this page in',
  );
  lines.push('step with the code as far as a script can.');
  lines.push('');

  for (const area of data.areas) {
    lines.push(`## ${area.title}`);
    lines.push('');
    if (area.note) {
      lines.push(area.note);
      lines.push('');
    }
    lines.push(
      '| | ' + keys.map((key) => data.platforms.find((p) => p.key === key).label).join(' | ') + ' | |',
    );
    lines.push('| --- | ' + keys.map(() => '---').join(' | ') + ' | --- |');
    for (const row of area.rows) {
      // An unknown status is reported by the validator above; this renders a
      // question mark rather than throwing, so one bad row cannot hide the rest
      // of the problems behind a stack trace.
      const cells = keys.map((key) => (STATUSES[row[key]] !== undefined ? STATUSES[row[key]].mark : '?'));
      const why = row.why ? `[${row.why.text}](${row.why.link})` : '';
      lines.push(`| ${row.feature} | ${cells.join(' | ')} | ${why} |`);
    }
    lines.push('');
  }

  return lines.join('\n');
}

function main() {
  const check = process.argv.includes('--check');
  const data = JSON.parse(fs.readFileSync(DATA, 'utf8'));

  const problems = validate(data).concat(audit(data));
  for (const problem of problems) {
    console.log(problem);
  }

  const rendered = render(data) + '\n';
  const existing = fs.existsSync(PAGE) ? fs.readFileSync(PAGE, 'utf8') : null;
  const stale = existing !== rendered;

  if (check) {
    if (stale) {
      console.log(
        'website/docs/support.mdx is not what docs/platform-support.json renders to;'
          + ' run node scripts/check_support.js',
      );
    }
  } else if (stale) {
    fs.writeFileSync(PAGE, rendered);
    console.log(`wrote ${path.relative(ROOT, PAGE)}`);
  }

  const total = data.areas.reduce((sum, area) => sum + area.rows.length, 0);
  console.log(
    `${total} rows across ${data.areas.length} areas, ${problems.length} problem${problems.length === 1 ? '' : 's'}`,
  );
  return problems.length === 0 && !(check && stale) ? 0 : 1;
}

module.exports = {validate, audit, render, STATUSES};

if (require.main === module) {
  process.exitCode = main();
}
