#!/usr/bin/env node
'use strict';

// What a release would upload, checked before there is a tag.
//
// `.github/workflows/release.yml` already asks two of these questions, and
// asks them at the worst moment: a tag is pushed, three platforms build for
// forty minutes, and the run fails because one package.json says 0.0.0-rc5
// where the tag says 0.0.0-rc6. The same arithmetic runs here in a second.
//
// The third question is the one that has no answer anywhere else, and is the
// reason this file exists rather than a note in the workflow: **a package in
// `packages/` is published unless it says otherwise.** Both pack loops walk
// `packages/*/` and the publish loop takes every tarball they produce, so
// adding a directory there is enough to put it on the registry on the next
// tag. A package that is not meant to go out yet has to say so, in
// `private: true`, and npm's own refusal to publish a private package is the
// backstop under this check rather than the only guard.
//
// Which packages are published is also a list in a second place --
// `publish.yml`'s trusted-publisher probe, which has to name each one because
// npm answers per package -- so the two lists are held against each other
// here. A package published with no registration fails its credential
// exchange half way through a release, after the ones before it have gone
// out, which is the worst shape a release failure can have.

const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const PACKAGES = path.join(ROOT, 'packages');
const PROBE = path.join(ROOT, '.github', 'workflows', 'publish.yml');

function manifests() {
  return fs
    .readdirSync(PACKAGES)
    .map((name) => path.join(PACKAGES, name, 'package.json'))
    .filter((file) => fs.existsSync(file))
    .map((file) => ({file, manifest: JSON.parse(fs.readFileSync(file, 'utf8'))}));
}

// The package names publish.yml asks npm about. Read out of the workflow
// rather than copied here, so this cannot drift from the thing it checks: the
// loop is `for p in <names>; do`, continued over as many lines as it takes.
function probedPackages(text) {
  const loop = text.match(/for p in ((?:[^;]|\n)*?); do/);
  if (loop === null) {
    return null;
  }
  return loop[1]
    .replace(/\\\s*\n/g, ' ')
    .split(/\s+/)
    .filter((name) => name.length > 0);
}

function check() {
  const problems = [];
  const found = manifests();
  const versions = new Set(found.map(({manifest}) => manifest.version));

  // One version across the repository, which is what the tag is compared
  // against. Every package here moves together by design: a host package is
  // useless against a different core and nothing is versioned on its own.
  if (versions.size > 1) {
    const spread = found
      .map(({manifest}) => `${manifest.name} ${manifest.version}`)
      .sort()
      .join(', ');
    problems.push(`the packages are at different versions: ${spread}`);
  }

  for (const {manifest} of found) {
    // A host package pins the shared one exactly. A gtk at 0.2.0 asking for
    // core 0.1.0 installs, and runs against the wrong core.
    const core = manifest.dependencies?.['basalt-core'];
    if (core !== undefined && core !== manifest.version) {
      problems.push(
        `${manifest.name} is ${manifest.version} and depends on basalt-core ${core}`,
      );
    }
  }

  const workflow = fs.readFileSync(PROBE, 'utf8');
  const probed = probedPackages(workflow);
  if (probed === null) {
    problems.push(
      'publish.yml has no `for p in ...; do` loop, so which packages it checks cannot be read',
    );
    return problems;
  }

  for (const {manifest} of found) {
    const isPrivate = manifest.private === true;
    const isProbed = probed.includes(manifest.name);
    if (!isPrivate && !isProbed) {
      problems.push(
        `${manifest.name} would be published and publish.yml does not check it has a trusted `
          + 'publisher: add it to the probe, or mark the package private until it is meant to go out',
      );
    }
    if (isPrivate && isProbed) {
      problems.push(
        `${manifest.name} is private and publish.yml checks it anyway, which asks npm about a `
          + 'package nothing uploads',
      );
    }
  }

  for (const name of probed) {
    if (!found.some(({manifest}) => manifest.name === name)) {
      problems.push(`publish.yml checks ${name}, which is not a package in this repository`);
    }
  }

  return problems;
}

module.exports = {check, probedPackages};

if (require.main === module) {
  const problems = check();
  for (const problem of problems) {
    console.error(problem);
  }
  const published = manifests().filter(({manifest}) => manifest.private !== true);
  console.log(
    `${manifests().length} packages, ${published.length} published, `
      + `${problems.length} problem${problems.length === 1 ? '' : 's'}`,
  );
  process.exit(problems.length === 0 ? 0 : 1);
}
