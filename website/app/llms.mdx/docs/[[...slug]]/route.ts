import { notFound } from "next/navigation";
import { pageMarkdownUrl } from "@/lib/shared";
import { docsLlms, source } from "@/lib/source";

export const revalidate = false;

export async function GET(
  _req: Request,
  { params }: RouteContext<"/llms.mdx/docs/[[...slug]]">,
) {
  const { slug } = await params;
  // The last segment is `content.md`, which names the format rather than the
  // page.
  const page = source.getPage(slug?.slice(0, -1));
  if (!page) notFound();

  return new Response(await docsLlms.page(page), {
    headers: { "Content-Type": "text/markdown" },
  });
}

export function generateStaticParams() {
  return source.getPages().map((page) => ({
    slug: pageMarkdownUrl("docs", page.slugs).segments,
  }));
}
