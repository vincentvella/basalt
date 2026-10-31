#!/usr/bin/env python3
"""Type-check this repository against a React Native it is not built with.

`supported-versions.json` lists the React Natives this platform claims. Every
build here uses one of them -- CI pins 0.88, and so does every local build --
so a claim about any other version is checked by nothing until a release, when
the Expo job builds the host inside a real app against whatever Expo installs.
That job is forty minutes into a gauntlet and it is the only thing standing
between an unbuildable version and a published package.

This asks the same question in about a minute, and it needs no build at all.
CMake records the exact compile command for every translation unit; this takes
each one, points its include paths at another checkout, sets the version macro
to match, and asks the compiler for syntax only. What that catches is the whole
class of version breakage this project has actually hit: a member that a newer
enum has and an older one does not, a field that moved, a type that stopped
being copyable.

    scripts/check_versions.py ../react-native-0.86/packages/react-native 86

What it does not catch is anything that links, runs, or renders. A version that
passes here is a version that compiles; `supported-versions.json` still means
"built and both suites run", and this is the cheap half of that.

Needs a configured build directory with CMAKE_EXPORT_COMPILE_COMMANDS on,
which `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` leaves behind.
"""

import concurrent.futures
import json
import os
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent


def compile_commands(build: pathlib.Path) -> list:
    database = build / "compile_commands.json"
    if not database.exists():
        print(f"no {database}: configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON", file=sys.stderr)
        raise SystemExit(2)
    entries = json.loads(database.read_text())
    # This repository's own sources. React Native's and Hermes's are theirs to
    # compile and are already built; what is under test is our use of them.
    return [e for e in entries if e["file"].startswith(f"{REPO}/packages/")]


def pinned_react_native(entries: list) -> str:
    """The checkout the build directory was configured against, read back out
    of a compile command rather than asked for a second time."""
    for entry in entries:
        found = re.search(r"-isystem\s+(\S+/packages/react-native)/ReactCommon\b", entry["command"])
        if found:
            return found.group(1)
    print("could not find the React Native this build was configured with", file=sys.stderr)
    raise SystemExit(2)


def check(entry: dict, pinned: str, other: str, minor: str) -> tuple:
    command = entry["command"].replace(pinned, other)
    command = re.sub(r"BASALT_RN_MINOR=\d+", f"BASALT_RN_MINOR={minor}", command)
    # No object file, no linking: the question is whether it compiles.
    command = re.sub(r"\s-o\s+\S+", " ", command)
    command = re.sub(r"\s-c\s", " -fsyntax-only ", command)
    result = subprocess.run(
        command, shell=True, cwd=entry["directory"], capture_output=True, text=True
    )
    return entry["file"], result.returncode, result.stderr


def main(argv: list) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    other = str(pathlib.Path(argv[1]).resolve())
    minor = argv[2]
    build = pathlib.Path(argv[3]) if len(argv) > 3 else REPO / "build"

    if not (pathlib.Path(other) / "ReactCommon").is_dir():
        print(f"{other} is not a React Native package directory", file=sys.stderr)
        return 2

    entries = compile_commands(build)
    pinned = pinned_react_native(entries)
    if pinned == other:
        print(f"{other} is what this build already uses; nothing to compare")
        return 2

    failures = []
    workers = min(8, (os.cpu_count() or 4))
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for file, code, stderr in pool.map(
            lambda entry: check(entry, pinned, other, minor), entries
        ):
            if code != 0:
                failures.append((file, stderr))

    for file, stderr in failures:
        print(f"== {file.replace(str(REPO) + '/', '')}")
        for line in [l for l in stderr.splitlines() if " error: " in l][:3]:
            print(f"   {line.strip()}")

    print(
        f"{len(entries)} translation units, {len(failures)} failed "
        f"against React Native {minor}"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
