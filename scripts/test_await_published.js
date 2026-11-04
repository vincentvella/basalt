// Reading a publish back, and the difference between a cache and a refusal.
//
// Three of the first six releases failed this check while publishing every
// package correctly. Each of those is a test here, because the cost of
// getting it wrong is not a red tick: it is a maintainer looking at a
// release that worked and having to decide whether it did.

'use strict';

const assert = require('node:assert');
const {test} = require('node:test');

const {awaitPublished, report, splitSpec} = require('./await_published.js');

/**
 * A registry that answers from two sets: what the origin holds, and what the
 * cache holds. `appearAfter` is how many origin reads of a spec happen before
 * the origin admits to it, which is a publish still being written.
 */
function registry({origin = [], cache = null, appearAfter = {}} = {}) {
  const seen = {origin: 0, cache: 0};
  const reads = {};
  const cached = cache ?? origin;

  const fetchJson = async (url) => {
    const authoritative = url.includes('?write=true');
    const name = decodeURIComponent(
      url.replace(`https://registry.npmjs.org/`, '').replace('?write=true', ''),
    );
    seen[authoritative ? 'origin' : 'cache'] += 1;
    reads[name] = (reads[name] ?? 0) + 1;

    const held = authoritative ? origin : cached;
    const versions = {};
    for (const spec of held) {
      const {name: heldName, version} = splitSpec(spec);
      if (heldName !== name) continue;
      const wait = appearAfter[spec] ?? 0;
      if (authoritative && reads[name] <= wait) continue;
      versions[version] = {name: heldName, version};
    }
    if (Object.keys(versions).length === 0) {
      // npm 404s a package it has never heard of, which is what a first
      // publish looks like right up until it lands.
      return null;
    }
    return {name, versions};
  };

  return {fetchJson, seen};
}

/** No real waiting, and a clock that only moves when something sleeps. */
function clock(step = 5_000) {
  let t = 0;
  return {
    now: () => t,
    sleep: async () => {
      t += step;
    },
  };
}

test('every package on the origin passes without waiting', async () => {
  const {fetchJson, seen} = registry({origin: ['basalt-core@1.0.0', 'basalt-gtk@1.0.0']});
  const result = await awaitPublished(['basalt-core@1.0.0', 'basalt-gtk@1.0.0'], {
    fetchJson,
    ...clock(),
    log: () => {},
  });

  assert.deepEqual(result.missing, []);
  assert.deepEqual(result.lagging, []);
  assert.equal(result.published.length, 2);
  // Two origin reads and two cache reads: one of each per package, no polling.
  assert.equal(seen.origin, 2);
});

test('a package the cache has not caught up with is not a failure', async () => {
  // 0.2.1, 0.2.2 and 0.2.5, all three. The registry had every package; the
  // CDN had not caught up with one of them, and the job failed the release.
  const {fetchJson} = registry({
    origin: ['basalt-core@0.2.5', 'basalt-subprocess@0.2.5'],
    cache: ['basalt-core@0.2.5'],
  });
  const lines = [];
  const result = await awaitPublished(['basalt-core@0.2.5', 'basalt-subprocess@0.2.5'], {
    fetchJson,
    ...clock(),
    log: () => {},
  });

  assert.deepEqual(result.missing, []);
  assert.deepEqual(result.lagging, ['basalt-subprocess@0.2.5']);
  assert.equal(report(result, (line) => lines.push(line)), 0);
  assert.match(lines.join('\n'), /not a failure/);
});

test('a staged publish is a failure, and says which one', async () => {
  // The case the read-back exists for: npm accepted the request, staged the
  // tarball, printed `+ name@version`, and published nothing.
  const {fetchJson} = registry({origin: ['basalt-core@0.2.5']});
  const lines = [];
  const result = await awaitPublished(['basalt-core@0.2.5', 'basalt-win32@0.2.5'], {
    fetchJson,
    ...clock(),
    log: () => {},
    timeoutMs: 30_000,
  });

  assert.deepEqual(result.missing, ['basalt-win32@0.2.5']);
  assert.equal(report(result, (line) => lines.push(line)), 1);
  const said = lines.join('\n');
  assert.match(said, /basalt-win32@0\.2\.5/);
  assert.match(said, /npm stage publish/);
  // And it does not blame propagation, which is what sent three releases'
  // worth of attention in the wrong direction.
  assert.doesNotMatch(said, /propagation(?!\.)/);
});

