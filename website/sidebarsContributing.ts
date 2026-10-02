import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

/**
 * The repository's own docs, read in place from ../docs. Ordered by what a
 * newcomer needs first rather than alphabetically, which is the order the
 * filesystem would have given.
 */
const sidebars: SidebarsConfig = {
  contributing: ['ARCHITECTURE', 'DECISIONS', 'TESTING', 'PORTING'],
};

export default sidebars;
