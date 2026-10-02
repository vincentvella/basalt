# CI performance, as measured

A record rather than instructions: what the CI jobs cost, what was wrong with
them, and what now stops each fault coming back silently. Split out of
`docs/TESTING.md`, which is for running the tests and had become half this.

Kept rather than deleted because every number here was measured, several were
surprising, and three of the checks in the repository exist only as the
conclusion of a paragraph below. Removing it would leave those checks looking
arbitrary.

Not on the documentation site, for the same reason `BACKLOG.md` is not: it is a
note to whoever picks the work up next, not documentation.

## Sharding, and what it cost to find out

Measured before it was built, because sharding the wrong thing is free to do
and worthless: of the Linux job's 12.9 minutes, **9.3 were the end-to-end
suite** and 0.2 were the build, ccache having made the compile nearly free.
The AppKit job is 26.8 minutes, of which 9.6 is the suite and another 8.8 is
the cross-host comparison. So both of those shard, and the build does not need
to.

Measuring it again afterwards is what found the next thing. Three macOS shards
came in at 13.4, 14.9 and 14.5 minutes against an unsharded 27.7: a clean
1.9x, but a long way off the others, and the step breakdown said why: 5.9
minutes of it was `Configure and build`, against 14 seconds for the same build
on Linux. That job installed ccache, restored a ccache and saved a ccache, and
never passed `CMAKE_CXX_COMPILER_LAUNCHER`. **A cache that is never written
restores in 0 seconds and saves in 0 seconds, and both steps report success**,
so the only visible symptom was a slow build, which looks like a Mac being a
Mac. It also cached `~/Library/Caches/ccache` while ccache's directory is not
reliably that; the job names `CCACHE_DIR` now, so the two cannot disagree.

The cache listing settled it beyond argument: Linux and Windows had a ccache
entry for every commit going back weeks, and macOS had **none at all**, ever.
With the launcher flags in place the build went from 350 seconds to **29**, at a
555/555 hit rate, and the job from 14.3 minutes to 9.8, so the AppKit job is
27.7 minutes unsharded and 9.8 sharded, which is most of what this whole
exercise was worth and none of it was the sharding.

**Which changes the arithmetic for adding shards, in favour of it.** The
non-shardable part of that job was 434 seconds and is now 128, because 355 of it
was the broken build. So `128s + 1380s/N`:

| shards | AppKit job |
| ------ | ---------- |
| 3      | 9.8m *(measured)* |
| 4      | 7.9m |
| 5      | 6.7m |
| many   | 2.1m floor |

Five is where it stops, and not for a reason about diminishing returns: a Free
or Pro account gets **five concurrent macOS jobs**, so six shards run in two
waves and finish slower than three. Five saturates the limit and leaves nothing
for `release.yml`, which also wants a Mac. Four is the one with headroom.

**To run the edit half on Windows**, from a Windows machine, with a React
Native checkout in place:

```
python scripts\integration_test.py --platform windows -k "Fast Refresh"
```

Leave `BASALT_SKIP_FAST_REFRESH` unset; that is the whole point.

This did not work before `BASALT_TEST_QUIT_FILE` existed, for a reason that had
nothing to do with Fast Refresh. The scenario's last assertion reads the dumped
widget tree; every host writes that on the way out; and the harness ended the
host with `terminate()`, which on Windows is `TerminateProcess` and reaches
neither `WM_CLOSE` nor the dump hanging off it. Measured on a Mac, where both
signals exist: SIGTERM exits 0 and writes 8293 bytes, SIGKILL exits -9 and
writes nothing. So it failed with "host wrote no widget tree" on a host that had
worked: a harness artefact dressed as a platform bug, which is the worst thing
to hand somebody about to go and test that platform by hand.

That is what `BASALT_TEST_QUIT_FILE` is for, and it costs about a quarter of a
second rather than ninety.

`scripts/test_harness.py` checks the harness's own waits, and exists because of
a bug in one. The Fast Refresh scenario waited for Metro to serve the running
app a bundle by counting `BUNDLE` lines in Metro's log, but `prewarm` is
itself a request and Metro logs two for it, so the count was already satisfied
before the host started. **The wait returned in about a hundredth of a second,
on every platform, and could not fail.** It had been that way since it was
written; what hid it is that the edit assertion after it carried the scenario on
the machines where Metro's file watching works. `wait_for_log` takes a byte
offset now, and the check is on what happened *after* the host existed.

It was found by making the scenario run on Windows, where the edit is skipped
and so nothing was left to carry it. The same run found the second thing
`test_harness.py` guards: `terminate()` is a SIGTERM that GTK and AppKit handle
and exit 0 from, and on Windows it is `TerminateProcess`, which sets the exit
code to 1 unconditionally. Asserting on that code reported our own kill as the
app crashing. `stop_host` says which of the two happened, so the exit code is
checked where it means something and the log is scanned for JS errors either
way.

`scripts/check_ccache.py` now runs after every build, on all three jobs, and
fails when ccache saw **no cacheable compiles at all**, which is what a
missing launcher flag looks like from the inside. It deliberately does not
assert a hit *rate*: a cold cache legitimately misses everything, and a check
that cries wolf gets deleted. The distinction it draws is "ccache was not in
the compile", which is always a workflow bug, against "ccache ran and missed",
which is a fact about the cache. Tested against five stubbed ccache states,
including a renamed field, which it reports as a version difference rather than
as zero; reading it as zero would point the next person at the wrong fix.

Which is the answer to "build once and hand the binary to every shard": ccache
already is that, and it is cheaper, because it is content-addressed and skips
exactly the work that is unchanged. An artifact would need its own build job
per operating system: three more jobs, each repeating the checkout, the
dependency install and the React Native fetch, and then an upload and a
download of a build tree with debug info in it, to save the 24 seconds that
`Configure` and `Build` actually cost on a warm cache. Every other fixed cost a
shard pays, it pays regardless: it installs GTK and Pango to *run* the hosts,
fetches React Native to bundle the demo, and bundles the demo because the suite
runs against it.

Striding rather than slicing: `scenarios[i - 1 :: n]`. The list is in the order
things were written, so neighbours cost about the same: the scroll scenarios
sit together, and so do the two that wait twelve seconds for a window. A
contiguous slice hands one shard all of them, and a shard set is only as fast
as its slowest member. Striding gives 58, 83 and 73 seconds of explicit waiting
across the three.

What each shard pays again is the job's fixed cost, installing dependencies,
bootstrapping, building. Three rather than more for that reason: past three the
repeated overhead grows faster than the saving. The steps that would give the
same answer three times, the CLI suite, the platform JavaScript tests,
include hygiene, run only on shard 1.

`scripts/test_shards.py` checks that every scenario lands in exactly one shard,
for every shard count, and that the parser refuses `0/4`, `5/4` and `2/4/8`. It
is there because the bug it guards against points towards green: a boundary
that dropped a scenario would make the suite pass while running less of it, and
nothing in the output would look wrong.
