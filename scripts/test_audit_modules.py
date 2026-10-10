#!/usr/bin/env python3
"""Tests for scripts/audit_modules.py, the parsing it rests on.

The audit itself is checked by running it: `--check` reads the three sources and
fails when `docs/platform-modules.json` is not what they say. What that cannot
check is the parsing, because the real sources parse correctly today -- and the
failures worth guarding against are the two that made the first version of this
script wrong about real modules.

The first was `class JSI_EXPORT NativeSourceCodeCxxSpec`, where a pattern that
reads the token after `class` reports the *macro* as the class name, so every
generated spec resolved to nothing. The second was a module class that declares
no name of its own: `AppStateModule : public NativeAppStateCxxSpec<AppStateModule>`
keeps its name on the spec, so a resolver that does not follow base classes
reports AppState as answered by nobody. Both produce a table that looks complete
and says a working module is missing.

Run with:  python3 scripts/test_audit_modules.py
"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def load():
    """The audit, imported from its path: it is a script rather than a module."""
    spec = importlib.util.spec_from_file_location(
        "audit_modules", REPO / "scripts" / "audit_modules.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


audit_modules = load()


def header(text: str) -> list[Path]:
    """One throwaway header holding `text`, as a list of paths to scan."""
    directory = Path(tempfile.mkdtemp())
    path = directory / "Module.h"
    path.write_text(text)
    return [path]


class Names(unittest.TestCase):
    def test_a_class_declaring_its_own_name_is_read(self):
        names = audit_modules.module_names(
            header(
                """
class DesktopClipboardModule : public NativeClipboardCxxSpec<DesktopClipboardModule> {
 public:
  static constexpr const char *kModuleName = "Clipboard";
};
"""
            )
        )
        self.assertEqual(names.get("DesktopClipboardModule"), "Clipboard")

    def test_an_export_macro_is_not_the_class_name(self):
        # The bug this guards: `class JSI_EXPORT Name` reads as a class called
        # JSI_EXPORT, and every generated spec in FBReactNativeSpecJSI.h is
        # declared that way -- so the whole upstream half of the audit resolved
        # to nothing and the table claimed React Native answers no modules.
        names = audit_modules.module_names(
            header(
                """
class JSI_EXPORT NativeSourceCodeCxxSpec : public TurboModule {
 public:
  static constexpr std::string_view kModuleName = "SourceCode";
};
"""
            )
        )
        self.assertEqual(names.get("NativeSourceCodeCxxSpec"), "SourceCode")
        self.assertNotIn("JSI_EXPORT", names)

    def test_a_name_belonging_to_the_next_class_is_not_borrowed(self):
        # A class that declares no name must not pick up the next class's, which
        # would report a module as answered under a name nothing asks for.
        names = audit_modules.module_names(
            header(
                """
class Quiet : public Something {
 public:
  void method();
};
class Loud : public TurboModule {
  static constexpr const char *kModuleName = "Loud";
};
"""
            )
        )
        self.assertNotIn("Quiet", names)
        self.assertEqual(names.get("Loud"), "Loud")


class Resolution(unittest.TestCase):
    def test_a_name_on_the_spec_is_followed_through_the_base(self):
        # The second bug: a module class usually keeps no name of its own.
        names = {"NativeAppStateCxxSpec": "AppState"}
        bases = {"AppStateModule": "NativeAppStateCxxSpec"}
        self.assertEqual(
            audit_modules.resolve("AppStateModule", names, bases), "AppState"
        )

    def test_a_class_with_no_name_anywhere_resolves_to_nothing(self):
        # Reported rather than skipped: an unresolved provider class means the
        # audit cannot say what that host offers, which is a failure and not a
        # blank row.
        self.assertIsNone(audit_modules.resolve("Mystery", {}, {}))

    def test_a_cycle_in_the_bases_does_not_hang(self):
        bases = {"A": "B", "B": "A"}
        self.assertIsNone(audit_modules.resolve("A", {}, bases))


class Answerer(unittest.TestCase):
    def result(self, hosts, upstream):
        return {"hosts": hosts, "upstream": set(upstream), "asked": {}, "unresolved": []}

    def test_all_three_hosts_is_this_repository(self):
        result = self.result(
            {"linux": {"Clipboard"}, "macos": {"Clipboard"}, "windows": {"Clipboard"}}, []
        )
        self.assertEqual(audit_modules.answerer("Clipboard", result), "basalt")

    def test_upstream_is_named_when_no_host_offers_it(self):
        result = self.result({"linux": set(), "macos": set(), "windows": set()}, ["AppState"])
        self.assertEqual(audit_modules.answerer("AppState", result), "react-native")

    def test_one_host_out_of_three_is_not_an_answer(self):
        # The divergence this exists to catch: a module offered on one desktop
        # and not the others reads as working to anyone testing on that desktop.
        result = self.result(
            {"linux": {"Only"}, "macos": set(), "windows": set()}, []
        )
        self.assertEqual(audit_modules.answerer("Only", result), "some hosts")

    def test_nobody_is_the_answer_when_nobody_offers_it(self):
        result = self.result({"linux": set(), "macos": set(), "windows": set()}, [])
        self.assertEqual(audit_modules.answerer("Missing", result), "nobody")


if __name__ == "__main__":
    unittest.main(verbosity=2)
