#!/usr/bin/env python3
"""Which of React Native's native modules this platform answers, and which it does not.

The support page answers that question for *props*, one row per attribute, and
the answer turned out to be worth having: three columns of ticks drift from the
code, and the per-row check caught claims nothing implemented. Native modules
had no such page, and they are the other half of what an app touches --
`Clipboard.setString`, `Linking.openURL`, `AppState.currentState`,
`Appearance.getColorScheme`, every `NativeEventEmitter`.

The difference from a prop is what happens when nobody answers. React Native
asks for a module in one of two ways:

    TurboModuleRegistry.get('Timing')              -> null, and the JS copes
    TurboModuleRegistry.getEnforcing('AppState')   -> throws

So an unanswered `get` is a silent no-op and an unanswered `getEnforcing` is an
app that dies on the line that reached it. Both are worth knowing before an app
does.

## What this reads, and why none of it is a list kept by hand

Three sources, all of them the code rather than a note about the code:

  - **What React Native asks for**: every `TurboModuleRegistry.get` and
    `.getEnforcing` call in its own JavaScript, with the name and which of the
    two it is. A module nobody asks for cannot be reached by an app that only
    imports `react-native`.
  - **What this repository answers**: each host's provider chain, which is a run
    of `name == SomeModule::kModuleName` comparisons in `main_*.cpp`. The module
    *name* is the string on the class or on the generated spec it derives from,
    which is why this follows base classes rather than matching on names.
  - **What React Native itself answers**: the same shape, in two files.
    `ReactCxxPlatform/react/runtime/ReactCxxTurboModuleProvider.cpp` is the C++
    platform's own -- AppState, DeviceInfo, Networking -- and it falls through to
    `ReactCommon/react/nativemodule/defaults/DefaultTurboModules.cpp`, which
    carries the web APIs: microtasks, idle callbacks, the DOM, performance,
    Animated. This platform builds on both, so anything either provides is
    provided here.

`docs/platform-modules.json` is the triage: for each name, who answers it and,
when nobody does, what an app sees. `--check` compares the three sources against
that file and fails on any difference -- a module added upstream, a provider
chain that stopped offering one, a status that claims an answer the code does
not give.

Run with:  python3 scripts/audit_modules.py           # print the table
           python3 scripts/audit_modules.py --check   # fail if it is stale
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TRIAGE = REPO / "docs/platform-modules.json"

# The hosts' provider chains. One per host, and they are expected to agree about
# every module that is not the host's own -- which is itself something this
# checks, a module offered on one desktop and not another being exactly the kind
# of divergence nobody would notice.
HOST_PROVIDERS = {
    "linux": "packages/basalt-gtk/native/gtk/main_gtk.cpp",
    "macos": "packages/basalt-appkit/native/appkit/main_appkit.mm",
    "windows": "packages/basalt-win32/native/win32/main_win32.cpp",
}

# `class JSI_EXPORT NativeFooCxxSpec : public TurboModule {`: the macro between
# `class` and the name is what a naive pattern reads as the class name.
CLASS = re.compile(r"class\s+(?:[A-Z][A-Z0-9_]*\s+)?([A-Za-z0-9_]+)\b")
MODULE_NAME = re.compile(r'kModuleName\s*=\s*"([^"]+)"')
BASE = re.compile(
    r"class\s+(?:[A-Z][A-Z0-9_]*\s+)?([A-Za-z0-9_]+)\s*(?:final\s*)?:"
    r"\s*public\s+(?:facebook::react::)?([A-Za-z0-9_]+)"
)
ASKED = re.compile(
    r"TurboModuleRegistry\.(get|getEnforcing)(?:<[^>]*>)?\(\s*'([A-Za-z0-9_]+)'"
)
OURS = re.compile(r"name == basalt::([A-Za-z0-9_]+)::kModuleName")
THEIRS = re.compile(r"name == ([A-Za-z0-9_:]+)::kModuleName")


def react_native_dir() -> Path:
    for candidate in [
        os.environ.get("RN_DIR"),
        REPO.parent / "react-native",
        REPO / "react-native-src",
    ]:
        if candidate is None:
            continue
        path = Path(candidate)
        if (path / "packages/react-native/ReactCommon").is_dir():
            return path.resolve()
    raise SystemExit(
        "no React Native checkout found: set RN_DIR, or put one beside this repository"
    )


def module_names(paths: list[Path]) -> dict[str, str]:
    """Class name -> the module name it declares, for every class that declares one.

    The constant sits inside the class, a few lines down, which is why this walks
    lines rather than trying to match a class body: the generated specs are one
    enormous file and a body regex over it is both slow and wrong.
    """
    found: dict[str, str] = {}
    for path in paths:
        try:
            lines = path.read_text(errors="ignore").splitlines()
        except OSError:
            continue
        for index, line in enumerate(lines):
            match = CLASS.search(line)
            if match is None:
                continue
            for ahead in lines[index : index + 40]:
                name = MODULE_NAME.search(ahead)
                if name is not None:
                    found.setdefault(match.group(1), name.group(1))
                    break
                if ahead is not line and CLASS.search(ahead):
                    break
    return found


def base_classes(paths: list[Path]) -> dict[str, str]:
    """Class name -> the class it derives from, which is where a spec's name lives."""
    found: dict[str, str] = {}
    for path in paths:
        try:
            text = path.read_text(errors="ignore")
        except OSError:
            continue
        for match in BASE.finditer(text):
            found.setdefault(match.group(1), match.group(2))
    return found


