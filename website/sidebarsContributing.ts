import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

/**
 * The repository's own documentation, read in place from `../docs`.
 *
 * Grouped rather than flat. Four files is not many, but two of them are very
 * long, and a flat list of four gave no sense of which to open for what.
 * ci-performance.md, BACKLOG.md, HANDOFF.md and backlog/ are excluded in
 * docusaurus.config.ts and so are absent here.
 */
const sidebars: SidebarsConfig = {
  contributing: [
    {
      type: 'category',
      label: 'How it works',
      collapsed: false,
      items: ['ARCHITECTURE', 'DECISIONS'],
    },
    {
      type: 'category',
      label: 'Working on it',
      collapsed: false,
      items: ['TESTING', 'PORTING'],
    },
  ],
};

export default sidebars;
