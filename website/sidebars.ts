import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

/**
 * The user-facing half, for people building an app. The repository's own docs
 * are a second instance with its own sidebar; see sidebarsContributing.ts.
 */
const sidebars: SidebarsConfig = {
  docs: [
    'intro',
    'getting-started',
    'navigation',
    {
      type: 'category',
      label: 'Reference',
      collapsed: false,
      items: ['reference/cli', 'reference/app-json', 'reference/api'],
    },
  ],
};

export default sidebars;
