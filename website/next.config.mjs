import { createMDX } from "fumadocs-mdx/next";

const withMDX = createMDX();

/** @type {import('next').NextConfig} */
const config = {
  reactStrictMode: true,
  async redirects() {
    return [
      // Both trees have a page people mean when they say "the docs".
      { source: "/docs", destination: "/docs/intro", permanent: false },
      {
        source: "/contributing",
        destination: "/contributing/ARCHITECTURE",
        permanent: false,
      },
    ];
  },
};

export default withMDX(config);
