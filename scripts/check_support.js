/**
 * Renders the platform support page, and checks what a script can check.
 *
 * One row per attribute, and the attributes are not ours to list:
 * `scripts/scrape_props.py` reads them out of React Native's own
 * `BaseViewProps`, `AccessibilityProps` and `TextAttributes` and writes
 * `docs/react-native-props.json`. The statuses live beside it in
 * `docs/platform-support.json`, keyed by `Struct.prop`, and this renders
 * `website/docs/support.mdx` from the two.
 *
 * ## What that arrangement buys
 *
 * A prop added upstream has no status, and the check fails until somebody says
 * what this platform does with it. That is the failure a hand-written table
 * cannot produce: a missing row looks exactly like a row nobody filled in.
 * It works the other way too -- a status for a prop that upstream has removed or
 * renamed is reported rather than rendered.
 *
 * And one real check against the code: for every row whose host column says
 * `yes`, that host's sources have to read the prop, and for every `no` they have
 * to not. That caught `accessibilityValue` claiming Windows support that was
 * never written, which the previous grouped table hid because its row named no
 * prop.
 *
 * What it cannot do is prove behaviour. A mention is not an implementation, and
 * the page says so: the scenarios in `scripts/integration_test.py` are the
 * proof. This is the index.
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
const INVENTORY = path.join(ROOT, 'docs', 'react-native-props.json');
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

// The three kinds a field can be, which `scripts/scrape_props.py` reads off
// React Native's own type declarations rather than deciding.
//
// reactnative.dev splits a component's props from the style props its `style`
// takes, and that split is worth keeping: `opacity` and `onLayout` are fields
// side by side in `BaseViewProps`, and only one of them is written inside
// `style={{...}}`. The third kind is the honest remainder.
const KINDS = {
  style: {
    title: 'Style props',
    note: 'Written inside `style={{...}}`.',
  },
  component: {
    title: 'Component props',
    note: 'Written on the element itself, as `<View onLayout={...} />`.',
  },
  internal: {
    title: 'Neither',
    note:
      'Declared in ReactCommon and in no public prop type, so nothing an app'
      + ' writes reaches them: they are filled in by the renderer or are the'
      + " innards of a component's own behaviour.",
  },
};

// How a host reads a prop, spelled tightly enough to mean it.
//
// A first attempt allowed a bare `.prop` as a fallback and reported ten hosts as
// reading props they do not: `.opacity` is the *view's* opacity, `.role` is the
// accessible role, `.layoutDirection` is a layout field. A looser needle makes
// the check useless in the direction that matters, since every `no` becomes a
// false alarm.
//
// `reads` on a status row overrides this, for a prop a host reaches through
// something else: the border radii arrive through `resolveBorderMetrics`, the
// shadows through core's `allShadows`, the transform through `resolveTransform`.
function needles(struct, prop, row) {
  if (row.reads !== undefined) {
    return row.reads;
  }
  if (struct === 'TextAttributes') {
    return [`textAttributes.${prop}`, `attributes.${prop}`];
  }
  return [`props->${prop}`, `props.${prop}`, `accessibility.${prop}`];
}

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

// Prose from the inventory goes into an MDX file, where `<View>` is a component
// that does not exist and fails the site build. Anything shaped like a tag
// becomes code, which is how the rest of these docs write a component's name
// anyway. Guarded here rather than only in the text, because the text comes from
// another file and the next note to mention a component should not break the
// build.
function mdxSafe(text) {
  return text.replace(/`?<([A-Za-z][A-Za-z0-9]*)>`?/g, '`<$1>`');
}

function platformKeys(data) {
  return data.platforms.map((platform) => platform.key);
}

function label(data, key) {
  return data.platforms.find((platform) => platform.key === key).label;
}

// Every status is legal, every row that is not done everywhere says why, and
// every why names a key that exists and links somewhere that does.
function validate(inventory, data) {
  const problems = [];
  const keys = platformKeys(data);
  const declared = new Set();

  for (const struct of inventory.structs) {
    for (const prop of struct.props) {
      const id = `${struct.name}.${prop.name}`;
      declared.add(id);
      if (KINDS[prop.kind] === undefined) {
        problems.push(
          `${id}: its kind is ${JSON.stringify(prop.kind)}; run scripts/scrape_props.py`,
        );
      }
      const row = data.statuses[id];
      if (row === undefined) {
        problems.push(
          `${id} is declared by React Native and has no status: say what this platform does with it`,
        );
        continue;
      }
      for (const key of keys) {
        if (!(row[key] in STATUSES)) {
          problems.push(`${id}: ${key} is ${JSON.stringify(row[key])}, which is not a status`);
        }
      }
      const everything = keys.every((key) => row[key] === 'yes');
      if (!everything && row.why === undefined) {
        problems.push(`${id}: not done everywhere and says no why`);
      }
      if (row.why !== undefined && data.whys[row.why] === undefined) {
        problems.push(`${id}: its why is ${row.why}, which is not in whys`);
      }
    }
  }

  for (const id of Object.keys(data.statuses)) {
    if (!declared.has(id)) {
      problems.push(`${id} has a status and is not declared by React Native any more`);
    }
  }

  for (const [key, why] of Object.entries(data.whys)) {
    if (!why.text || !why.link) {
      problems.push(`the why ${key} needs text and a link`);
      continue;
    }
    const inRepo = why.link.match(/blob\/main\/(.*)$/);
    if (inRepo !== null && !fs.existsSync(path.join(ROOT, inRepo[1]))) {
      problems.push(`the why ${key} links ${inRepo[1]}, which is not in the repository`);
    }
  }

  return problems;
}

// The one check that reads the code rather than the file.
function audit(inventory, data) {
  const problems = [];
  const sources = {};
  for (const [key, dirs] of Object.entries(HOST_DIRS)) {
    sources[key] = hostSources(dirs);
  }

  for (const struct of inventory.structs) {
    for (const prop of struct.props) {
      const row = data.statuses[`${struct.name}.${prop.name}`];
      if (row === undefined) {
        continue;
      }
      const what = needles(struct.name, prop.name, row);
      for (const [key, text] of Object.entries(sources)) {
        if (text.length === 0) {
          continue;
        }
        const mentioned = text.some((file) => what.some((needle) => file.includes(needle)));
        if (row[key] === 'yes' && !mentioned) {
          problems.push(
            `${struct.name}.${prop.name}: ${key} says done and no source there reads it`,
          );
        }
        if (row[key] === 'no' && mentioned) {
          problems.push(
            `${struct.name}.${prop.name}: ${key} says not yet and a source there reads it`,
          );
        }
      }
    }
  }
  return problems;
}

function counts(inventory, data, key) {
  let done = 0;
  let total = 0;
  for (const struct of inventory.structs) {
    for (const prop of struct.props) {
      const row = data.statuses[`${struct.name}.${prop.name}`];
      if (row === undefined) {
        continue;
      }
      total++;
      if (row[key] === 'yes' || row[key] === 'upstream') {
        done++;
      }
    }
  }
  for (const area of data.areas) {
    for (const row of area.rows) {
      total++;
      if (row[key] === 'yes' || row[key] === 'upstream') {
        done++;
      }
    }
  }
  return {done, total};
}

function bar(done, total) {
  const filled = total === 0 ? 0 : Math.round((done / total) * 20);
  return '█'.repeat(filled) + '░'.repeat(20 - filled);
}

function render(inventory, data) {
  const keys = platformKeys(data);
  const lines = [];

  lines.push('---');
  lines.push('title: Platform support');
  lines.push('sidebar_position: 4');
  lines.push('---');
  lines.push('');
  lines.push('{/* Generated by scripts/check_support.js. Do not edit. */}');
  lines.push('{/* The attributes come from scripts/scrape_props.py, the statuses from */}');
  lines.push('{/* docs/platform-support.json. Run both after changing either. */}');
  lines.push('');
  lines.push('# Platform support');
  lines.push('');
  lines.push('Three desktops, three native toolkits, and not the same amount finished on');
  lines.push('each. One row per attribute, with anything short of done linking to the entry');
  lines.push('that says what is missing and which call would do it.');
  lines.push('');
  lines.push(
    `The attributes are React Native's own, read out of its declarations rather than`,
  );
  lines.push(
    `listed here: \`scripts/scrape_props.py\` scrapes React Native ${inventory.reactNative}`,
  );
  lines.push('and this renders what it found, so a prop added upstream turns up as a row');
  lines.push('with no status and fails the check until somebody fills it in.');
  lines.push('');
  lines.push('**Every style name a `<View>` or a `<Text>` takes has a row.** Most are');
  lines.push("fields in one of ReactCommon's prop structs. The rest belong to Yoga:");
  lines.push('`padding`, `margin`, `flex`, `inset`, `gap` and their spellings, which no');
  lines.push('struct declares one by one, because `YogaStylableProps` carries a single');
  lines.push("`yogaStyle` field. Those names come from React Native's own");
  lines.push('`____LayoutStyle_Internal` and sit in a section of their own. A handful are');
  lines.push('declared in JavaScript and read by no struct at all, and they are listed too,');
  lines.push('because an app can still write them.');
  lines.push('');
  lines.push('The scrape reports any style name that is none of those three, which is what');
  lines.push('makes this list complete rather than long: the layout props were missing from');
  lines.push('this page until somebody went looking for `padding`, and nothing could have');
  lines.push('said so.');
  lines.push('');
  lines.push('**What is not here yet**: the props of a `<TextInput>`, which has');
  lines.push('ReactCommon structs of its own that nothing scrapes, so some thirty names an');
  lines.push('app writes on one have no row. `backlog/textinput.md` records it with the');
  lines.push('shape of the fix, which is the one `<Image>` and the layout props already');
  lines.push('had.');
  lines.push('');
  lines.push('Each struct is split the way reactnative.dev splits a component page: the');
  lines.push('style props a `style={{...}}` takes, then the props written on the element');
  lines.push('itself. That split is read from React Native too, out of the Flow types that');
  lines.push('declare both, so a prop that moves between them moves here. Where');
  lines.push('ReactCommon spells a prop differently from JavaScript, the name an app writes');
  lines.push('is the one in the row and ReactCommon\'s is under it; where several names land');
  lines.push('in one field, as thirteen corner radii land in `borderRadii`, they are listed');
  lines.push('under the row that implements them.');
  lines.push('');

  lines.push('| | ' + keys.map((key) => label(data, key)).join(' | ') + ' |');
  lines.push('| --- | ' + keys.map(() => '---').join(' | ') + ' |');
  lines.push(
    '| Toolkit | '
      + keys.map((key) => data.platforms.find((p) => p.key === key).toolkit).join(' | ')
      + ' |',
  );
  const done = keys.map((key) => counts(inventory, data, key));
  lines.push('| Done | ' + done.map(({done: d, total}) => `${d} of ${total}`).join(' | ') + ' |');
  lines.push('| | ' + done.map(({done: d, total}) => '`' + bar(d, total) + '`').join(' | ') + ' |');
  lines.push('');
  lines.push('Counting a row as done when the host implements it, or when ReactCommon or');
  lines.push('Yoga does it for every host: a layout prop never reaches one of these');
  lines.push('toolkits, so there is nothing for a host to implement and nothing to claim.');
  lines.push('');

  lines.push('| | |');
  lines.push('| --- | --- |');
  for (const [, {mark, label: text}] of Object.entries(STATUSES)) {
    lines.push(`| ${mark} | ${text} |`);
  }
  lines.push('');
  lines.push('**A tick is an index entry, not a proof.** What proves a prop behaves is the');
  lines.push('end-to-end scenario that exercises it on every host, in');
  lines.push('`scripts/integration_test.py`. What this page is checked against is narrower');
  lines.push('and still worth having: a host claiming a prop has to read it, and a host');
  lines.push('disclaiming one has to not.');
  lines.push('');

  const table = (rows) => {
    // The last column is headed, the first is not: the first holds the prop
    // name, which needs no saying, and the last holds a reason that is empty on
    // every row done everywhere -- which is what made it look like a column
    // nobody meant to add.
    lines.push('| | ' + keys.map((key) => label(data, key)).join(' | ') + ' | Why |');
    lines.push('| --- | ' + keys.map(() => '---').join(' | ') + ' | --- |');
    for (const {feature, row} of rows) {
      const cells = keys.map((key) =>
        STATUSES[row[key]] !== undefined ? STATUSES[row[key]].mark : '?',
      );
      const why = row.why !== undefined && data.whys[row.why] !== undefined
        ? `[${data.whys[row.why].text}](${data.whys[row.why].link})`
        : '';
      lines.push(`| ${feature} | ${cells.join(' | ')} | ${why} |`);
    }
    lines.push('');
  };

  for (const struct of inventory.structs) {
    lines.push(`## ${struct.name}`);
    lines.push('');
    lines.push(`${mdxSafe(struct.note)} From \`${struct.header}\`.`);
    lines.push('');
    for (const [kind, {title, note}] of Object.entries(KINDS)) {
      const rows = struct.props
        .filter((prop) => prop.kind === kind)
        .filter((prop) => data.statuses[`${struct.name}.${prop.name}`] !== undefined)
        .map((prop) => ({
          // The name an app writes, which is not always ReactCommon's: it says
          // `borderRadii` for `borderRadius` and `foregroundColor` for `color`.
          // And every other name that lands in the same field, spelled out
          // rather than counted: the reason this page exists is somebody
          // looking for one name and wanting to know whether it works, so
          // `borderTopLeftRadius` has to be findable even though
          // `borderRadii` is what a host reads.
          feature: [
            prop.javascript === undefined
              ? `\`${prop.name}\``
              : `\`${prop.javascript}\` <sub>\`${prop.name}\`</sub>`,
            (prop.spellings ?? []).length > 0
              ? '<br/><sub>'
                + prop.spellings.map((name) => `\`${name}\``).join(' ')
                + '</sub>'
              : '',
          ].join(''),
          row: data.statuses[`${struct.name}.${prop.name}`],
        }));
      if (rows.length === 0) {
        continue;
      }
      lines.push(`### ${title}`);
      lines.push('');
      // The style note names the types it was read from rather than a
      // paraphrase of them: `ViewStyle` is the published name and
      // `____ViewStyle_InternalBase` is the one that declares the members.
      // Which Flow types these names were read from. Per struct where the
      // struct says -- Yoga's come from the layout type alone, and the ones
      // nothing reads come from wherever they are declared, which is what their
      // own note is for -- and the whole list otherwise.
      const from = struct.styleFrom !== undefined
        ? struct.styleFrom
        : (inventory.styleTypes ?? []).flatMap(({types}) => types);
      const where = kind === 'style' && from.length > 0
        ? ' Read from ' + from.map((type) => `\`${type}\``).join(', ') + '.'
        : '';
      lines.push(mdxSafe(note + where));
      lines.push('');
      table(rows);
    }
  }

  for (const area of data.areas) {
    lines.push(`## ${area.title}`);
    lines.push('');
    if (area.note) {
      lines.push(mdxSafe(area.note));
      lines.push('');
    }
    table(area.rows.map((row) => ({feature: row.feature, row})));
  }

  return lines.join('\n');
}