def resolve(cls: str, names: dict[str, str], bases: dict[str, str]) -> str | None:
    """The module name a provider class answers to, following its base specs.

    A module class rarely declares the name itself: it derives from a generated
    `NativeFooCxxSpec`, and the constant is there. Bounded by `seen` because a
    malformed pair of declarations would otherwise loop.
    """
    seen: set[str] = set()
    while cls is not None and cls not in seen:
        seen.add(cls)
        if cls in names:
            return names[cls]
        cls = bases.get(cls)
    return None


def headers(root: Path) -> list[Path]:
    return [path for path in root.rglob("*.h")]


def audit(rn_dir: Path) -> dict:
    react_native = rn_dir / "packages/react-native"

    names = module_names(
        list(REPO.glob("packages/*/native/*/*.h"))
        + headers(REPO / "third_party/codegen")
        + headers(react_native / "ReactCommon")
        + headers(react_native / "ReactCxxPlatform")
    )
    bases = base_classes(
        list(REPO.glob("packages/*/native/*/*.h"))
        + headers(react_native / "ReactCommon")
        + headers(react_native / "ReactCxxPlatform")
    )

    asked: dict[str, dict] = {}
    for root in (react_native / "Libraries", react_native / "src"):
        for path in sorted(root.rglob("*.js")):
            try:
                text = path.read_text(errors="ignore")
            except OSError:
                continue
            for match in ASKED.finditer(text):
                how, name = match.group(1), match.group(2)
                previous = asked.get(name)
                # `getEnforcing` is the stronger claim about the same name: it
                # throws rather than answering null, so a name asked for both
                # ways is recorded as the one that can kill an app.
                if previous is None or (
                    previous["how"] == "get" and how == "getEnforcing"
                ):
                    asked[name] = {
                        "how": how,
                        "where": str(path.relative_to(react_native)),
                    }

    hosts: dict[str, set[str]] = {}
    unresolved: list[str] = []
    for host, provider in HOST_PROVIDERS.items():
        answered: set[str] = set()
        for cls in OURS.findall((REPO / provider).read_text()):
            name = resolve(cls, names, bases)
            if name is None:
                unresolved.append(f"{host}: {cls}")
            else:
                answered.add(name)
        hosts[host] = answered

    # Two providers, because ReactCxxPlatform's falls through to the defaults:
    # the C++ platform answers AppState and Networking, and the shared defaults
    # answer the web APIs. Reading only the first reports the DOM and
    # `queueMicrotask` as answered by nobody, which is the kind of wrong a
    # checked-in table would then carry.
    upstream: set[str] = set()
    for provider in (
        react_native / "ReactCxxPlatform/react/runtime/ReactCxxTurboModuleProvider.cpp",
        react_native / "ReactCommon/react/nativemodule/defaults/DefaultTurboModules.cpp",
    ):
        for cls in THEIRS.findall(provider.read_text()):
            name = resolve(cls.split("::")[-1], names, bases)
            if name is None:
                unresolved.append(f"{provider.name}: {cls}")
            else:
                upstream.add(name)

    return {
        "asked": asked,
        "hosts": hosts,
        "upstream": upstream,
        "unresolved": unresolved,
    }


