import type {ReactNode} from 'react';
import Heading from '@theme/Heading';
import styles from './styles.module.css';

type FeatureItem = {
  title: string;
  description: ReactNode;
};

/**
 * Three claims, each of which is true of Basalt today. The scaffold's cards
 * described Docusaurus; a card that praises the tool the site is built with
 * tells a visitor nothing about the thing the site is about.
 */
const FeatureList: FeatureItem[] = [
  {
    title: 'Native, not a browser',
    description: (
      <>
        A <code>&lt;View&gt;</code> is a real widget: GTK4 on Linux, AppKit on
        macOS, Win32 with Direct2D on Windows. Text is laid out by Pango, Core
        Text or DirectWrite. The app ships as one binary with no bundled
        browser engine.
      </>
    ),
  },
  {
    title: "React Native's own core",
    description: (
      <>
        The same C++ that runs on iOS and Android (Fabric, Yoga, the shadow
        tree) with a host per desktop written against its toolkit. Your
        components, props and layout behave the way they already do.
      </>
    ),
  },
  {
    title: 'Configured, not wired up',
    description: (
      <>
        <code>app.json</code> declares which desktops an app targets and each
        package declares what it needs natively. There is no per-platform
        project to hand-edit and keep in sync.
      </>
    ),
  },
];

function Feature({title, description}: FeatureItem) {
  return (
    <div className="col col--4">
      <div className={styles.card}>
        <Heading as="h3" className={styles.cardTitle}>
          {title}
        </Heading>
        <p className={styles.cardBody}>{description}</p>
      </div>
    </div>
  );
}

export default function HomepageFeatures(): ReactNode {
  return (
    <section className={styles.features}>
      <div className="container">
        <div className="row">
          {FeatureList.map((props) => (
            <Feature key={props.title} {...props} />
          ))}
        </div>
      </div>
    </section>
  );
}
