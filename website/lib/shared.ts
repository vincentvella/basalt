/**
 * What the site is called and where its two docs trees live.
 *
 * Two, which is what differs from most Fumadocs sites: `/docs` is written for
 * somebody building an app and lives in `content/docs`, and `/contributing` is
 * the repository's own `docs/` folder, synced in by `scripts/sync-docs.mjs` so
 * that it stays readable on GitHub as well.
 */

export const appName = "Basalt";
export const docsRoute = "/docs";
export const contributingRoute = "/contributing";

export const gitConfig = {
  user: "vincentvella",
  repo: "basalt",
  branch: "main",
};

/** Where a page's source is on GitHub, for the "edit this page" link. */
export function sourceUrl(path: string, inRepoDocs: boolean): string {
  const prefix = inRepoDocs ? "docs" : "website/content/docs";
  return `https://github.com/${gitConfig.user}/${gitConfig.repo}/blob/${gitConfig.branch}/${prefix}/${path}`;
}
