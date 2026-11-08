#!/usr/bin/env node
'use strict';

// Every publishable package's trusted publisher, in one command.
//
// npm will only accept a publish from this repository's workflow if the
// package has a trusted publisher naming it, and that is per package rather
// than per account. Eight of those were set up by hand through npmjs.com, one
// form at a time, and the ninth is where that stopped being reasonable.
//
// It was never necessary. `npm trust github <package> --file publish.yml
// --repo <owner/repo>` does the same thing from a terminal, so this walks the
// packages the same way check_packages.js does and registers whichever are
// missing one. Adding a package is now: write it, run this, publish.
//
//   node scripts/trust_packages.js              # say what is missing
//   node scripts/trust_packages.js --write      # and register it
//
// **It needs an npm credential, and deliberately does not look for one.** Run
// `npm login` first. A trusted publisher is the thing that lets a workflow
// publish without a long-lived token, so creating one cannot itself be done
// by that workflow: the OIDC exchange the release uses is granted *because*
// the trust exists. Chicken, egg, and a human in between, once per package.
//
// Private packages are skipped, because npm has nothing to trust until a
// package is meant to go out. `basalt-async-storage` is private as this is
// written; marking it public and running this is the whole of shipping it.

const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');

const ROOT = path.join(__dirname, '..');
const PACKAGES = path.join(ROOT, 'packages');

// The workflow npm matches against. Not release.yml: release hands off with
// `gh workflow run publish.yml`, so the token npm sees is minted by
// publish.yml whichever path started it. publish.yml's own comment explains
// that at length, and getting it wrong is a 404 at the exchange.
const WORKFLOW = 'publish.yml';
const REPOSITORY = 'vincentvella/basalt';

function publishablePackages() {
  return fs
    .readdirSync(PACKAGES)
    .map((name) => path.join(PACKAGES, name, 'package.json'))
    .filter((file) => fs.existsSync(file))
    .map((file) => JSON.parse(fs.readFileSync(file, 'utf8')))
    .filter((manifest) => manifest.private !== true)
    .map((manifest) => manifest.name)
    .sort();
}

/**
 * Whether npm already holds a trusted publisher for `name`.
 *
 * Answers `null` rather than false when the question could not be asked: not
 * logged in, offline, or a package that does not exist yet. The difference
 * matters, because "no trust" is something to fix and "could not ask" is
 * something to report.
 */
function trusted(name) {
  const result = spawnSync('npm', ['trust', 'list', name, '--json'], {
    encoding: 'utf8',
  });
  if (result.status !== 0) {
    return null;
  }
  try {
    const parsed = JSON.parse(result.stdout);
    const entries = Array.isArray(parsed) ? parsed : (parsed?.trustedPublishers ?? []);
    return entries.length > 0;
  } catch {
    return null;
  }
}

function register(name) {
  console.log(`==> npm trust github ${name} --file ${WORKFLOW} --repo ${REPOSITORY}`);
  const result = spawnSync(
    'npm',
    ['trust', 'github', name, '--file', WORKFLOW, '--repo', REPOSITORY, '--yes'],
    {stdio: 'inherit'},
  );
  return result.status === 0;
}

function main(argv) {
  const write = argv.includes('--write');
  const names = publishablePackages();
  console.log(`${names.length} publishable packages, workflow ${WORKFLOW}, repo ${REPOSITORY}\n`);

  const missing = [];
  const unknown = [];
  for (const name of names) {
    const has = trusted(name);
    if (has === null) {
      unknown.push(name);
      console.log(`  ?  ${name.padEnd(24)} could not be asked`);
    } else if (has) {
      console.log(`  =  ${name.padEnd(24)} trusted`);
    } else {
      missing.push(name);
      console.log(`  !  ${name.padEnd(24)} no trusted publisher`);
    }
  }

  if (unknown.length === names.length) {
    console.log(
      '\nnpm answered nothing for any of them, which is almost always "not logged in".\n' +
        'Run `npm login` and try again; this needs a credential of yours and looks for none.',
    );
    return 1;
  }

  if (missing.length === 0) {
    console.log('\nnothing to do');
    return 0;
  }

  if (!write) {
    console.log(`\n${missing.length} to register. Run again with --write.`);
    return 1;
  }

  let failed = 0;
  for (const name of missing) {
    if (!register(name)) {
      failed++;
    }
  }
  if (failed > 0) {
    console.log(`\n${failed} could not be registered`);
    return 1;
  }
  console.log('\nregistered. `node scripts/check_packages.js` holds the probe list to this.');
  return 0;
}

if (require.main === module) {
  process.exitCode = main(process.argv.slice(2));
}

module.exports = {publishablePackages, WORKFLOW, REPOSITORY};
