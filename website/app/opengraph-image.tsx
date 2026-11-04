import { card, OG_CONTENT_TYPE, OG_SIZE } from "@/lib/og";

// The default card, inherited by every route that does not draw its own.
export const size = OG_SIZE;
export const contentType = OG_CONTENT_TYPE;
export const alt = "Basalt: React Native on Linux, macOS and Windows";

export default function Image() {
  return card({ title: "React Native on the desktop" });
}
