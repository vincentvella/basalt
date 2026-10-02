import type * as Preset from '@docusaurus/preset-classic';
import type {Config} from '@docusaurus/types';
import {themes as prismThemes} from 'prism-react-renderer';

/**
 * Two docs instances, which is the shape of this site.
 *
 * `website/docs` is the user-facing half, for people building an app. `../docs`
 * is the repository's own, for people working on basalt -- and it is read in
 * place rather than copied. Ninety comments across the C++ and TypeScript point
 * at `docs/DECISIONS.md` and the rest by that path, so moving them to make a
 * website would have broken every one. A website is not a good enough reason
 * for the source to lie about where its reasons live.
 *
 * BACKLOG, HANDOFF and ci-performance stay off the site. They are notes to whoever picks the
 * work up next rather than documentation, and publishing them invites reading
 * them as promises.
 */
const config: Config = {
  title: 'Basalt',
  tagline: 'React Native on the desktop: Linux, macOS and Windows',
  favicon: 'img/favicon.ico',

  headTags: [
    // An SVG favicon, which modern browsers prefer over the .ico and which
    // stays sharp wherever the tab strip is rendered larger than 16 pixels.
    {
      tagName: 'link',
      attributes: {rel: 'icon', type: 'image/svg+xml', href: '/img/favicon.svg'},
    },
  ],

  future: {v4: true},

  // Canonical URLs and the sitemap are built from this, so it has to be the
  // address the site is actually served from, not an aspiration.
  url: 'https://basaltjs.dev',
  baseUrl: '/',

  organizationName: 'vincentvella',
  projectName: 'basalt',

  // Warn rather than throw. The repository docs are full of relative links
  // written to be read on GitHub, and one that does not resolve on the site
  // should not stop the site building while they are sorted out.
  onBrokenLinks: 'warn',

  i18n: {defaultLocale: 'en', locales: ['en']},

  // `.md` is CommonMark, `.mdx` is MDX. Docusaurus treats everything as MDX by
  // default, and these docs are about React Native: they mention <View>,
  // <Image> and <ScrollView> on nearly every page, which MDX reads as JSX and
  // then fails on for want of a closing tag. Prose written to be read on
  // GitHub should not have to escape itself to be published.
  markdown: {
    format: 'detect',
    hooks: {onBrokenMarkdownLinks: 'warn'},
  },

  presets: [
    [
      'classic',
      {
        docs: {
          sidebarPath: './sidebars.ts',
          editUrl: 'https://github.com/vincentvella/basalt/tree/main/website/',
        },
        blog: false,
        theme: {customCss: './src/css/custom.css'},
      } satisfies Preset.Options,
    ],
  ],

  plugins: [
    [
      '@docusaurus/plugin-content-docs',
      {
        id: 'contributing',
        path: '../docs',
        routeBasePath: 'contributing',
        sidebarPath: './sidebarsContributing.ts',
        // backlog/ is eighteen more files of the same, one per area.
        exclude: ['BACKLOG.md', 'HANDOFF.md', 'backlog/**', 'ci-performance.md'],
        editUrl: 'https://github.com/vincentvella/basalt/tree/main/',
      },
    ],
  ],

  themeConfig: {
    colorMode: {respectPrefersColorScheme: true},
    navbar: {
      title: 'Basalt',
      logo: {
        alt: '',
        src: 'img/logo-light.svg',
        srcDark: 'img/logo-dark.svg',
        width: 22,
        height: 22,
      },
      items: [
        {
          type: 'docSidebar',
          sidebarId: 'docs',
          position: 'left',
          label: 'Docs',
        },
        {
          type: 'docSidebar',
          sidebarId: 'contributing',
          docsPluginId: 'contributing',
          position: 'left',
          label: 'Contributing',
        },
        {
          href: 'https://github.com/vincentvella/basalt',
          label: 'GitHub',
          position: 'right',
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Docs',
          items: [
            {label: 'Getting started', to: '/docs/getting-started'},
            {label: 'Architecture', to: '/contributing/ARCHITECTURE'},
          ],
        },
        {
          title: 'More',
          items: [
            {label: 'GitHub', href: 'https://github.com/vincentvella/basalt'},
            {label: 'npm', href: 'https://www.npmjs.com/package/basalt-core'},
          ],
        },
      ],
      copyright: 'Basalt. Built with Docusaurus.',
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      // What the documentation is mostly made of.
      additionalLanguages: ['bash', 'json', 'cmake', 'cpp'],
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
