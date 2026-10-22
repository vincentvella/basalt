import { DynamicCodeBlock } from "fumadocs-ui/components/dynamic-codeblock";
import Link from "next/link";
import type { ReactNode } from "react";
import {
  Body,
  Button,
  Foot,
  Section,
  SectionLede,
  SectionTitle,
} from "@/components/home";
import VersionMatrix from "@/components/VersionMatrix";
import support from "@/lib/support.json";

/**
 * The landing page.
 *
 * It answers the three questions somebody arriving has, in that order: what do
 * I write, what runs it, and what does it give me that React Native does not.
 *
 * The numbers come from `lib/support.json`, which `scripts/check_support.js`
 * writes beside the support page and CI checks for staleness. A figure typed
 * into this file is a figure that goes stale, and going stale is the thing
 * this project's documentation is most careful about.
 */

// Which text engine lays out a paragraph on each host. Not in support.json,
// which is about props, and the one fact about a host that most explains why
// layout behaves the way it does here.
const TEXT_ENGINES: Record<string, string> = {
  linux: "Pango",
  macos: "Core Text",
  windows: "DirectWrite",
};

// Short lines on purpose: this sits in a half-width column, and a landing page
// whose one code sample scrolls sideways has failed at the only job it had.
const APP = `import {View, Text, Pressable} from 'react-native';
import {Menu, useWindow} from 'basalt-core';

export default function App() {
  const {bounds, center} = useWindow();

  return (
    <View style={{flex: 1, padding: 24}}>
      <Menu>
        <Menu.Submenu label="File">
          <Menu.Item label="Open…" onClick={open} />
          <Menu.Item role="close" />
        </Menu.Submenu>
      </Menu>

      <Text>
        {bounds.width} × {bounds.height}
      </Text>

      <Pressable onPress={center}>
        <Text>Centre the window</Text>
      </Pressable>
    </View>
  );
}`;

type Api = { title: string; href: string; body: string };

// What a desktop has and a phone does not, which is the whole of what
// `basalt-core` adds. Ordered by how early an app needs it.
const APIS: Api[] = [
  {
    title: "Windows",
    href: "/docs/reference/windows",
    body: 'Set the size and position, go full screen, open a second window, and get asked before one closes so you can say "are you sure".',
  },
  {
    title: "Menus",
    href: "/docs/reference/menus",
    body: "A menu bar written as components, with the standard items each platform already has, and right-click menus with checkmarks and submenus.",
  },
  {
    title: "File dialogs",
    href: "/docs/reference/dialogs",
    body: "The native open, save and choose-a-folder dialogs, with filters.",
  },
  {
    title: "Drag and drop",
    href: "/docs/reference/drag-and-drop",
    body: "Take a file dropped on your app from the file manager, and let a row be dragged out of it into something else.",
  },
  {
    title: "Keyboard shortcuts",
    href: "/docs/reference/keyboard",
    body: "Bind shortcuts to anything you draw. The keys you do not take still reach the menu and whatever the person is typing in.",
  },
  {
    title: "The title bar",
    href: "/docs/reference/title-bar",
    body: "Its title and its colours, or no title bar at all and a header you drew yourself.",
  },
  {
    title: "Displays",
    href: "/docs/reference/windows#usedisplays",
    body: "Every screen the desktop has, how big it is and how sharp, which is what placing a window needs to know.",
  },
  {
    title: "Notifications",
    href: "/docs/reference/notifications",
    body: "Real desktop notifications, through the expo-notifications API you already know.",
  },
  {
    title: "Subprocesses",
    href: "/docs/reference/subprocess",
    body: "Run a command, watch its output, hear when it exits. What a front end for something like ffmpeg is made of.",
  },
];

const FEATURES: { title: string; body: ReactNode }[] = [
  {
    title: "Native, not a browser",
    body: (
      <>
        Every view is a real control, drawn by the desktop itself, and text is
        laid out by the system the way every other app on that machine does it.
        Nothing is a web page in disguise, and there is no browser shipped
        alongside your app.
      </>
    ),
  },
  {
    title: "The React Native you know",
    body: (
      <>
        Not a rewrite that behaves almost the same. The same engine that runs
        your app on iOS and Android runs it here, so your components, your props
        and your layout do what they already do.
      </>
    ),
  },
  {
    title: "One command to add",
    body: (
      <>
        <code>npx basalt init</code> adds a desktop to the app you already have,
        and <code>app.json</code> says which ones you want. There is no second
        project to maintain beside your own.
      </>
    ),
  },
];

