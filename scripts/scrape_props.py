#!/usr/bin/env python3
"""Reads React Native's own prop structs and writes down what they carry.

The platform support page has one row per attribute, and the list of attributes
is not ours to maintain: it is whatever `BaseViewProps`, `TextAttributes` and
`AccessibilityProps` declare in the React Native this platform builds against.
Typing that list out by hand means it is wrong the first time upstream adds a
prop -- quietly, because a missing row looks exactly like a row nobody has
filled in.

So this scrapes the headers and `scripts/check_support.js` renders from the
result. What that buys is the thing a hand-written table cannot do: a prop added
upstream has no status here, and the check fails until somebody says what this
platform does with it.

## What it parses, and why a regex is enough

A field declaration in these headers is one line of the form

    std::optional<FontWeight> fontWeight{};
    Float opacity{std::numeric_limits<Float>::quiet_NaN()};
    SharedColor backgroundColor{};

between `#pragma mark - Props` and the next `#pragma mark`. They are written by
hand, one per line, and nothing in them spans lines -- so a regex reads them
exactly, and anything it cannot read it reports rather than skips. A real parser
would need a C++ front end to answer a question these files answer in their
shape.

Run with:  python3 scripts/scrape_props.py --from-pin   # rewrite the inventory
           python3 scripts/scrape_props.py --check      # fail if it is stale
           python3 scripts/scrape_props.py              # read RN_DIR instead

## Yoga's props, which no struct here declares

A style name an app writes is not always a field in one of these structs.
`padding`, `margin`, `flex`, `inset` and the rest live in `yoga::Style`, behind
`YogaStylableProps`, which carries one field -- `yogaStyle` -- and no per-prop
members to scrape. Nothing in a host ever sees them: Fabric gives Yoga the
style, Yoga computes a frame, and the host applies the frame.

They are still props an app writes, so leaving them off the page made it say
less than it knew. `____LayoutStyle_Internal` is React Native's own list of
them, this file already reads it, and those names now come out as a group of
their own with the same rule as the rest: each one needs a status, and a name
upstream adds has none until somebody says what happens to it.

## Every style name is accounted for, which is the check this exists for

The three sources above do not partition the style names, and the gap used to
be invisible: `borderTopLeftRadius` is one of thirteen spellings that land in
`borderRadii`, and a reader looking for it found nothing and could not tell
whether it was unimplemented or unlisted. So SPELLINGS says which extra names
land in a field, and the scrape reports any style name that is neither a field,
nor one of its spellings, nor one of Yoga's. That report is what makes "every
value a `style` takes is on the page" a thing a check can say rather than a
thing somebody remembers.

## Style props and component props, which is upstream's line and not ours

reactnative.dev splits a component's props from the style props its `style`
takes, and the split is worth having here because `BaseViewProps` does not make
it: `opacity` and `onLayout` are fields side by side, and one is written inside
`style={{...}}` and the other is not.

That line is declared, so it is scraped rather than judged. React Native's Flow
types say exactly which names a style takes -- `____ViewStyle_InternalBase`,
`____LayoutStyle_Internal`, `____ShadowStyle_InternalCore`,
`____TextStyle_InternalBase` and `____TransformStyle_Internal` -- and
`ViewPropTypes.js`, `ViewAccessibility.js` and `TextProps.js` say which names a
component takes. So each field lands in one of three kinds:

  style       the name is in a style type
  component   the name is in a public prop type
  internal    neither: declared in ReactCommon and in no public prop type

`internal` is a real answer and not a gap. `TextAttributes::isHighlighted` and
`isPressable` are the innards of iOS's pressable text, `layoutDirection` is
filled in from the layout rather than from a prop, and `events` is the view's
event-registration bitmap. Nothing an app writes reaches them, so a support
column for them would be answering a question nobody asked.

ALIASES is the only hand-written part, and every entry is checked: ReactCommon
spells several of these differently from JavaScript (`borderRadii` for
`borderRadius`, `foregroundColor` for `color`, `alignment` for `textAlign`), and
an alias naming something upstream does not declare is reported rather than
believed.

## Which React Native it reads, and why the committed answer is the pinned one

`--from-pin` fetches the three headers from GitHub at `scripts/react-native.pin`,
which is the release CI builds and type-checks against, and is therefore the one
the support page is about. That is how the committed inventory is produced.

Without the flag it reads RN_DIR, the local checkout, which is normally `main` --
useful for seeing what upstream has added since the pin, and not what the page
should claim: v0.87.1 has 31 text attributes and main has 32, the extra being
`fontVariationSettings`. The two modes disagreeing is the point rather than a
problem, so the version is recorded in the output either way.

`--check` compares the committed file against the pin and needs the network;
CI does not run it, for that reason. What CI runs is
`scripts/check_support.js --check`, which reads the committed inventory and is
offline.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
INVENTORY = REPO / "docs" / "react-native-props.json"

# Which structs, where each one lives inside the React Native checkout, and the
# `#pragma mark` its fields sit under -- which is not the same word in every
# header: TextAttributes says `Fields` where the props classes say `Props`. Named
# rather than guessed, so a header that renames its section is a failure here
# instead of an empty list.
STRUCTS = [
    (
        "BaseViewProps",
        "packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h",
        "Props",
        "Every style and behaviour prop a <View> carries.",
    ),
    (
        "AccessibilityProps",
        "packages/react-native/ReactCommon/react/renderer/components/view/AccessibilityProps.h",
        "Props",
        "What a view tells the platform's accessible layer. BaseViewProps inherits these.",
    ),
    (
        "TextAttributes",
        "packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h",
        "Fields",
        "Per-fragment text styling, so a nested <Text> can differ from its parent.",
    ),
    (
        "ImageProps",
        "packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h",
        "Props",
        "What an <Image> carries beyond a view's own props: the source, how it "
        "fills its box, and the Android and iOS extras.",
    ),
    (
        "BaseTextInputProps",
        "packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h",
        "Props",
        "What a <TextInput> carries on every platform: the text, the placeholder, "
        "the tint colours and what the field will accept.",
    ),
    (
        "TextInputProps",
        "packages/react-native/ReactCommon/react/renderer/components/textinput/"
        "platform/ios/react/renderer/components/iostextinput/TextInputProps.h",
        "Props",
        "The iOS variant, which this platform reuses rather than forking: see "
        "docs/DECISIONS.md. It adds the selection and holds the traits below.",
    ),
    (
        "TextInputTraits",
        "packages/react-native/ReactCommon/react/renderer/components/textinput/"
        "platform/ios/react/renderer/components/iostextinput/primitives.h",
        "class TextInputTraits",
        "The keyboard and editing behaviour a field asks for. Declared with no "
        "section marker, so this one is read as a whole class.",
    ),
    (
        "ParagraphAttributes",
        "packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h",
        "Fields",
        "What belongs to a whole paragraph rather than to a fragment: how many "
        "lines it may take, how it is truncated, and whether it shrinks to fit.",
    ),
]

# Where React Native declares what a `style` takes. Each entry is a file and the
# types in it whose members are style props; a type that is not there any more is
# a failure rather than a smaller list.
STYLE_TYPES = [
    (
        "packages/react-native/Libraries/StyleSheet/StyleSheetTypes.js",
        [
            "____LayoutStyle_Internal",
            "____ShadowStyle_InternalCore",
            "____ViewStyle_InternalBase",
            "____TextStyle_InternalBase",
            # An <Image>'s own four, which a <View>'s style does not take:
            # `resizeMode`, `objectFit`, `tintColor` and `overlayColor`. Read
            # once `ImageProps` was scraped, because three of them are its
            # fields and the fourth is another spelling of one.
            "____ImageStyle_InternalCore",
        ],
    ),
    (
        "packages/react-native/Libraries/StyleSheet/private/_TransformStyle.js",
        ["____TransformStyle_Internal"],
    ),
]

# Yoga's own, out of the same file. Named separately from STYLE_TYPES because
# these are the style names that reach no struct here: `YogaStylableProps` holds
# one `yogaStyle` field and nothing per prop, so there is nothing to scrape and
# the Flow type is the list.
LAYOUT_TYPE = (
    "packages/react-native/Libraries/StyleSheet/StyleSheetTypes.js",
    "____LayoutStyle_Internal",
)

# And where it declares what a component takes. Every name these files declare
# counts, which is deliberately looser than reading one type: `ViewProps` spreads
# a dozen others, and the question being asked is only "is this a public prop".
PROP_TYPE_FILES = [
    "packages/react-native/Libraries/Components/View/ViewPropTypes.js",
    "packages/react-native/Libraries/Components/View/ViewAccessibility.js",
    "packages/react-native/Libraries/Text/TextProps.js",
    # A <TextInput> too, because `TextAttributes` serves both and some of it is
    # only public there: `lineBreakModeIOS` is a TextInput prop and not a Text
    # one, which is why it was reading as internal.
    "packages/react-native/Libraries/Components/TextInput/TextInput.flow.js",
    # And an <Image>'s, so `blurRadius` and `capInsets` read as the public props
    # they are rather than as internals. Without it every ImageProps field but
    # two landed in "Neither", which is a page saying nothing an app writes
    # reaches them.
    "packages/react-native/Libraries/Image/ImageProps.js",
]

# ReactCommon's spelling -> JavaScript's, where they differ. Checked against what
# was scraped: an alias that names nothing upstream declares is reported.
ALIASES = {
    # A struct of eight corners against the names of each.
    "borderRadii": "borderRadius",
    "borderColors": "borderColor",
    "borderCurves": "borderCurve",
    "borderStyles": "borderStyle",
    # Still behind the experimental prefix in the pinned release.
    "backgroundImage": "experimental_backgroundImage",
    "backgroundSize": "experimental_backgroundSize",
    "backgroundPosition": "experimental_backgroundPosition",
    "backgroundRepeat": "experimental_backgroundRepeat",
    "accessibilityOrder": "experimental_accessibilityOrder",
    # The platform suffix JavaScript carries and ReactCommon does not.
    "shouldRasterize": "shouldRasterizeIOS",
    "lineBreakStrategy": "lineBreakStrategyIOS",
    "lineBreakMode": "lineBreakModeIOS",
    # An <Image>'s source, which ReactCommon keeps as a list: React Native
    # resolves one `source` into the candidates a density picker chooses from.
    "sources": "source",
    # A paragraph, where ReactCommon's name is longer than JavaScript's.
    "maximumNumberOfLines": "numberOfLines",
    # Text, where ReactCommon's names are its own.
    "foregroundColor": "color",
    "alignment": "textAlign",
    "baseWritingDirection": "writingDirection",
    "textDecorationLineType": "textDecorationLine",
    # A trait whose name is the behaviour and whose prop is the switch.
    "autocapitalizationType": "autoCapitalize",
    # Capitalisation, which is the whole difference.
    "testId": "testID",
    "nativeId": "nativeID",
}

# The extra JavaScript names that land in one ReactCommon field, beyond the one
# ALIASES gives it. Hand-written and checked the same way: a spelling React
# Native does not declare is reported rather than believed.
#
# These are not aliases in the sense ALIASES means -- `borderTopLeftRadius` is
# not another name for the whole struct, it is one corner of it -- but for this
# page they belong on the same row, because one field is what a host reads and
# one entry is what says whether it works.
SPELLINGS = {
    "borderRadii": [
        "borderTopLeftRadius",
        "borderTopRightRadius",
        "borderBottomLeftRadius",
        "borderBottomRightRadius",
        "borderTopStartRadius",
        "borderTopEndRadius",
        "borderBottomStartRadius",
        "borderBottomEndRadius",
        "borderStartStartRadius",
        "borderStartEndRadius",
        "borderEndStartRadius",
        "borderEndEndRadius",
    ],
    "resizeMode": ["objectFit"],
    "borderColors": [
        "borderTopColor",
        "borderBottomColor",
        "borderLeftColor",
        "borderRightColor",
        "borderStartColor",
        "borderEndColor",
        "borderBlockColor",
        "borderBlockStartColor",
        "borderBlockEndColor",
    ],
}

# Style names React Native declares in JavaScript and no struct here reads.
#
# Not a gap in this file: a prop reaches a Fabric platform only if some
# ReactCommon field takes it, and these three have none that any of these hosts
# can see. They are listed rather than dropped because an app can still write
# them, and "the page does not mention it" is indistinguishable from "nobody has
# looked". Each one's status says what is actually true of it.
#
# Checked like the rest: a name here that upstream stops declaring, or starts
# declaring a field for, is reported.
UNREAD = [
    "elevation",
    "userSelect",
    "verticalAlign",
]

# `  <type> <name>{...};` or `  <type> <name>;`, with the type allowed to carry
# templates, namespaces and references, and the initialiser allowed to be
# anything at all -- which it has to be, because several of these fields default
# to `{std::numeric_limits<Float>::quiet_NaN()}`. A first attempt skipped every
# line with a parenthesis in it, to skip the methods, and silently lost the seven
# Float fields that have a call in their initialiser: `opacity`, `fontSize`,
# `letterSpacing`, `lineHeight` and the rest. Hence this order: match a field
# first, and only decide what to skip when it does not match.
FIELD = re.compile(
    r"^  (?P<type>[A-Za-z_][A-Za-z0-9_:<>,\s*&]*?)\s"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?:\{.*\}|=[^;]*)?;\s*(?://.*)?$"
)

# Lines in the region that are not fields and are not meant to be: a comment, a
# preprocessor line, a brace, a nested type, a static, or a method -- which has
# its parenthesis before any initialiser brace. Anything else that fails to parse
# is reported rather than dropped.
SKIP = re.compile(
    r"^\s*(//|/\*|\*|#|\}|\{|$)|\busing\b|\bstruct\b|\bclass\b|\benum\b|\bstatic\b"
    # An access specifier, which a class body has and a `#pragma mark` region
    # does not: reading a whole class brought `public:` along.
    r"|^\s*(public|private|protected):"
)


# An `operator==` declaration, which the field regex reads as a field called
# `operator`: its `==(...)` matches the "or an initialiser" branch, because that
# branch has to accept anything at all. Checked for by name rather than by
# tightening the regex, so a field whose initialiser contains `==` keeps
# working.
#
# Found by `ParagraphAttributes`, which declares one inside its field region
# where the other three declare theirs after a `#pragma mark`.
OPERATOR = re.compile(r"\boperator\b")


def looks_like_a_method(line: str) -> bool:
    brace = line.find("{")
    paren = line.find("(")
    if paren < 0:
        return False
    return brace < 0 or paren < brace


def flow_members(text: str, type_name: str) -> list[str] | None:
    """The members of one Flow object type, or None if it is not declared.

    Depth-aware rather than a grep over the region: `shadowOffset` is a nested
    `{width, height}`, and a flat scan reports those two as style props of their
    own. Counts brackets as well as braces, an array of objects nesting the same
    way.
    """
    declaration = re.search(
        r"^(?:export )?type " + re.escape(type_name) + r"\s*=\s*", text, re.M
    )
    if declaration is None:
        return None
    opening = text.index("{", declaration.end())
    depth = 0
    members: list[str] = []
    for line in text[opening:].splitlines():
        if depth == 1:
            member = re.match(r"\+?([A-Za-z_][A-Za-z0-9_]*)\??\s*:", line.strip())
            if member is not None:
                members.append(member.group(1))
        depth += line.count("{") + line.count("[") - line.count("}") - line.count("]")
        if depth <= 0:
            break
    return members


def flow_declared_names(text: str) -> set[str]:
    """Every name declared as a member anywhere in a Flow file."""
    return set(re.findall(r"^\s*\+?([A-Za-z_][A-Za-z0-9_]*)\??\s*:", text, re.M))


def fetch_from_pin(relative: str, tag: str) -> str:
    """One header, read from GitHub at the pinned tag."""
    url = (
        "https://raw.githubusercontent.com/facebook/react-native/"
        f"{tag}/{relative}"
    )
    with urllib.request.urlopen(url, timeout=30) as response:
        return response.read().decode()


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


def react_native_version(rn_dir: Path) -> str:
    package = rn_dir / "packages/react-native/package.json"
    try:
        return json.loads(package.read_text())["version"]
    except (OSError, KeyError, json.JSONDecodeError):
        return "unknown"


def pinned_version() -> str:
    pin = (REPO / "scripts" / "react-native.pin").read_text()
    for line in pin.splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            return line
    return "unknown"


def props_region(text: str, section: str) -> list[str]:
    """The lines between `#pragma mark - <section>` and the next `#pragma mark`."""
    marks = [i for i, line in enumerate(text.splitlines()) if "#pragma mark" in line]
    lines = text.splitlines()
    start = None
    for index in marks:
        if section in lines[index]:
            start = index + 1
            break
    if start is None:
        return []
    for index in marks:
        if index > start:
            return lines[start:index]
    return lines[start:]


def class_body(text: str, name: str) -> list[str]:
    """The lines inside `class <name>` or `struct <name>`, or none.

    For a struct that declares its fields without a `#pragma mark` to find them
    by. `TextInputTraits` is the one: twenty props an app writes on a
    `<TextInput>`, in a class body with nothing but comments between them, and
    no section marker anywhere in the file.

    Brace-counted from the declaration, so a nested type inside it ends where it
    ends rather than closing the class early.
    """
    declaration = re.search(
        r"^(?:class|struct)\s+" + re.escape(name) + r"\b[^{;]*\{", text, re.M
    )
    if declaration is None:
        return []
    depth = 0
    lines: list[str] = []
    for line in text[declaration.end() - 1 :].splitlines():
        if depth == 1:
            lines.append(line)
        depth += line.count("{") - line.count("}")
        if depth <= 0:
            break
    return lines


def scrape(text: str, section: str) -> tuple[list[str], list[str]]:
    """The field names, and the lines that looked like fields and did not parse."""
    names: list[str] = []
    unread: list[str] = []
    # `class Name` asks for the whole class body: a struct that declares its
    # fields with no `#pragma mark` to find them by. Anything else is a section.
    region = (
        class_body(text, section[len("class ") :])
        if section.startswith("class ")
        else props_region(text, section)
    )
    for line in region:
        match = FIELD.match(line)
        if match is not None and not OPERATOR.search(line):
            names.append(match.group("name"))
            continue
        if SKIP.search(line) or looks_like_a_method(line):
            continue
        unread.append(line.strip())
    return names, unread


def kind_of(name: str, style: set[str], public: set[str]) -> tuple[str, str]:
    """Which bucket a field is in, and the JavaScript name it was matched by."""
    spelling = ALIASES.get(name, name)
    if spelling in style:
        return "style", spelling
    if spelling in public:
        return "component", spelling
    return "internal", spelling


def account(
    style_props: set[str],
    structs: list[dict],
    layout_members: list[str],
    unread: list[str],
) -> tuple[list[str], list[str], list[str]]:
    """Which style names are Yoga's, which are read by nothing, and what is left.

    The left-over list is the point. A style name that is neither a field here,
    nor one of a field's other spellings, nor one of Yoga's, nor one somebody has
    judged unread, is a name an app can write and this page cannot answer for --
    which is what `padding` and `margin` were until somebody went looking for
    them. Returned as problems rather than dropped.
    """
    claimed: set[str] = set()
    for struct in structs:
        for prop in struct["props"]:
            claimed.add(prop["name"])
            claimed.add(prop.get("javascript", prop["name"]))
            claimed.update(prop.get("spellings", []))

    layout = sorted(name for name in layout_members if name not in claimed)
    claimed.update(layout)

    problems: list[str] = []
    for name in unread:
        if name not in style_props:
            problems.append(
                f"UNREAD lists {name}, which React Native no longer declares as a style prop"
            )
        if name in claimed:
            problems.append(
                f"UNREAD lists {name}, and something here now reads it: take it out and "
                "give it the status its field deserves"
            )
    unread_names = sorted(name for name in unread if name in style_props)
    claimed.update(unread_names)

    for name in sorted(style_props):
        if name not in claimed:
            problems.append(
                f"{name} is a style prop React Native declares and nothing here accounts "
                "for: add it to SPELLINGS beside the field it lands in, or give it a "
                "status of its own"
            )

    return layout, unread_names, problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the inventory is stale")
    parser.add_argument(
        "--from-pin",
        action="store_true",
        help="read the headers from GitHub at scripts/react-native.pin rather than RN_DIR",
    )
    arguments = parser.parse_args()

    pin = pinned_version()
    from_pin = arguments.from_pin or arguments.check
    if from_pin:
        rn_dir = None
        version = pin.lstrip("v")
    else:
        rn_dir = react_native_dir()
        version = react_native_version(rn_dir)

    inventory = {
        "comment": [
            "Scraped from React Native by scripts/scrape_props.py. Do not edit.",
            "The statuses live in docs/platform-support.json, keyed by these names;",
            "scripts/check_support.js renders the page from the two together and",
            "fails when a prop here has no status or a status names a prop that is",
            "no longer declared.",
        ],
        "reactNative": version,
        "readFrom": "the pin" if from_pin else "RN_DIR",
        "structs": [],
    }

    problems = []

    def read(relative: str) -> str | None:
        """One file out of React Native, from the pin or from the checkout."""
        if from_pin:
            try:
                return fetch_from_pin(relative, pin)
            except OSError as error:
                problems.append(f"{relative} could not be read at {pin}: {error}")
                return None
        path = rn_dir / relative
        if not path.is_file():
            problems.append(f"{relative} is not in {rn_dir}")
            return None
        return path.read_text()

    # What a `style` takes, and what a component takes. Both scraped, because
    # which of the two a prop belongs to is upstream's answer and not ours.
    style_props: set[str] = set()
    # Kept per type as well as pooled, so each struct's section can name the
    # types its own style props came from rather than all of them: an <Image>'s
    # three are declared in one type, and a page that listed five would be
    # saying something it had not read.
    members_by_type: list[tuple[str, set[str]]] = []
    for relative, type_names in STYLE_TYPES:
        text = read(relative)
        if text is None:
            continue
        for type_name in type_names:
            members = flow_members(text, type_name)
            if members is None:
                problems.append(f"{relative}: no `type {type_name}`; has it been renamed?")
                continue
            if not members:
                problems.append(f"{relative}: `type {type_name}` parsed to no members")
            style_props.update(members)
            members_by_type.append((type_name, set(members)))

    public_props: set[str] = set()
    for relative in PROP_TYPE_FILES:
        text = read(relative)
        if text is None:
            continue
        names = flow_declared_names(text)
        if not names:
            problems.append(f"{relative}: no prop names found")
        public_props.update(names)

    for spelled, alias in sorted(ALIASES.items()):
        if alias not in style_props and alias not in public_props:
            problems.append(
                f"ALIASES says {spelled} is JavaScript's {alias}, which React Native "
                f"{version} declares in neither a style type nor a prop type"
            )

    for field, spellings in sorted(SPELLINGS.items()):
        for spelling in spellings:
            if spelling not in style_props:
                problems.append(
                    f"SPELLINGS says {spelling} lands in {field}, which React Native "
                    f"{version} does not declare as a style prop"
                )

    # Yoga's. Read from the layout type and then narrowed to the names no struct
    # here declares: `borderWidth` and `zIndex` are in that type as well, because
    # Yoga needs a border's width to lay out inside it, and both are fields a
    # host reads.
    layout_relative, layout_type = LAYOUT_TYPE
    layout_text = read(layout_relative)
    layout_members: list[str] = []
    if layout_text is not None:
        members = flow_members(layout_text, layout_type)
        if members is None:
            problems.append(
                f"{layout_relative}: no `type {layout_type}`; has it been renamed?"
            )
        elif not members:
            problems.append(f"{layout_relative}: `type {layout_type}` parsed to no members")
        else:
            layout_members = members

    inventory["styleProps"] = sorted(style_props)
    inventory["styleTypes"] = [
        {"file": relative, "types": type_names} for relative, type_names in STYLE_TYPES
    ]
    inventory["propTypeFiles"] = list(PROP_TYPE_FILES)

    for name, relative, section, note in STRUCTS:
        text = read(relative)
        if text is None:
            continue
        # `unreadable` rather than `unread`: the module's UNREAD is a list of
        # style names, and these are lines that did not parse.
        names, unreadable = scrape(text, section)
        if not names:
            problems.append(
                f"{relative}: no fields under `#pragma mark - {section}`; has the header "
                "changed shape?"
            )
        for line in unreadable:
            problems.append(f"{relative}: cannot read {line!r}")
        props = []
        for field in names:
            kind, spelling = kind_of(field, style_props, public_props)
            prop = {"name": field, "kind": kind}
            if spelling != field:
                prop["javascript"] = spelling
            if field in SPELLINGS:
                prop["spellings"] = list(SPELLINGS[field])
            props.append(prop)
        # The types that declared this struct's own style names, in the order
        # STYLE_TYPES reads them.
        style_names = {prop.get("javascript", prop["name"]) for prop in props if prop["kind"] == "style"}
        style_from = [
            type_name
            for type_name, members in members_by_type
            if style_names & members
        ]
        inventory["structs"].append(
            {
                "name": name,
                "header": relative,
                "note": note,
                "styleFrom": style_from,
                "props": props,
            }
        )

    layout_names, unread_names, accounting = account(
        style_props, inventory["structs"], layout_members, UNREAD
    )
    problems.extend(accounting)

    inventory["layoutProps"] = layout_names
    # Emitted as a struct of its own so that everything downstream -- a status
    # per row, the audit against the hosts' sources, the page's tables -- works
    # on it unchanged. `YogaStylableProps` is where ReactCommon really keeps
    # them, even though it keeps them in one field.
    inventory["structs"].append(
        {
            "name": "YogaStylableProps",
            "header": "packages/react-native/ReactCommon/react/renderer/components/view/YogaStylableProps.h",
            "note": "Everything Yoga lays out with: padding, margin, flex, inset, gap and "
            "the rest. This struct carries one field, `yogaStyle`, so these names come "
            "from React Native's own `____LayoutStyle_Internal` instead.",
            "styleFrom": [layout_type],
            "props": [{"name": name, "kind": "style"} for name in layout_names],
        }
    )

    inventory["unreadStyleProps"] = unread_names
    inventory["structs"].append(
        {
            "name": "StyleSheetTypes",
            "header": "packages/react-native/Libraries/StyleSheet/StyleSheetTypes.js",
            "note": "Style names JavaScript declares and no ReactCommon struct these hosts "
            "read takes. An app can write them, so they are listed; what each one's status "
            "says is where it actually goes.",
            "styleFrom": [],
            "props": [{"name": name, "kind": "style"} for name in unread_names],
        }
    )

    for problem in problems:
        print(problem)

    rendered = json.dumps(inventory, indent=2) + "\n"
    existing = INVENTORY.read_text() if INVENTORY.exists() else None
    stale = existing != rendered

    if arguments.check:
        if stale:
            print(
                f"{INVENTORY.relative_to(REPO)} is not what scripts/scrape_props.py reads from "
                f"React Native {version}; run python3 scripts/scrape_props.py"
            )
    elif stale:
        INVENTORY.write_text(rendered)
        print(f"wrote {INVENTORY.relative_to(REPO)}")

    total = sum(len(struct["props"]) for struct in inventory["structs"])
    by_kind: dict[str, int] = {}
    for struct in inventory["structs"]:
        for prop in struct["props"]:
            by_kind[prop["kind"]] = by_kind.get(prop["kind"], 0) + 1
    source = f"the pin, {pin}" if from_pin else f"RN_DIR, React Native {version}"
    print(f"{total} props across {len(inventory['structs'])} structs, from {source}")
    print(
        "    "
        + ", ".join(f"{count} {kind}" for kind, count in sorted(by_kind.items()))
        + f"; {len(style_props)} style props declared upstream"
    )
    if not from_pin and f"v{version}" != pin:
        print(
            f"    CI pins {pin}, and the committed inventory is built from it:"
            " run --from-pin to rewrite it"
        )

    return 0 if not problems and not (arguments.check and stale) else 1


if __name__ == "__main__":
    sys.exit(main())
