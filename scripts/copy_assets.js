#!/usr/bin/env node
// Copies the images a bundle `require()`s to where it will look for them.
//
// Metro's `build` command has no `--assets-dest`. React Native's own CLI does,
// and reaching for it would mean bundling through a second tool with its own
// idea of which platforms exist -- the thing packages/basalt-core's
// Metro config is there to avoid. So the asset list is read back out of the
// bundle that was just written, which is where Metro puts it anyway.
//
// Every `require('./thing.png')` becomes a descriptor in the bundle:
//
//   {__packager_asset:!0,httpServerLocation:"/assets/../../react-native/...",
//    scales:[1,2,3],name:"close",type:"png",...}
//
// `httpServerLocation` is the asset's directory relative to Metro's project
// root, under a `/assets` prefix -- so the source is that path resolved against
// the project root, and the destination is the same path with each `..` turned
// into `__`, which is the escaping `resolveAssetSource` applies at runtime.
// Getting both from one string is what keeps this from having to know where
// React Native is checked out.
//
// This is what LogBox's icons needed, and it is not only LogBox's: any app with
// a `require()`d image had the same silent hole, which showed up as an <Image>
// that laid out at the right size and drew nothing.

'use strict';

const fs = require('fs');
const path = require('path');

function usage() {
  console.error('usage: copy_assets.js <bundle.js> <project-root> <dest-dir>');
  process.exit(2);
}

const [bundlePath, projectRoot, destRoot] = process.argv.slice(2);
if (!bundlePath || !projectRoot || !destRoot) {
  usage();
}

const source = fs.readFileSync(bundlePath, 'utf8');

// The descriptors are object literals in the bundle's own JavaScript, so they
// are read with a regular expression rather than parsed: a production bundle is
// minified and a development one is not, and neither is JSON.
const descriptors = new Map();
// `!0` rather than `true` in a minified bundle, and `true` in a development
// one, so both spellings are accepted.
const pattern = /__packager_asset\s*:\s*(?:!\d|true)\s*,([^}]*)}/g;
let match;
while ((match = pattern.exec(source)) !== null) {
  const body = match[1];
  const read = key => {
    const found = body.match(new RegExp(`["']?${key}["']?\\s*:\\s*["']([^"']*)["']`));
    return found ? found[1] : null;
  };
  const location = read('httpServerLocation');
  const name = read('name');
  const type = read('type');
  if (location == null || name == null || type == null) {
    continue;
  }
  const scalesMatch = body.match(/["']?scales["']?\s*:\s*\[([^\]]*)\]/);
  const scales = scalesMatch
    ? scalesMatch[1].split(',').map(part => Number(part.trim())).filter(Number.isFinite)
    : [1];
  descriptors.set(`${location}/${name}.${type}`, {location, name, type, scales});
}

// `/assets/../../react-native/x` -> `../../react-native/x`, which resolved
// against the project root is where the file is.
const relativeFrom = location => location.replace(/^\/assets\/?/, '');

// And `../../react-native/x` -> `__react-native/x`, which is where the runtime
// will look. Each `../` becomes a single `_`, not each `..` -- that is
// AssetSourceResolver.scaledAssetURLNearBundle's rule, and it exists so an
// asset from outside the project root cannot land outside the assets
// directory. Two consecutive `../` are why the path reads `__react-native`.
const escaped = relative => relative.replace(/\.\.\//g, '_');

let copied = 0;
let missing = 0;
for (const {location, name, type, scales} of descriptors.values()) {
  const relative = relativeFrom(location);
  const from = path.resolve(projectRoot, relative);
  const to = path.join(destRoot, 'assets', escaped(relative));

  for (const scale of scales.length > 0 ? scales : [1]) {
    // Metro names the 1x file plainly and the rest with a suffix, which is the
    // convention every React Native platform's packager uses.
    const suffix = scale === 1 ? '' : `@${scale}x`;
    const file = `${name}${suffix}.${type}`;
    const sourceFile = path.join(from, file);
    if (!fs.existsSync(sourceFile)) {
      // A scale an asset does not have is not a problem: `scales` lists what
      // the runtime may ask for, and the runtime falls back.
      if (scale === 1) {
        missing += 1;
        console.error(`    missing ${sourceFile}`);
      }
      continue;
    }
    fs.mkdirSync(to, {recursive: true});
    fs.copyFileSync(sourceFile, path.join(to, file));
    copied += 1;
  }
}

console.log(
  `==> copied ${copied} asset${copied === 1 ? '' : 's'}` +
    (missing > 0 ? `, ${missing} not found` : ''),
);