function Hero() {
  return (
    <header className="border-b border-fd-border bg-linear-to-b from-fd-card to-fd-background px-4 py-20 text-center">
      <div className="mx-auto w-full max-w-4xl">
        {/* The mascot, above the name rather than beside it: the page is
            centred and a mark to the left of a centred title reads as a
            mistake. */}
        <img
          src="/img/logo.svg"
          alt=""
          width={88}
          height={88}
          className="mx-auto mb-4 dark:hidden"
        />
        <img
          src="/img/logo-inverse.svg"
          alt=""
          width={88}
          height={88}
          className="mx-auto mb-4 hidden dark:block"
        />
        <h1 className="mb-2 text-6xl font-semibold tracking-tight">Basalt</h1>
        <p className="mb-8 text-xl text-fd-muted-foreground">
          React Native on the desktop: Linux, macOS and Windows
        </p>
        <p className="mx-auto mb-8 max-w-2xl text-fd-muted-foreground">
          Build desktop apps for Mac, Windows and Linux with React Native. The
          same components you already write, running as real native windows,
          menus and controls, with no browser packed inside.
        </p>
        <div className="mx-auto mb-8 max-w-md text-left">
          <DynamicCodeBlock
            lang="bash"
            code={"npx basalt init\nnpm run macos -- --build"}
          />
        </div>
        <div className="flex flex-wrap justify-center gap-4">
          <Button href="/docs/getting-started" primary>
            Get started
          </Button>
          <Button href="/docs/intro">What Basalt is</Button>
        </div>
        {/* Said on the front page rather than discovered after an install.
            Below 1.0, so the API can change in any release. */}
        <p className="mx-auto mt-10 max-w-xl text-sm text-fd-muted-foreground">
          Early days. On npm at <code>0.2.x</code>, and below 1.0 the API can
          still change in any release.
        </p>
      </div>
    </header>
  );
}

function Features() {
  return (
    <Section>
      <div className="grid gap-4 md:grid-cols-3">
        {FEATURES.map((feature) => (
          <div
            key={feature.title}
            className="rounded-lg border border-fd-border bg-fd-card p-7"
          >
            <h3 className="mb-3 text-lg font-semibold">{feature.title}</h3>
            <p className="text-fd-muted-foreground">{feature.body}</p>
          </div>
        ))}
      </div>
    </Section>
  );
}

// What an app is actually made of here, which is the question three cards
// cannot answer: components from react-native, and the desktop's own things
// from basalt-core. Nothing in the sample is a Basalt component.
function WhatYouWrite() {
  return (
    <Section>
      <div className="grid gap-10 lg:grid-cols-12">
        <div className="lg:col-span-5">
          <SectionTitle>You write React Native</SectionTitle>
          <Body>
            Your components come from <code>react-native</code>, exactly as they
            do on iOS and Android. Nothing to relearn, no HTML and no CSS: what
            you have already written behaves the way it already does.
          </Body>
          <Body>
            <code>basalt-core</code> is the rest. A window somebody can move and
            resize. A menu bar. A file picker. The things a desktop has and a
            phone does not.
          </Body>
          <Body>
            <Link href="/docs/reference/api">The APIs</Link> are the whole list,
            and it is short on purpose.
          </Body>
        </div>
        <div className="lg:col-span-7">
          <DynamicCodeBlock lang="tsx" code={APP} />
        </div>
      </div>
    </Section>
  );
}

// One host per desktop, with the number each one has earned. `yes` out of
// `ours` is what that host wrote itself; the rest of the rows are ReactCommon's
// or deliberately not done, and the support page draws the whole bar.
function Hosts() {
  return (
    <Section tinted>
      <SectionTitle>Three desktops, one app</SectionTitle>
      <SectionLede>
        Each desktop has its own version of Basalt, built against the toolkit
        that desktop actually uses, so an app looks and behaves like it belongs
        there. How far along each one is, measured against React Native{" "}
        {support.reactNative}:
      </SectionLede>
      <div className="grid gap-4 md:grid-cols-3">
        {support.hosts.map((host) => (
          <div
            key={host.key}
            className="rounded-lg border border-fd-border bg-fd-background p-6"
          >
            <h3 className="text-xl font-semibold">{host.label}</h3>
            <p className="mb-5 font-mono text-sm text-fd-primary">
              {host.toolkit}
            </p>
            <dl className="grid grid-cols-[auto_1fr] gap-x-4 gap-y-2 text-sm">
              <dt className="text-fd-muted-foreground">Text drawn by</dt>
              <dd className="text-right tabular-nums">
                {TEXT_ENGINES[host.key]}
              </dd>
              <dt className="text-fd-muted-foreground">Props supported</dt>
              <dd className="text-right tabular-nums">
                {host.yes} of {host.ours}, {host.percent}%
              </dd>
              <dt className="text-fd-muted-foreground">Partly</dt>
              <dd className="text-right tabular-nums">{host.partial}</dd>
              <dt className="text-fd-muted-foreground">Missing</dt>
              <dd className="text-right tabular-nums">{host.no}</dd>
            </dl>
          </div>
        ))}
      </div>
      <Foot>
        Counted by a script rather than claimed. Every prop React Native has
        gets a row, a column per desktop, and a reason wherever one is missing.{" "}
        <Link href="/docs/support">
          The table is the honest version of this
        </Link>
        .
      </Foot>
    </Section>
  );
}

