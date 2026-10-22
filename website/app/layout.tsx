import { RootProvider } from "fumadocs-ui/provider/next";
import type { Metadata } from "next";
import "./global.css";

export const metadata: Metadata = {
  title: {
    default: "Basalt",
    template: "%s | Basalt",
  },
  description:
    "Basalt runs React Native apps on Linux, macOS and Windows, on React Native's own C++ core rather than on a browser engine.",
  metadataBase: new URL("https://basaltjs.dev"),
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
