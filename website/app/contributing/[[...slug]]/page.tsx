import {
  DocsBody,
  DocsDescription,
  DocsPage,
  DocsTitle,
  MarkdownCopyButton,
  ViewOptionsPopover,
} from "fumadocs-ui/layouts/docs/page";
import { createRelativeLink } from "fumadocs-ui/mdx";
import type { Metadata } from "next";
import { notFound } from "next/navigation";
import { getMDXComponents } from "@/components/mdx";
import { pageMarkdownUrl, sourceUrl } from "@/lib/shared";
import { contributingSource } from "@/lib/source";

export default async function Page(
  props: PageProps<"/contributing/[[...slug]]">,
) {
  const params = await props.params;
  const page = contributingSource.getPage(params.slug);
  if (!page) notFound();

  const MDX = page.data.body;
  const markdownUrl = pageMarkdownUrl("contributing", page.slugs).url;

  return (
    <DocsPage toc={page.data.toc} full={page.data.full}>
      <DocsTitle>{page.data.title}</DocsTitle>
      {page.data.description ? (
        <DocsDescription>{page.data.description}</DocsDescription>
      ) : null}
      {/* The page as markdown, and where it is written. These are the repository's own, so the link goes to docs/ rather than to the copy this site builds. */}
      <div className="flex flex-row items-center gap-2 border-b pb-6">
        <MarkdownCopyButton markdownUrl={markdownUrl} />
        <ViewOptionsPopover
          markdownUrl={markdownUrl}
          githubUrl={sourceUrl(page.path, true)}
        />
      </div>
      <DocsBody>
        <MDX
          components={getMDXComponents({
            a: createRelativeLink(contributingSource, page),
          })}
        />
      </DocsBody>
    </DocsPage>
  );
}

export async function generateStaticParams() {
  return contributingSource.generateParams();
}

export async function generateMetadata(
  props: PageProps<"/contributing/[[...slug]]">,
): Promise<Metadata> {
  const params = await props.params;
  const page = contributingSource.getPage(params.slug);
  if (!page) notFound();
  return { title: page.data.title, description: page.data.description };
}
