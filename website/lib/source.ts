import { llms, loader } from "fumadocs-core/source";
import { metaSchema, pageSchema } from "fumadocs-core/source/schema";
import { defineDocs } from "fumadocs-mdx/macro";
import { contributingRoute, docsRoute } from "./shared";

// The site's own docs: MDX, because several pages are more than prose -- the
// support table is generated with a bar per host, and Getting started renders
// the version matrix out of the file the build enforces.
const docs = defineDocs({
  dir: "content/docs",
  docs: { schema: pageSchema, postprocess: { includeProcessedMarkdown: true } },
  meta: { schema: metaSchema },
});

// The repository's own docs, synced in from ../docs. Markdown, and they stay
// markdown: they are read on GitHub at least as often as here.
const contributing = defineDocs({
  dir: "content/contributing",
  docs: { schema: pageSchema, postprocess: { includeProcessedMarkdown: true } },
  meta: { schema: metaSchema },
});

export const source = loader({
  baseUrl: docsRoute,
  source: docs.toFumadocsSource(),
});

export const contributingSource = loader({
  baseUrl: contributingRoute,
  source: contributing.toFumadocsSource(),
});

/**
 * The two trees as markdown, for anything reading rather than browsing.
 *
 * `/llms.txt` is the index of both and `/llms-full.txt` is all of it; a single
 * page is `/llms.mdx/docs/...`. A docs site about a platform is read by agents
 * at least as often as by people now, and this is the format that asks them to
 * parse nothing.
 */
const renderPage = async (page: {
  data: { title: string; getText: (kind: "processed") => Promise<string> };
  url: string;
}) =>
  `# ${page.data.title} (${page.url})\n\n${await page.data.getText("processed")}`;

export const docsLlms = llms(source, { renderPage });
export const contributingLlms = llms(contributingSource, { renderPage });
