import { contributingLlms, docsLlms } from "@/lib/source";

export const revalidate = false;

// Both trees, in the order a reader meets them: what to build an app with,
// then how the thing itself is built.
export async function GET() {
  const [docs, contributing] = await Promise.all([
    docsLlms.index(),
    contributingLlms.index(),
  ]);
  return new Response(`${docs}\n\n${contributing}`);
}