def answerer(name: str, result: dict) -> str:
    """Who answers `name` here: this repository, ReactCxxPlatform, or nobody."""
    everywhere = all(name in answered for answered in result["hosts"].values())
    somewhere = any(name in answered for answered in result["hosts"].values())
    if everywhere:
        return "basalt"
    if name in result["upstream"]:
        return "react-native"
    if somewhere:
        return "some hosts"
    return "nobody"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="fail if docs/platform-modules.json is stale"
    )
    arguments = parser.parse_args()

    rn_dir = react_native_dir()
    result = audit(rn_dir)
    problems = list(result["unresolved"])

    rows = []
    for name, asked in sorted(result["asked"].items()):
        rows.append(
            {
                "module": name,
                "asked": asked["how"],
                "answeredBy": answerer(name, result),
            }
        )

    # The modules this platform answers that React Native's own JavaScript never
    # asks for: a library's, or this project's own. Listed because the triage has
    # to account for them too -- a name here that nothing asks for is either a
    # third-party module or a typo, and the two look identical from outside.
    asked_names = set(result["asked"])
    extra = sorted(
        {name for answered in result["hosts"].values() for name in answered} - asked_names
    )

    if not arguments.check:
        print(f"React Native {rn_dir} asks for {len(rows)} native modules\n")
        width = max(len(row["module"]) for row in rows)
        for row in rows:
            print(f"  {row['module']:<{width}}  {row['asked']:<12} {row['answeredBy']}")
        print(f"\nanswered here and asked for by nobody upstream ({len(extra)}):")
        for name in extra:
            print(f"  {name}")
        if problems:
            print("\nunresolved provider classes:")
            for problem in problems:
                print(f"  {problem}")
        return 1 if problems else 0

    triage = json.loads(TRIAGE.read_text())
    statuses = triage["modules"]

    for row in rows:
        entry = statuses.get(row["module"])
        if entry is None:
            problems.append(
                f"{row['module']}: React Native asks for it and nothing in "
                f"docs/platform-modules.json says what happens here"
            )
            continue
        if entry["answeredBy"] != row["answeredBy"]:
            problems.append(
                f"{row['module']}: the file says {entry['answeredBy']} and the "
                f"code says {row['answeredBy']}"
            )
        if entry["asked"] != row["asked"]:
            problems.append(
                f"{row['module']}: the file says React Native asks with "
                f"{entry['asked']} and it asks with {row['asked']}"
            )
        if entry["answeredBy"] == "nobody" and not entry.get("effect"):
            problems.append(
                f"{row['module']}: nobody answers it and the file does not say "
                f"what an app sees"
            )

    for name in statuses:
        if name not in {row["module"] for row in rows} and name not in extra:
            problems.append(
                f"{name}: has a status and neither React Native nor this platform "
                f"mentions it any more"
            )

    for name in extra:
        if name not in statuses:
            problems.append(
                f"{name}: this platform answers it and nothing says what it is for"
            )

    # The divergence check: the same modules on all three desktops, bar the ones
    # a host implements itself.
    for name in sorted({n for answered in result["hosts"].values() for n in answered}):
        offering = {host for host, answered in result["hosts"].items() if name in answered}
        if len(offering) not in (0, 3):
            entry = statuses.get(name, {})
            if not entry.get("hostSpecific"):
                problems.append(
                    f"{name}: offered on {', '.join(sorted(offering))} and not "
                    f"the others, and the file does not say it is host-specific"
                )

    if problems:
        for problem in problems:
            print(problem)
    print(
        f"{len(rows)} modules asked for, {len(extra)} more answered here, "
        f"{len(problems)} problems"
    )
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
