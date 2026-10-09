# Website

This website is built using [Docusaurus](https://docusaurus.io/), a modern static website generator.

## Installation

```bash
npm install
```

**Note**: feel free to use the package manager of your choice.

## Local Development

```bash
npm run start
```

This command starts a local development server and opens up a browser window. Most changes are reflected live without having to restart the server.

## Build

```bash
npm run build
```

This command generates static content into the `build` directory and can be served using any static contents hosting service.

## Deployment

The site is a Vercel project (`basalt`, root directory `website`), serving
basaltjs.dev. **A push does not deploy it, on any branch.** `vercel.json` turns
the git integration off:

```json
{"git": {"deploymentEnabled": false}}
```

Every branch, not just `main`, because a preview deployment is a build like any
other: pushing the `ci` branch or opening a pull request would otherwise cost
one. Deploying from the CLI is unaffected, that not being the git integration.

Why: the site renders `website/**` and `../docs`, and `docusaurus.config.ts`
excludes `BACKLOG.md`, `backlog/**` and `ci-performance.md` from the latter.
Almost every commit in this repository touches one of those excluded files and
nothing else the site renders, so auto-deploying on push rebuilt an identical
site about twenty times a day. The numbers are in `docs/ci-performance.md`.

To ship it, from this directory:

```bash
npx vercel --prod
```

The GitHub Actions `Docs` workflow still builds the site on every push that
changes something it renders, so a commit that breaks the build says so on the
commit. What it does not do is deploy, which is the one thing that now needs
asking for.

`npm run deploy` is Docusaurus's own GitHub Pages command and is not what hosts
this site; it is left alone rather than removed because it is Docusaurus's, not
this repository's.
