import type {ReactNode} from 'react';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import useBaseUrl from '@docusaurus/useBaseUrl';
import Layout from '@theme/Layout';
import Heading from '@theme/Heading';
import CodeBlock from '@theme/CodeBlock';
import ThemedImage from '@theme/ThemedImage';
import HomepageFeatures from '@site/src/components/HomepageFeatures';
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
    body: 'Size, position, full screen, live bounds, a second window, and being asked before one closes.',
  },
  {
    title: 'Menus',
    href: '/docs/reference/menus',
    body: 'A declarative menu bar with the platform roles that make Cmd-C work, and a context menu with checkmarks and submenus.',
  },
  {
    title: 'File dialogs',
    href: '/docs/reference/dialogs',
    body: 'The native open, save and choose-a-folder dialogs, with filters.',
  },
  {
    title: 'Drag and drop',
    href: '/docs/reference/drag-and-drop',
    body: 'Accept a file dropped from the file manager, and drag a row out into another application.',
  },
  {
    title: 'Keyboard shortcuts',
    href: '/docs/reference/keyboard',
    body: 'A view declares the combinations it handles, so every other key goes on to the menu and the text field.',
  },
  {
    title: 'The title bar',
    href: '/docs/reference/title-bar',
    body: 'Its title and its colours, or no title bar at all and a header the app drew itself.',
  },
  {
    title: 'Displays',
    href: '/docs/reference/windows#usedisplays',
    body: 'Every screen, its work area and its scale factor, which is the question Dimensions does not answer.',
  },
  {
    title: 'Notifications',
    href: '/docs/reference/notifications',
    body: "expo-notifications' own contract, implemented natively on each desktop.",
  },
  {
    title: 'Subprocesses',
    href: '/docs/reference/subprocess',
    body: 'Run a command, watch its output, hear about its exit: the thing a front end for ffmpeg is made of.',
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
          Your React Native app, built as a native desktop application. A{' '}
          <code>&lt;View&gt;</code> is a real GTK4, AppKit or Win32 widget, the
          C++ underneath is React Native's own, and the result is one binary
          with no browser engine in it.
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
          Early. Published on npm at <code>0.2.x</code>, which under semver
          promises nothing about stability and is meant literally.
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
              Components come from <code>react-native</code>, exactly as they do
              on iOS and Android. There is no Basalt <code>&lt;View&gt;</code>{' '}
              to import and no CSS: your components, props and layout behave the
              way they already do, because what lays them out is the same Yoga
              and the same shadow tree.
            </p>
            <p className={styles.sectionBody}>
              <code>basalt-core</code> is only the things a phone has no
              equivalent of. A window has a size you can set and a person can
              drag. There is a menu bar, and on macOS it is also what makes
              Cmd-C reach a text field.
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
          Three hosts, one shared core
        </Heading>
        <p className={styles.sectionLede}>
          Each desktop gets a host written against its own toolkit, over the
          same C++. React Native {support.reactNative} is what these numbers are
          measured against.
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
                  <dt>Text</dt>
                  <dd>{TEXT_ENGINES[host.key]}</dd>
                  <dt>Props written</dt>
                  <dd>
                    {host.yes} of {host.ours}, {host.percent}%
                  </dd>
                  <dt>Partly done</dt>
                  <dd>{host.partial}</dd>
                  <dt>Not done</dt>
                  <dd>{host.no}</dd>
                </dl>
              </div>
            </div>
          ))}
        </div>
        <p className={styles.sectionFoot}>
          Counted by a script rather than claimed: every field React Native's
          C++ parses has a row, a column per desktop, and a reason wherever a
          host does not read one.{' '}
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
          And the desktop itself
        </Heading>
        <p className={styles.sectionLede}>
          React Native has no API for any of this, because a phone has no such
          thing. Each one is guarded, so the same code runs on all three, and
          where a desktop genuinely cannot do a thing it is answerable at
          runtime rather than documented and hoped for.
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
          all run, and an app can contribute its own C++ without publishing a
          package.
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
              A web view: you write web UI and a browser lays it out. Basalt
              gives you the React Native component model over native widgets.
              There is no DOM and no CSS.
            </p>
          </div>
          <div className="col col--4">
            <Heading as="h3" className={styles.compareName}>
              react-native-windows and -macos
            </Heading>
            <p className={styles.sectionBody}>
              Separate forks with separate cadences. Basalt is one project
              covering three desktops, and uses their platform names, so a
              library shipping <code>Button.macos.js</code> resolves here
              without knowing Basalt exists.
            </p>
          </div>
          <div className="col col--4">
            <Heading as="h3" className={styles.compareName}>
              Expo
            </Heading>
            <p className={styles.sectionBody}>
              Not an alternative, the common case. A stock Expo app runs, and{' '}
              <code>init</code> writes the Metro config on{' '}
              <code>expo/metro-config</code> when it finds one.
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
        <ul className={styles.expectList}>
          <li>
            <strong>React Native 0.86 and 0.87</strong> are supported, and 0.87
            is what CI pins. On 0.86, which Expo SDK 57 installs, there is no
            Fast Refresh; that was root-caused and fixed upstream in 0.87.
          </li>
          <li>
            <strong>The first build compiles Hermes and React Native's C++</strong>{' '}
            from source and takes tens of minutes. Later builds do not.{' '}
            <code>npx basalt-core doctor</code> checks your toolchain in seconds
            before you spend that.
          </li>
          <li>
            <strong>The three desktops are not equally finished.</strong> Linux
            and macOS are ahead of Windows, and{' '}
            <Link to="/docs/support">the support table</Link> says exactly
            where.
          </li>
          <li>
            <strong>No third-party native module has been ported end to end</strong>{' '}
            through codegen. The seam is proven and the libraries above run, but
            what porting an arbitrary module costs is still a guess. It is the
            largest unknown here.
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
