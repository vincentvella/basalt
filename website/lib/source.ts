import { loader } from "fumadocs-core/source";
import { metaSchema, pageSchema } from "fumadocs-core/source/schema";
import { defineDocs } from "fumadocs-mdx/macro";
import { contributingRoute, docsRoute } from "./shared";

// The site's own docs: MDX, because several pages are more than prose -- the
// support table is generated with a bar per host, and Getting started renders
// the version matrix out of the file the build enforces.
const docs = defineDocs({
  dir: "content/docs",
  docs: { schema: pageSchema },
  meta: { schema: metaSchema },
});

// The repository's own docs, synced in from ../docs. Markdown, and they stay
// markdown: they are read on GitHub at least as often as here.
const contributing = defineDocs({
  dir: "content/contributing",
  docs: { schema: pageSchema },
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
