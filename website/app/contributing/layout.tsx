import { DocsLayout } from "fumadocs-ui/layouts/docs";
import { baseOptions } from "@/lib/layout.shared";
import { contributingSource } from "@/lib/source";

export default function Layout({ children }: LayoutProps<"/contributing">) {
  return (
    <DocsLayout tree={contributingSource.getPageTree()} {...baseOptions()}>
      {children}
    </DocsLayout>
  );
}