test('a publish still being written is waited for', async () => {
  const {fetchJson} = registry({
    origin: ['basalt-core@0.2.5'],
    appearAfter: {'basalt-core@0.2.5': 2},
  });
  const result = await awaitPublished(['basalt-core@0.2.5'], {
    fetchJson,
    ...clock(),
    log: () => {},
  });

  assert.deepEqual(result.missing, []);
  assert.deepEqual(result.published, ['basalt-core@0.2.5']);
});

test('the wait is one deadline for the whole set, not one each', async () => {
  // Eight packages against a two-minute budget, none of which ever appears.
  // One deadline each would be sixteen minutes inside a job release.yml
  // allows less than that.
  const specs = Array.from({length: 8}, (_, i) => `basalt-${i}@0.2.5`);
  const {fetchJson} = registry({origin: []});
  const time = clock();
  const result = await awaitPublished(specs, {
    fetchJson,
    ...time,
    log: () => {},
    timeoutMs: 120_000,
  });

  assert.equal(result.missing.length, 8);
  assert.ok(
    time.now() <= 120_000,
    `gave up after ${time.now()}ms, which is past the one shared deadline`,
  );
});

test('the origin is asked with ?write=true and the cache without it', async () => {
  // The whole fix in one assertion. `npm view` reads the cached packument,
  // which is served with cf-cache-status: HIT and max-age=300, so it can be
  // minutes behind a publish that has already happened. The origin read
  // misses that cache.
  const urls = [];
  const fetchJson = async (url) => {
    urls.push(url);
    return {name: 'basalt-core', versions: {'0.2.5': {}}};
  };
  await awaitPublished(['basalt-core@0.2.5'], {fetchJson, ...clock(), log: () => {}});

  assert.deepEqual(urls, [
    'https://registry.npmjs.org/basalt-core?write=true',
    'https://registry.npmjs.org/basalt-core',
  ]);
});

test('a scoped name keeps its slash and its version is still read off the end', () => {
  assert.deepEqual(splitSpec('@scope/thing@1.2.3'), {
    name: '@scope/thing',
    version: '1.2.3',
  });
});

test('a registry error is not evidence that a publish was refused', async () => {
  // A 5xx while npm is deploying, or a dropped connection: fetchJson answers
  // null for all of those, and the loop treats it as "not yet" rather than
  // failing a release over one bad response.
  let calls = 0;
  const fetchJson = async (url) => {
    calls += 1;
    if (calls <= 2) return null;
    return {name: 'basalt-core', versions: {'0.2.5': {}}};
  };
  const result = await awaitPublished(['basalt-core@0.2.5'], {
    fetchJson,
    ...clock(),
    log: () => {},
  });

  assert.deepEqual(result.missing, []);
});

test('a cached ?write=true read falls back to the long wait, not a fast failure', async () => {
  // The way this fix could rot: npm starts serving the origin read from the
  // cache, and a two-minute deadline over a stale answer fails releases
  // faster than the fifteen-minute one did. The cache header is the tell.
  const lines = [];
  let reads = 0;
  const fetchJson = async (url) => {
    if (!url.includes('?write=true')) {
      return {document: {name: 'basalt-core', versions: {}}, cached: true};
    }
    reads += 1;
    // Arrives on the fortieth origin read, which at five seconds a poll is
    // past the two-minute deadline and well inside the fallback one.
    return {
      document: {name: 'basalt-core', versions: reads >= 40 ? {'0.2.5': {}} : {}},
      cached: true,
    };
  };
  const time = clock();
  const result = await awaitPublished(['basalt-core@0.2.5'], {
    fetchJson,
    ...time,
    log: (line) => lines.push(line),
  });

  assert.deepEqual(result.missing, []);
  assert.match(lines.join('\n'), /served from the cache/);
  assert.ok(time.now() > 120_000, 'gave up at the short deadline despite a cached read');
});
