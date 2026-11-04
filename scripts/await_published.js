#!/usr/bin/env node
'use strict';

// Did npm actually take the publish?
//
// `npm publish` exiting 0 does not mean a package is on the registry. The
// case worth catching is the one that never resolves: a trusted publisher
// granted only `npm stage publish` stages the tarball, prints the same
// `+ name@version`, exits 0, and nothing is ever published. basalt-win32
// carries a `0.0.0-stage` version from one.
//
// So publish.yml reads every version back afterwards. It did that with
// `npm view <spec> version --prefer-online` against a fifteen-minute
// deadline, and that failed three releases out of six -- 0.2.1, 0.2.2 and
// 0.2.5 -- each time reporting a release that had published all eight
// packages correctly as a release that had failed. The deadline grew from
// five minutes to fifteen along the way and the failures continued.
//
// **The deadline was never the problem.** `npm view` reads the packument
// through npm's CDN, and that copy is cached:
//
//     GET https://registry.npmjs.org/basalt-subprocess
//       cf-cache-status: HIT        cache-control: public, max-age=300
//
//     GET https://registry.npmjs.org/basalt-subprocess?write=true
//       cf-cache-status: DYNAMIC
//
// `?write=true` is the read npm's own client uses when it needs to see its
// own writes. It misses the cache and asks the origin, so it answers the
// question this check is actually asking -- did the registry accept the
// publish -- rather than the question it was accidentally asking, which was
// whether a cache somewhere had caught up yet. No deadline makes a cached
// answer authoritative, which is why raising it twice did not help.
//
// What a cache lag does mean is that somebody running `npm install` in the
// next few minutes may not see the version yet. That is worth printing and
// is not a failed release, so it is reported and never fails the job.

const ORIGIN = 'https://registry.npmjs.org';

/** `name@version`, where the name may itself be scoped. */
function splitSpec(spec) {
  const at = spec.lastIndexOf('@');
  if (at <= 0) {
    throw new Error(`not a name@version spec: ${spec}`);
  }
  return {name: spec.slice(0, at), version: spec.slice(at + 1)};
}

/**
 * Whether the registry holds `version` of `name`.
 *
 * `authoritative` picks which copy is asked: the origin, which sees a publish
 * immediately, or the CDN, which is what an `npm install` reaches.
 *
 * A packument that is missing, unparseable or served as an error is "not
 * there yet" rather than a failure. A brand new package 404s until its first
 * version lands, and a 5xx from the registry during a deploy is not evidence
 * that a publish was rejected.
 *
 * Answers `cached` as well, which is how this notices that the trick it
 * rests on has stopped working. `?write=true` is npm's own convention rather
 * than a documented guarantee, and if npm ever starts serving it from the
 * cache, a short deadline over a stale read is worse than the long deadline
 * this replaced. `cf-cache-status: HIT` is that happening, and the caller
 * falls back rather than failing a release on it.
 */
async function hasVersion(fetchJson, name, version, authoritative) {
  const url = authoritative
    ? `${ORIGIN}/${encodeURIComponent(name)}?write=true`
    : `${ORIGIN}/${encodeURIComponent(name)}`;
  const answer = await fetchJson(url);
  const document = answer && answer.document !== undefined ? answer.document : answer;
  return {
    present: Boolean(document && document.versions && document.versions[version]),
    cached: Boolean(answer && answer.cached),
  };
}

/**
 * Waits for every spec to be on the registry, against one deadline rather
 * than one deadline each: the wait is then as long as the slowest package
 * instead of the sum of all of them.
 *
 * Returns `{published, missing, lagging}`. `missing` is the only one that is
 * a failed release.
 */
async function awaitPublished(specs, options = {}) {
  const {
    fetchJson,
    now = () => Date.now(),
    sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
    log = console.log,
    // Two minutes, where the old check allowed fifteen. The origin answers a
    // publish within seconds, so a package still missing after this long is
    // not slow, it is staged or rejected, and saying so quickly beats saying
    // it quarter of an hour later.
    timeoutMs = 120_000,
    intervalMs = 5_000,
    // Only reached if an origin read comes back from the cache, which means
    // `?write=true` has stopped bypassing it. The old fifteen minutes, which
    // was wrong but was wrong slowly.
    cachedOriginTimeoutMs = 900_000,
    // What an `npm install` would get right now. Checked once each, after
    // the gate above has passed, and only ever printed.
    checkCdn = true,
  } = options;

  let deadline = now() + timeoutMs;
  const published = [];
  let pending = [...specs];
  let warnedAboutCache = false;

  while (pending.length > 0) {
    const still = [];
    for (const spec of pending) {
      const {name, version} = splitSpec(spec);
      const answer = await hasVersion(fetchJson, name, version, true);
      if (answer.cached && !warnedAboutCache) {
        warnedAboutCache = true;
        deadline = now() + cachedOriginTimeoutMs;
        log('-- the ?write=true read was served from the cache, so it is no');
        log('   longer authoritative. Waiting the old fifteen minutes rather');
        log('   than failing a release over a stale answer.');
      }
      if (answer.present) {
        log(`== ${spec} is on the registry`);
        published.push(spec);
      } else {
        still.push(spec);
      }
    }
    pending = still;
    if (pending.length === 0 || now() >= deadline) {
      break;
    }
    await sleep(intervalMs);
  }

  const lagging = [];
  if (checkCdn) {
    for (const spec of published) {
      const {name, version} = splitSpec(spec);
      if (!(await hasVersion(fetchJson, name, version, false)).present) {
        lagging.push(spec);
      }
    }
  }

  return {published, missing: pending, lagging};
}

function report(result, log = console.log) {
  if (result.lagging.length > 0) {
    log('');
    log(`-- published, and npm's cache has not caught up yet:${
      result.lagging.map((spec) => ` ${spec}`).join('')}`);
    log('   An `npm install` may not resolve it for a few minutes. The');
    log('   registry has it, which is what this job is asking about, so');
    log('   this is not a failure.');
  }
  if (result.missing.length > 0) {
    log('');
    log(`!! the registry does not hold these:${
      result.missing.map((spec) => ` ${spec}`).join('')}`);
    log('   npm said it published each of them, and this read goes to the');
    log('   origin rather than the cache, so it is not propagation. A staged');
    log('   publish looks exactly like this: check Allowed actions on the');
    log('   trusted publisher, which needs `npm publish` and not only');
    log('   `npm stage publish`.');
    return 1;
  }
  return 0;
}

async function main(argv) {
  const specs = argv.filter((argument) => argument.length > 0);
  if (specs.length === 0) {
    console.log('nothing was published, so there is nothing to read back');
    return 0;
  }
  const fetchJson = async (url) => {
    try {
      const response = await fetch(url, {
        cache: 'no-store',
        headers: {'cache-control': 'no-cache', accept: 'application/json'},
      });
      if (!response.ok) {
        return null;
      }
      return {
        document: await response.json(),
        cached: (response.headers.get('cf-cache-status') ?? '').toUpperCase() === 'HIT',
      };
    } catch {
      return null;
    }
  };
  return report(await awaitPublished(specs, {fetchJson}));
}

if (require.main === module) {
  main(process.argv.slice(2)).then(
    (code) => {
      process.exitCode = code;
    },
    (error) => {
      console.error(error.message);
      process.exitCode = 1;
    },
  );
}

module.exports = {awaitPublished, hasVersion, report, splitSpec};
