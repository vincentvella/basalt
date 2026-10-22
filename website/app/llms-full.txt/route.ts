import { contributingLlms, docsLlms } from "@/lib/source";

export const revalidate = false;

export async function GET() {
  const [docs, contributing] = await Promise.all([
    docsLlms.full(),
    contributingLlms.full(),
  ]);
  return new Response(`${docs}\n\n${contributing}`);
}
