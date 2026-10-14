import type {ReactNode} from 'react';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import Heading from '@theme/Heading';
import CodeBlock from '@theme/CodeBlock';
import HomepageFeatures from '@site/src/components/HomepageFeatures';

import styles from './index.module.css';

function HomepageHeader() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <header className={styles.hero}>
      <div className="container">
        <Heading as="h1" className={styles.heroTitle}>
          {siteConfig.title}
        </Heading>
        <p className={styles.heroTagline}>{siteConfig.tagline}</p>
        <div className={styles.heroCode}>
          <CodeBlock language="bash">npx basalt init</CodeBlock>
        </div>
        <div className={styles.heroButtons}>
          <Link className="button button--primary button--lg" to="/docs/intro">
            What Basalt is
          </Link>
          <Link
            className="button button--secondary button--lg"
            to="/contributing/ARCHITECTURE">
            How it works
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

export default function Home(): ReactNode {
  return (
    <Layout
      title="React Native on the desktop"
      description="Basalt runs React Native apps on Linux, macOS and Windows, on React Native's own C++ core rather than on a browser engine.">
      <HomepageHeader />
      <main>
        <HomepageFeatures />
      </main>
    </Layout>
  );
}
