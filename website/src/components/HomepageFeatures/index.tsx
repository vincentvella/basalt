import type {ReactNode} from 'react';
import Heading from '@theme/Heading';
import styles from './styles.module.css';

type FeatureItem = {
  title: string;
  description: ReactNode;
};

/**
 * Three claims, each of which is true of Basalt today, and each said without
 * naming a toolkit or a piece of React Native's C++. The page below goes into
 * how it works; this is the part somebody reads before deciding to care.
 *
 * The scaffold's cards described Docusaurus. A card that praises the tool the
 * site is built with tells a visitor nothing about the thing the site is
 * about.
 */
const FeatureList: FeatureItem[] = [
  {
    title: 'Native, not a browser',
    description: (
      <>
        Every view is a real control, drawn by the desktop itself, and text is
        laid out by the system the way every other app on that machine does
        it. Nothing is a web page in disguise, and there is no browser shipped
        alongside your app.
      </>
    ),
  },
  {
    title: 'The React Native you know',
    description: (
      <>
        Not a rewrite that behaves almost the same. The same engine that runs
        your app on iOS and Android runs it here, so your components, your
        props and your layout do what they already do.
      </>
    ),
  },
  {
    title: 'One command to add',
    description: (
      <>
        <code>npx basalt init</code> adds a desktop to the app you already
        have, and <code>app.json</code> says which ones you want. There is no
        second project to maintain beside your own.
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
