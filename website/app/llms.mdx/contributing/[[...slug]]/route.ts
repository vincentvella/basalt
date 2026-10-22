import { notFound } from "next/navigation";
import { pageMarkdownUrl } from "@/lib/shared";
import { contributingLlms, contributingSource } from "@/lib/source";

export const revalidate = false;

export async function GET(
  _req: Request,
  { params }: RouteContext<"/llms.mdx/contributing/[[...slug]]">,
) {
  const { slug } = await params;
  // The last segment is `content.md`, which names the format rather than the
  // page.
  const page = contributingSource.getPage(slug?.slice(0, -1));
  if (!page) notFound();

  return new Response(await contributingLlms.page(page), {
    headers: { "Content-Type": "text/markdown" },
  });
}

export function generateStaticParams() {
  return contributingSource.getPages().map((page) => ({
    slug: pageMarkdownUrl("contributing", page.slugs).segments,
  }));
}
