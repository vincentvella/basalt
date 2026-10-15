import type {ReactNode} from 'react';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import useBaseUrl from '@docusaurus/useBaseUrl';
import Layout from '@theme/Layout';
import Heading from '@theme/Heading';
import CodeBlock from '@theme/CodeBlock';
import ThemedImage from '@theme/ThemedImage';
import HomepageFeatures from '@site/src/components/HomepageFeatures';
import VersionMatrix from '@site/src/components/VersionMatrix';
import support from '@site/src/data/support.json';

import styles from './index.module.css';

/**
 * The landing page.
 *
 * It said the tagline, showed one command and stopped, which left a visitor to
 * work out what this is from three cards. So it now answers the three questions
 * somebody arriving actually has, in that order: what do I write, what runs it,
 * and what does it give me that React Native does not.
 *
 * The numbers come from `src/data/support.json`, which
 * `scripts/check_support.js` writes beside the support page and CI checks for
 * staleness. A figure typed into this file is a figure that goes stale, and
 * going stale is the thing this project's documentation is most careful about.
 */

// Which text engine lays out a paragraph on each host. Not in support.json,
// which is about props, and the one fact about a host that most explains why
// layout behaves the way it does here.
const TEXT_ENGINES: Record<string, string> = {
  linux: 'Pango',
  macos: 'Core Text',
  windows: 'DirectWrite',
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

type Api = {
  title: string;
  href: string;
  body: string;
};

// What a desktop has and a phone does not, which is the whole of what
// `basalt-core` adds. Ordered by how early an app needs it.
const APIS: Api[] = [
  {
    title: 'Windows',
    href: '/docs/reference/windows',
    body: 'Set the size and position, go full screen, open a second window, and get asked before one closes so you can say "are you sure".',
  },
  {
    title: 'Menus',
    href: '/docs/reference/menus',
    body: 'A menu bar written as components, with the standard items each platform already has, and right-click menus with checkmarks and submenus.',
  },
  {
    title: 'File dialogs',
    href: '/docs/reference/dialogs',
    body: 'The native open, save and choose-a-folder dialogs, with filters.',
  },
  {
    title: 'Drag and drop',
    href: '/docs/reference/drag-and-drop',
    body: 'Take a file dropped on your app from the file manager, and let a row be dragged out of it into something else.',
  },
  {
    title: 'Keyboard shortcuts',
    href: '/docs/reference/keyboard',
    body: 'Bind shortcuts to anything you draw. The keys you do not take still reach the menu and whatever the person is typing in.',
  },
  {
    title: 'The title bar',
    href: '/docs/reference/title-bar',
    body: 'Its title and its colours, or no title bar at all and a header you drew yourself.',
  },
  {
    title: 'Displays',
    href: '/docs/reference/windows#usedisplays',
    body: 'Every screen the desktop has, how big it is and how sharp, which is what placing a window needs to know.',
  },
  {
    title: 'Notifications',
    href: '/docs/reference/notifications',
    body: 'Real desktop notifications, through the expo-notifications API you already know.',
  },
  {
    title: 'Subprocesses',
    href: '/docs/reference/subprocess',
    body: 'Run a command, watch its output, hear when it exits. What a front end for something like ffmpeg is made of.',
  },
];

function HomepageHeader() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <header className={styles.hero}>
      <div className="container">
        <ThemedImage
          className={styles.heroLogo}
          alt=""
          sources={{
            light: useBaseUrl('/img/logo.svg'),
            dark: useBaseUrl('/img/logo-inverse.svg'),
          }}
        />
        <Heading as="h1" className={styles.heroTitle}>
          {siteConfig.title}
        </Heading>
        <p className={styles.heroTagline}>{siteConfig.tagline}</p>
        <p className={styles.heroLede}>
          Build desktop apps for Mac, Windows and Linux with React Native. The
          same components you already write, running as real native windows,
          menus and controls, with no browser packed inside.
        </p>
        <div className={styles.heroCode}>
          <CodeBlock language="bash">{`npx basalt init\nnpm run macos -- --build`}</CodeBlock>
        </div>
        <div className={styles.heroButtons}>
          <Link
            className="button button--primary button--lg"
            to="/docs/getting-started">
            Get started
          </Link>
          <Link className="button button--secondary button--lg" to="/docs/intro">
            What Basalt is
          </Link>
        </div>
        {/* Said on the front page rather than discovered after an install.
            Below 1.0, so the API can change in any release. */}
        <p className={styles.heroStatus}>
          Early days. On npm at <code>0.2.x</code>, and below 1.0 the API can
          still change in any release.
        </p>
      </div>
    </header>
  );
}

