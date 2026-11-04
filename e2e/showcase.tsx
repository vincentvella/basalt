/**
 * The app the screenshots come from.
 *
 * Every other app in this directory is a fixture: it exists to make one
 * behaviour assertable and looks like it. This one exists to be looked at, so
 * that the README and the website can show a window instead of describing one.
 * It is still ordinary React Native -- flexbox, StyleSheet, Pressable,
 * ScrollView, TextInput, Switch -- plus the two things a phone has no use for
 * and a desktop screenshot should show: a window whose size the app chose, and
 * a title bar the app drew itself.
 *
 * Deliberately not in `scripts/compare_all.sh`'s list. That list is a parity
 * obligation, and this app's job is to be redesigned whenever the screenshots
 * want redesigning, which is the opposite of a fixture's job.
 *
 * Run it like any of the others:
 *
 *   scripts/bundle.sh ../react-native --platform macos \
 *     --entry e2e/showcase.tsx --out showcase
 *   ./build/basalt_appkit build/showcase.macos.jsbundle.js BasaltShowcase
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {
  AppRegistry,
  Platform,
  Pressable,
  ScrollView,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  View,
} from 'react-native';
import {TitleBar, useTitleBarMetrics, useWindow} from 'basalt-core';

const INK = '#16181d';
const PANEL = '#1c1f26';
const LINE = '#272b34';
const BONE = '#f7f6f3';
const MUTED = '#9aa0a8';
const EMBER = '#e2561f';
const AMBER = '#f0b429';

type Entry = {
  key: string;
  name: string;
  group: string;
  dot: string;
  status: string;
  blurb: string[];
  facts: string[];
};

// Written out rather than generated, because the generated version of this is
// the support table on the website and it is a table. What a screenshot wants
// is prose somebody would read.
const ENTRIES: Entry[] = [
  {
    key: 'linux',
    name: 'Linux',
    group: 'Desktops',
    dot: EMBER,
    status: 'GTK4',
    blurb: [
      'A view is a GtkWidget, and a paragraph is laid out by Pango over the font the desktop chose. Narrowing the window re-wraps it the way it does on a phone, because the same Yoga tree is asking the same question of a different text engine.',
      'The host builds against GTK 4.14 and later. Notifications go over D-Bus, the file dialogs are the portal ones, and the accessibility tree is AT-SPI, so Orca reads an app without it knowing.',
      'Nothing here is an emulator and nothing is a browser. The binary links GTK and the same ReactCommon a phone links, and there is no third thing in between.',
    ],
    facts: ['126 of 137 props', 'Wayland and X11', 'AT-SPI'],
  },
  {
    key: 'macos',
    name: 'macOS',
    group: 'Desktops',
    dot: AMBER,
    status: 'AppKit',
    blurb: [
      'An NSView per view, Core Text for every paragraph, and the menu bar the system expects an application to own. The window you are looking at drew its own title bar, which is why the app name sits beside the traffic lights.',
      'The same binary answers VoiceOver through NSAccessibility, posts through UNUserNotificationCenter, and ships as an .app that runs on a Mac other than the one that built it.',
      'Fast Refresh works the way it does on a phone, because what reloads is React Native\u2019s own packager talking to React Native\u2019s own runtime.',
    ],
    facts: ['130 of 141 props', 'Apple silicon and Intel', 'VoiceOver'],
  },
  {
    key: 'windows',
    name: 'Windows',
    group: 'Desktops',
    dot: '#4f8ef7',
    status: 'Win32',
    blurb: [
      'Win32 windows, Direct2D for everything drawn, and DirectWrite for every line of text. The caption buttons stay the system’s and the app answers HTCAPTION for the parts of its header that should move the window.',
      'Furthest behind of the three, and the support table says exactly where rather than leaving you to find out. Every prop that is not implemented has a row and a reason.',
      'Built with clang-cl and Ninja, or with the Visual Studio generator and MSBuild for anyone who would rather open the project than type at it.',
    ],
    facts: ['124 of 137 props', 'Direct2D', 'UI Automation'],
  },
  {
    key: 'second-windows',
    name: 'Second windows',
    group: 'Desktop APIs',
    dot: '#7a5cf0',
    status: '',
    blurb: [
      'Open a second window and it is a second React root, sized and positioned by the app, able to refuse to close while there is unsaved work in it.',
      'A window is a surface is a root, which is Fabric’s own grain rather than a simplification invented here. Geometry is addressed by window, so a hook inside one reports that one and not whichever the person is looking at.',
      'None of this exists in React Native, because none of it exists on a phone.',
    ],
    facts: ['<Window>', 'useWindow()', 'useCloseRequest()'],
  },
  {
    key: 'menus',
    name: 'Menus',
    group: 'Desktop APIs',
    dot: '#4bbf8a',
    status: '',
    blurb: [
      'A menu bar described in JSX and built by the host out of NSMenu, GMenu or a Win32 HMENU, with the roles each desktop expects already in the right place.',
      'Context menus come from the same description, including submenus and the checkable and radio items a desktop menu is allowed to have.',
      'Shortcuts are registered rather than guessed, so Cmd+W closes a window on a Mac and Ctrl+W does on the other two without the app writing that down twice.',
    ],
    facts: ['<MenuBar>', 'useContextMenu()', 'useShortcut()'],
  },
  {
    key: 'dialogs',
    name: 'File dialogs',
    group: 'Desktop APIs',
    dot: '#d9578f',
    status: '',
    blurb: [
      'Ask for a file and the desktop’s own dialog opens: NSOpenPanel, the GTK portal, IFileOpenDialog. Not a web view pretending to be one, and not a picker this project drew.',
      'Filters, directories, multiple selection and a suggested name, answered as a promise. A cancel is null rather than an error, because a person changing their mind is not a failure.',
      'The portal route on Linux means it also works from inside a Flatpak sandbox, where an app has no business reading the filesystem directly.',
    ],
    facts: ['openFile()', 'saveFile()', 'openDirectory()'],
  },
  {
    key: 'drag',
    name: 'Drag and drop',
    group: 'Desktop APIs',
    dot: '#3fb6c4',
    status: '',
    blurb: [
      'Files dragged in from the file manager, text dragged out to another application, and the walk that finds the deepest view willing to accept what is hovering over it.',
      'Both vocabularies are read, Android’s MIME types and iOS’s uniform type identifiers, because an app written for a phone already says one of them.',
      'A desktop is the only place this question comes up, which is why React Native has no answer for it.',
    ],
    facts: ['useDropTarget()', 'startDrag()', 'MIME and UTI'],
  },
];

function Dot({color}: {color: string}) {
  return <View style={[styles.dot, {backgroundColor: color}]} />;
}

function Sidebar({
  selected,
  onSelect,
  query,
  onQuery,
}: {
  selected: string;
  onSelect: (key: string) => void;
  query: string;
  onQuery: (text: string) => void;
}) {
  const matches = ENTRIES.filter(entry =>
    entry.name.toLowerCase().includes(query.trim().toLowerCase()),
  );
  const groups = matches.reduce<Record<string, Entry[]>>((all, entry) => {
    (all[entry.group] ??= []).push(entry);
    return all;
  }, {});

  return (
    <View style={styles.sidebar}>
      <TextInput
        style={styles.search}
        value={query}
        onChangeText={onQuery}
        placeholder="Filter"
        placeholderTextColor={MUTED}
        accessibilityLabel="Filter the list"
      />
      <ScrollView style={styles.sidebarScroll}>
        {Object.entries(groups).map(([group, entries]) => (
          <View key={group}>
            <Text style={styles.groupLabel}>{group}</Text>
            {entries.map(entry => {
              const active = entry.key === selected;
              return (
                <Pressable
                  key={entry.key}
                  onPress={() => onSelect(entry.key)}
                  accessibilityRole="button"
                  accessibilityLabel={entry.name}
                  style={({pressed}) => [
                    styles.row,
                    pressed && styles.rowPressed,
                    active && styles.rowActive,
                  ]}>
                  <Dot color={entry.dot} />
                  <Text
                    numberOfLines={1}
                    style={[styles.rowLabel, active && styles.rowLabelActive]}>
                    {entry.name}
                  </Text>
                  {entry.status === '' ? null : (
                    <Text style={styles.rowMeta}>{entry.status}</Text>
                  )}
                </Pressable>
              );
            })}
          </View>
        ))}
        {matches.length === 0 ? (
          <Text style={styles.empty}>Nothing matches {query}</Text>
        ) : null}
      </ScrollView>
    </View>
  );
}

function Detail({entry}: {entry: Entry}) {
  const [native, setNative] = React.useState(true);

  return (
    <ScrollView style={styles.detail} contentContainerStyle={styles.detailBody}>
      <Text style={styles.kicker}>{entry.group}</Text>
      <Text style={styles.title}>{entry.name}</Text>

      <View style={styles.chips}>
        {entry.facts.map(fact => (
          <View key={fact} style={styles.chip}>
            <Text style={styles.chipLabel}>{fact}</Text>
          </View>
        ))}
      </View>

      {entry.blurb.map(paragraph => (
        <Text key={paragraph.slice(0, 24)} style={styles.paragraph}>
          {paragraph}
        </Text>
      ))}

      <View style={styles.card}>
        <View style={styles.cardRow}>
          <View style={styles.cardText}>
            <Text style={styles.cardTitle}>Native widgets</Text>
            <Text style={styles.cardNote}>
              Off would be a browser engine, which is the other way of doing
              this and not the one here.
            </Text>
          </View>
          <Switch
            value={native}
            onValueChange={setNative}
            accessibilityLabel="Native widgets"
          />
        </View>

        <View style={styles.buttons}>
          <Pressable
            style={({pressed}) => [styles.primary, pressed && styles.pressed]}
            accessibilityRole="button">
            <Text style={styles.primaryLabel}>Read the docs</Text>
          </Pressable>
          <Pressable
            style={({pressed}) => [styles.secondary, pressed && styles.pressed]}
            accessibilityRole="button">
            <Text style={styles.secondaryLabel}>Platform support</Text>
          </Pressable>
        </View>
      </View>
    </ScrollView>
  );
}

function App() {
  const {height, buttonsWidth} = useTitleBarMetrics();
  const {setSize} = useWindow();
  // The three desktop keys are Platform.OS values, so a host opens on its
  // own page and the screenshot taken there is about the desktop it was
  // taken on.
  // React Native's own types do not know `linux`, because upstream has no
  // such platform; the string is there at runtime all the same.
  const here = String(Platform.OS);
  const [selected, setSelected] = React.useState<string>(
    ENTRIES.some(e => e.key === here) ? here : 'macos',
  );
  const [query, setQuery] = React.useState('');

  // One size on every desktop, so the three screenshots can sit beside each
  // other without one of them having been resized by hand afterwards.
  React.useEffect(() => {
    setSize(1040, 560);
  }, [setSize]);

  const entry = ENTRIES.find(e => e.key === selected) ?? ENTRIES[0];

  return (
    <View style={styles.page}>
      <TitleBar style="hidden" title="Basalt" />
      <TitleBar.DragRegion
        style={[
          styles.header,
          {height},
          // The caption buttons belong to the system and are drawn over this
          // header, so the app leaves them the room they need. macOS puts them
          // on the left, the other two on the right.
          Platform.OS === 'macos'
            ? {paddingLeft: buttonsWidth + 16}
            : {paddingRight: buttonsWidth + 16},
        ]}>
        <Text style={styles.appName}>Basalt</Text>
        <Text style={styles.appWhere}>{entry.name}</Text>
      </TitleBar.DragRegion>

      <View style={styles.body}>
        <Sidebar
          selected={selected}
          onSelect={setSelected}
          query={query}
          onQuery={setQuery}
        />
        <Detail entry={entry} />
      </View>

      <View style={styles.status}>
        <View style={styles.statusDot} />
        <Text style={styles.statusText}>
          One codebase, three desktops, on React Native&apos;s own C++
        </Text>
        <View style={styles.spacer} />
        <Text style={styles.statusText}>{Platform.OS}</Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: INK},

  header: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 10,
    paddingHorizontal: 16,
    backgroundColor: PANEL,
    borderBottomWidth: 1,
    borderBottomColor: LINE,
  },
  appName: {color: BONE, fontSize: 13, fontWeight: '600'},
  appWhere: {color: MUTED, fontSize: 13},

  body: {flex: 1, flexDirection: 'row'},

  sidebar: {
    width: 232,
    backgroundColor: PANEL,
    borderRightWidth: 1,
    borderRightColor: LINE,
    paddingTop: 12,
  },
  search: {
    marginHorizontal: 12,
    marginBottom: 10,
    paddingHorizontal: 10,
    paddingVertical: 6,
    borderRadius: 6,
    borderWidth: 1,
    borderColor: LINE,
    backgroundColor: INK,
    color: BONE,
    fontSize: 13,
  },
  sidebarScroll: {flex: 1},
  groupLabel: {
    color: MUTED,
    fontSize: 11,
    fontWeight: '600',
    letterSpacing: 0.6,
    textTransform: 'uppercase',
    paddingHorizontal: 16,
    paddingTop: 12,
    paddingBottom: 6,
  },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 10,
    marginHorizontal: 8,
    paddingHorizontal: 8,
    paddingVertical: 7,
    borderRadius: 6,
  },
  rowPressed: {backgroundColor: '#22262f'},
  rowActive: {backgroundColor: '#2c313c'},
  rowLabel: {flex: 1, color: MUTED, fontSize: 13},
  rowLabelActive: {color: BONE},
  rowMeta: {color: '#6c727c', fontSize: 11},
  dot: {width: 8, height: 8, borderRadius: 4},
  empty: {color: MUTED, fontSize: 13, padding: 16},

  detail: {flex: 1},
  detailBody: {padding: 28, paddingBottom: 36},
  kicker: {
    color: EMBER,
    fontSize: 11,
    fontWeight: '600',
    letterSpacing: 0.8,
    textTransform: 'uppercase',
    marginBottom: 6,
  },
  title: {color: BONE, fontSize: 28, fontWeight: '700', marginBottom: 14},

  chips: {flexDirection: 'row', flexWrap: 'wrap', gap: 8, marginBottom: 18},
  chip: {
    paddingHorizontal: 10,
    paddingVertical: 4,
    borderRadius: 999,
    borderWidth: 1,
    borderColor: LINE,
    backgroundColor: PANEL,
  },
  chipLabel: {color: MUTED, fontSize: 12},

  paragraph: {color: '#c6cbd2', fontSize: 14, lineHeight: 23, marginBottom: 14},

  card: {
    marginTop: 10,
    padding: 18,
    borderRadius: 10,
    borderWidth: 1,
    borderColor: LINE,
    backgroundColor: PANEL,
  },
  cardRow: {flexDirection: 'row', alignItems: 'center', gap: 16},
  cardText: {flex: 1},
  cardTitle: {color: BONE, fontSize: 14, fontWeight: '600', marginBottom: 4},
  cardNote: {color: MUTED, fontSize: 13, lineHeight: 20},

  buttons: {flexDirection: 'row', gap: 10, marginTop: 18},
  primary: {
    paddingHorizontal: 16,
    paddingVertical: 9,
    borderRadius: 7,
    backgroundColor: EMBER,
  },
  primaryLabel: {color: BONE, fontSize: 13, fontWeight: '600'},
  secondary: {
    paddingHorizontal: 16,
    paddingVertical: 9,
    borderRadius: 7,
    borderWidth: 1,
    borderColor: LINE,
    backgroundColor: INK,
  },
  secondaryLabel: {color: '#c6cbd2', fontSize: 13, fontWeight: '600'},
  pressed: {opacity: 0.7},

  status: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 10,
    height: 30,
    paddingHorizontal: 16,
    backgroundColor: PANEL,
    borderTopWidth: 1,
    borderTopColor: LINE,
  },
  statusDot: {width: 7, height: 7, borderRadius: 4, backgroundColor: '#4bbf8a'},
  statusText: {color: '#6c727c', fontSize: 11},
  spacer: {flex: 1},
});

AppRegistry.registerComponent('BasaltShowcase', () => App);
