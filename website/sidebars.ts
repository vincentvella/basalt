import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

/**
 * The user-facing half, for people building an app. The repository's own docs
 * are a second instance with its own sidebar; see sidebarsContributing.ts.
 *
 * The reference is a page per area rather than one long APIs page. It was one
 * page, and the page documented five of the fifteen things `basalt-core`
 * exports, which is the failure a single page invites: a window, a menu and a
 * drop target are each somebody's whole afternoon, and none of them is findable
 * by name in a sidebar entry called "APIs".
 */
const sidebars: SidebarsConfig = {
  docs: [
    'intro',
    'getting-started',
    'navigation',
    'libraries',
    'support',
    {
      type: 'category',
      label: 'Reference',
      collapsed: false,
      items: [
        'reference/cli',
        'reference/app-json',
        'reference/api',
        'reference/windows',
        'reference/title-bar',
        'reference/menus',
        'reference/dialogs',
        'reference/drag-and-drop',
        'reference/keyboard',
        'reference/subprocess',
        'reference/notifications',
      ],
    },
  ],
};

export default sidebars;
