import { RootProvider } from "fumadocs-ui/provider/next";
import type { Metadata } from "next";
import "./global.css";

const DESCRIPTION =
  "Basalt runs React Native apps on Linux, macOS and Windows, on React Native's own C++ core rather than on a browser engine.";

export const metadata: Metadata = {
  title: {
    default: "Basalt",
    template: "%s | Basalt",
  },
  description: DESCRIPTION,
  metadataBase: new URL("https://basaltjs.dev"),
  // Without these a link to this site is a bare URL wherever it is posted.
  // The images come from the `opengraph-image` files, which Next attaches to
  // this metadata per route, so there is nothing to list here.
  openGraph: {
    type: "website",
    siteName: "Basalt",
    title: "Basalt",
    description: DESCRIPTION,
    url: "https://basaltjs.dev",
    locale: "en",
  },
  twitter: {
    card: "summary_large_image",
    title: "Basalt",
    description: DESCRIPTION,
  },
};

export default function Layout({ children }: LayoutProps<"/">) {
  return (
    <html lang="en" suppressHydrationWarning>
      <body className="flex flex-col min-h-screen">
        <RootProvider>{children}</RootProvider>
      </body>
    </html>
  );
}