// What an app is actually made of here, which is the question three cards
// cannot answer: components from react-native, and the desktop's own things
// from basalt-core. Nothing in the sample is a Basalt component.
function WhatYouWrite() {
  return (
    <section className={styles.section}>
      <div className="container">
        <div className="row">
          <div className="col col--5">
            <Heading as="h2" className={styles.sectionTitle}>
              You write React Native
            </Heading>
            <p className={styles.sectionBody}>
              Your components come from <code>react-native</code>, exactly as
              they do on iOS and Android. Nothing to relearn, no HTML and no
              CSS: what you have already written behaves the way it already
              does.
            </p>
            <p className={styles.sectionBody}>
              <code>basalt-core</code> is the rest. A window somebody can move
              and resize. A menu bar. A file picker. The things a desktop has
              and a phone does not.
            </p>
            <p className={styles.sectionBody}>
              <Link to="/docs/reference/api">The APIs</Link> are the whole list,
              and it is short on purpose.
            </p>
          </div>
          <div className="col col--7">
            <CodeBlock language="jsx" title="App.tsx">
              {APP}
            </CodeBlock>
          </div>
        </div>
      </div>
    </section>
  );
}

// One host per desktop, with the number each one has earned. `yes` out of
// `ours` is what that host wrote itself; the rest of the rows are ReactCommon's
// or deliberately not done, and the support page draws the whole bar.
function Hosts() {
  return (
    <section className={`${styles.section} ${styles.sectionTinted}`}>
      <div className="container">
        <Heading as="h2" className={styles.sectionTitle}>
          Three desktops, one app
        </Heading>
        <p className={styles.sectionLede}>
          Each desktop has its own version of Basalt, built against the toolkit
          that desktop actually uses, so an app looks and behaves like it
          belongs there. How far along each one is, measured against React
          Native {support.reactNative}:
        </p>
        <div className="row">
          {support.hosts.map((host) => (
            <div className="col col--4" key={host.key}>
              <div className={styles.hostCard}>
                <Heading as="h3" className={styles.hostName}>
                  {host.label}
                </Heading>
                <p className={styles.hostToolkit}>{host.toolkit}</p>
                <dl className={styles.hostFacts}>
                  <dt>Text drawn by</dt>
                  <dd>{TEXT_ENGINES[host.key]}</dd>
                  <dt>Props supported</dt>
                  <dd>
                    {host.yes} of {host.ours}, {host.percent}%
                  </dd>
                  <dt>Partly</dt>
                  <dd>{host.partial}</dd>
                  <dt>Missing</dt>
                  <dd>{host.no}</dd>
                </dl>
              </div>
            </div>
          ))}
        </div>
        <p className={styles.sectionFoot}>
          Counted by a script rather than claimed. Every prop React Native has
          gets a row, a column per desktop, and a reason wherever one is
          missing.{' '}
          <Link to="/docs/support">The table is the honest version of this</Link>
          .
        </p>
      </div>
    </section>
  );
}

