import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

/** The user-facing half. Mostly stubs for now; the content is the next job. */
const sidebars: SidebarsConfig = {
  docs: [
    'intro',
    'getting-started',
    {
      type: 'category',
      label: 'Reference',
      items: ['reference/cli', 'reference/app-json'],
    },
  ],
};

export default sidebars;
