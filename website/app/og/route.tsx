import type { NextRequest } from "next/server";
import { card } from "@/lib/og";

/**
 * A card with a page's own title on it.
 *
 * A route rather than an `opengraph-image.tsx` beside the page, because the
 * docs route is an optional catch-all and Next refuses a metadata file
 * inside one: "Optional catch-all must be the last part of the URL". The
 * pages pass their title here instead, and the result is cached at the edge
 * like any other response.
 */
export function GET(request: NextRequest) {
  const parameters = request.nextUrl.searchParams;
  const title =
    parameters.get("title")?.slice(0, 120) || "React Native on the desktop";
  const eyebrow = parameters.get("eyebrow")?.slice(0, 40) || undefined;
  return card({ title, eyebrow });
}
