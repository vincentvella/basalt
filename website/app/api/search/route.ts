import { createSearchAPI } from "fumadocs-core/search/server";
import { contributingSource, source } from "@/lib/source";

// Both trees in one index: somebody searching for "mounting manager" should
// find it whether it was written for an app author or for a contributor.
//
// `createFromSource` takes one loader, so this builds the index from both
// rather than standing up two endpoints and asking the reader which one they
// meant.
export const { GET } = createSearchAPI("advanced", {
  indexes: [...source.getPages(), ...contributingSource.getPages()].map(
    (page) => ({
      id: page.url,
      url: page.url,
      title: page.data.title,
      description: page.data.description,
      structuredData: page.data.structuredData,
    }),
  ),
});
