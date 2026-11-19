import { createMDX } from "fumadocs-mdx/next";

const withMDX = createMDX();

/** @type {import('next').NextConfig} */
const config = {
  reactStrictMode: true,
  experimental: {
    // Set by `next upgrade --agent=security`, which is what moved this site
    // from 16.4.2 to 16.4.6 for a published advisory. It records that the
    // upgrade was applied by an agent rather than by hand.
    agentUpgrade: "security",
  },
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