// The part of the project that is not React Native and could not be: what a
// desktop has that a phone does not.
function DesktopApis() {
  return (
    <Section>
      <SectionTitle>And everything a phone never needed</SectionTitle>
      <SectionLede>
        React Native has no API for any of this, because a phone has none of it.
        Each one works on all three desktops, and in the few places where a
        desktop genuinely cannot do something, your app can ask instead of
        guessing.
      </SectionLede>
      <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
        {APIS.map((api) => (
          <Link
            key={api.title}
            href={api.href}
            className="flex flex-col gap-2 rounded-lg border border-fd-border bg-fd-card p-6 transition-colors hover:border-fd-primary"
          >
            <span className="font-semibold">{api.title}</span>
            <span className="text-sm text-fd-muted-foreground">{api.body}</span>
          </Link>
        ))}
      </div>
      <Foot>
        The libraries you already use come too:{" "}
        <Link href="/docs/libraries">
          react-navigation, Skia, Reanimated and gesture-handler
        </Link>{" "}
        all run, and your app can bring native code of its own.
      </Foot>
    </Section>
  );
}

function Differs() {
  return (
    <Section tinted>
      <SectionTitle>How this differs</SectionTitle>
      <div className="grid gap-8 md:grid-cols-3">
        <div>
          <h3 className="mb-2 font-semibold">Electron and Tauri</h3>
          <Body>
            They put a web page in a window: you write web UI and a browser
            draws it. Basalt gives you React Native components drawn as real
            native controls, with no HTML and no CSS.
          </Body>
        </div>
        <div>
          <h3 className="mb-2 font-semibold">
            react-native-windows and -macos
          </h3>
          <Body>
            Separate projects on separate release schedules, one desktop each.
            Basalt is one project for all three, and it uses the same platform
            names, so a library that ships a <code>Button.macos.js</code>{" "}
            already works here.
          </Body>
        </div>
        <div>
          <h3 className="mb-2 font-semibold">Expo</h3>
          <Body>
            Not an alternative, the common case. A stock Expo app runs, and{" "}
            <code>basalt init</code> sets up Metro for you when it finds one.
          </Body>
        </div>
      </div>
    </Section>
  );
}

// Being specific about the limits is more useful than a maturity label, and it
// belongs where somebody decides whether to try this rather than two pages in.
function Expect() {
  return (
    <Section>
      <SectionTitle>What to expect</SectionTitle>
      <h3 className="mb-3 text-xl font-semibold">Which React Native</h3>
      <Body>
        A version is on this list once it has been built against and both test
        suites have run. One that has not is refused by the build rather than
        quietly allowed, so there is no range to guess at.
      </Body>
      <VersionMatrix />
      <ul className="mb-10 max-w-4xl list-disc space-y-4 pl-5 text-fd-muted-foreground">
        <li>
          <strong>The first build takes tens of minutes</strong>, because it
          compiles React Native's C++ from source. Every build after that is
          quick. <code>npx basalt-core doctor</code> checks you have what it
          needs, in seconds, before you spend that.
        </li>
        <li>
          <strong>The three desktops are not equally finished.</strong> Linux
          and macOS are ahead of Windows, and{" "}
          <Link href="/docs/support">the support table</Link> says exactly
          where.
        </li>
        <li>
          <strong>
            Nobody has yet taken an arbitrary native library and ported it from
            scratch.
          </strong>{" "}
          The popular ones above run, but what a new one costs is still unknown,
          and it is the biggest open question here.
        </li>
      </ul>
      <div className="flex flex-wrap gap-4">
        <Button href="/docs/getting-started" primary>
          Get started
        </Button>
        <Button href="https://github.com/vincentvella/basalt">GitHub</Button>
      </div>
    </Section>
  );
}

export default function Home() {
  return (
    <main className="flex flex-1 flex-col">
      <Hero />
      <Features />
      <WhatYouWrite />
      <Hosts />
      <DesktopApis />
      <Differs />
      <Expect />
    </main>
  );
}