// The part of the project that is not React Native and could not be: what a
// desktop has that a phone does not.
function DesktopApis() {
  return (
    <section className={styles.section}>
      <div className="container">
        <Heading as="h2" className={styles.sectionTitle}>
          And everything a phone never needed
        </Heading>
        <p className={styles.sectionLede}>
          React Native has no API for any of this, because a phone has none of
          it. Each one works on all three desktops, and in the few places where
          a desktop genuinely cannot do something, your app can ask instead of
          guessing.
        </p>
        <div className={styles.apiGrid}>
          {APIS.map((api) => (
            <Link className={styles.apiCard} to={api.href} key={api.title}>
              <span className={styles.apiTitle}>{api.title}</span>
              <span className={styles.apiBody}>{api.body}</span>
            </Link>
          ))}
        </div>
        <p className={styles.sectionFoot}>
          The libraries you already use come too:{' '}
          <Link to="/docs/libraries">react-navigation, Skia, Reanimated and
          gesture-handler</Link>{' '}
          all run, and your app can bring native code of its own.
        </p>
      </div>
    </section>
  );
}

function Differs() {
  return (
    <section className={`${styles.section} ${styles.sectionTinted}`}>
      <div className="container">
        <Heading as="h2" className={styles.sectionTitle}>
          How this differs
        </Heading>
        <div className="row">
          <div className="col col--4">
            <Heading as="h3" className={styles.compareName}>
              Electron and Tauri
            </Heading>
            <p className={styles.sectionBody}>
              They put a web page in a window: you write web UI and a browser
              draws it. Basalt gives you React Native components drawn as real
              native controls, with no HTML and no CSS.
            </p>
          </div>
          <div className="col col--4">
            <Heading as="h3" className={styles.compareName}>
              react-native-windows and -macos
            </Heading>
            <p className={styles.sectionBody}>
              Separate projects on separate release schedules, one desktop
              each. Basalt is one project for all three, and it uses the same
              platform names, so a library that ships a{' '}
              <code>Button.macos.js</code> already works here.
            </p>
          </div>
          <div className="col col--4">
            <Heading as="h3" className={styles.compareName}>
              Expo
            </Heading>
            <p className={styles.sectionBody}>
              Not an alternative, the common case. A stock Expo app runs, and{' '}
              <code>basalt init</code> sets up Metro for you when it finds one.
            </p>
          </div>
        </div>
      </div>
    </section>
  );
}

// Being specific about the limits is more useful than a maturity label, and it
// belongs where somebody decides whether to try this rather than two pages in.
function Expect() {
  return (
    <section className={styles.section}>
      <div className="container">
        <Heading as="h2" className={styles.sectionTitle}>
          What to expect
        </Heading>
        <Heading as="h3" className={styles.subTitle}>
          Which React Native
        </Heading>
        <p className={styles.sectionBody}>
          A version is on this list once it has been built against and both
          test suites have run. One that has not is refused by the build rather
          than quietly allowed, so there is no range to guess at.
        </p>
        <VersionMatrix />
        <ul className={styles.expectList}>
          <li>
            <strong>The first build takes tens of minutes</strong>, because it
            compiles React Native's C++ from source. Every build after that is
            quick. <code>npx basalt-core doctor</code> checks you have what it
            needs, in seconds, before you spend that.
          </li>
          <li>
            <strong>The three desktops are not equally finished.</strong> Linux
            and macOS are ahead of Windows, and{' '}
            <Link to="/docs/support">the support table</Link> says exactly
            where.
          </li>
          <li>
            <strong>Nobody has yet taken an arbitrary native library and
            ported it from scratch.</strong> The popular ones above run, but
            what a new one costs is still unknown, and it is the biggest open
            question here.
          </li>
        </ul>
        <div className={styles.endButtons}>
          <Link
            className="button button--primary button--lg"
            to="/docs/getting-started">
            Get started
          </Link>
          <Link
            className="button button--secondary button--lg"
            to="https://github.com/vincentvella/basalt">
            GitHub
          </Link>
        </div>
      </div>
    </section>
  );
}

export default function Home(): ReactNode {
  return (
    <Layout
      title="React Native on the desktop"
      description="Basalt runs React Native apps on Linux, macOS and Windows, on React Native's own C++ core rather than on a browser engine.">
      <HomepageHeader />
      <main>
        <HomepageFeatures />
        <WhatYouWrite />
        <Hosts />
        <DesktopApis />
        <Differs />
        <Expect />
      </main>
    </Layout>
  );
}