function main() {
  const check = process.argv.includes('--check');
  const inventory = JSON.parse(fs.readFileSync(INVENTORY, 'utf8'));
  const data = JSON.parse(fs.readFileSync(DATA, 'utf8'));

  const problems = validate(inventory, data).concat(audit(inventory, data));
  for (const problem of problems) {
    console.log(problem);
  }

  const rendered = render(inventory, data) + '\n';
  const existing = fs.existsSync(PAGE) ? fs.readFileSync(PAGE, 'utf8') : null;
  const stale = existing !== rendered;

  if (check) {
    if (stale) {
      console.log(
        'website/docs/support.mdx is not what the data renders to;'
          + ' run node scripts/check_support.js',
      );
    }
  } else if (stale) {
    fs.writeFileSync(PAGE, rendered);
    console.log(`wrote ${path.relative(ROOT, PAGE)}`);
  }

  const rows = Object.keys(data.statuses).length
    + data.areas.reduce((sum, area) => sum + area.rows.length, 0);
  console.log(
    `${rows} rows from React Native ${inventory.reactNative},`
      + ` ${problems.length} problem${problems.length === 1 ? '' : 's'}`,
  );
  return problems.length === 0 && !(check && stale) ? 0 : 1;
}

module.exports = {validate, audit, render, counts, STATUSES, KINDS};

if (require.main === module) {
  process.exitCode = main();
}
