import type { MetadataRoute } from "next";
import { contributingSource, source } from "@/lib/source";

/**
 * Every page, for the crawlers.
 *
 * Docusaurus generated one of these and Next does not, so it is written here
 * rather than quietly lost in the move: a docs site nobody can find is a docs
 * site nobody reads.
 */
export default function sitemap(): MetadataRoute.Sitemap {
  const pages = [...source.getPages(), ...contributingSource.getPages()];
  return [
    { url: "https://basaltjs.dev", priority: 1 },
    ...pages.map((page) => ({ url: `https://basaltjs.dev${page.url}` })),
  ];
}
