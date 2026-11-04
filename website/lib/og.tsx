import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ImageResponse } from "next/og";

/**
 * The card a link to this site unfurls into.
 *
 * There was none, so every link posted to Slack, Discord, X or Bluesky was a
 * bare URL. Next's file conventions put one image per route segment, and
 * `card()` is what both of them draw so that the home page and a docs page
 * cannot drift apart.
 *
 * The mascot is read off disk and inlined, because satori resolves no
 * relative URLs: at build time there is no origin to resolve them against.
 *
 * One weight throughout, which is satori's doing rather than a choice: it
 * draws the fonts it is given, and the built-in one has a single weight. A
 * bold heading here would mean shipping a second font file.
 */

export const OG_SIZE = { width: 1200, height: 630 };
export const OG_CONTENT_TYPE = "image/png";

const INK = "#16181d";
const PANEL = "#1c1f26";
const BONE = "#f7f6f3";
const MUTED = "#9aa0a8";
const EMBER = "#e2561f";

function puffin(): string {
  const file = join(process.cwd(), "public", "img", "logo-inverse.svg");
  const svg = readFileSync(file, "utf8");
  return `data:image/svg+xml;base64,${Buffer.from(svg).toString("base64")}`;
}

/**
 * `title` is the page, and `eyebrow` says where it sits. The home page passes
 * the project name as the title and nothing else; a docs page passes its own
 * title, which is the whole reason these are generated per page rather than
 * being one static image.
 */
export function card({ title, eyebrow }: { title: string; eyebrow?: string }) {
  return new ImageResponse(
    <div
      style={{
        width: "100%",
        height: "100%",
        display: "flex",
        flexDirection: "column",
        justifyContent: "space-between",
        background: INK,
        padding: 72,
        // The one brand gesture: a bar in ember down the left edge, the
        // same colour as the bill on the bird above it.
        borderLeft: `16px solid ${EMBER}`,
      }}
    >
      <div style={{ display: "flex", alignItems: "center", gap: 28 }}>
        {/* An img, not next/image: satori draws this, not a browser. */}
        <img src={puffin()} width={104} height={104} alt="" />
        <div style={{ display: "flex", flexDirection: "column" }}>
          <div style={{ fontSize: 40, color: BONE }}>Basalt</div>
          {/* Only beside a page title. On the home card the big line below
                is this sentence, and printing it twice reads as a mistake. */}
          {eyebrow ? (
            <div style={{ fontSize: 24, color: MUTED }}>
              React Native on the desktop
            </div>
          ) : null}
        </div>
      </div>

      <div style={{ display: "flex", flexDirection: "column" }}>
        {eyebrow ? (
          <div
            style={{
              fontSize: 22,
              color: EMBER,
              letterSpacing: 2,
              textTransform: "uppercase",
              marginBottom: 12,
            }}
          >
            {eyebrow}
          </div>
        ) : null}
        <div
          style={{
            fontSize: title.length > 34 ? 60 : 76,
            color: BONE,
            lineHeight: 1.1,
          }}
        >
          {title}
        </div>
      </div>

      <div
        style={{
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          fontSize: 22,
          color: MUTED,
        }}
      >
        <div style={{ display: "flex", gap: 10 }}>
          {["Linux", "macOS", "Windows"].map((desktop) => (
            <div
              key={desktop}
              style={{
                padding: "6px 16px",
                borderRadius: 999,
                background: PANEL,
                color: MUTED,
              }}
            >
              {desktop}
            </div>
          ))}
        </div>
        <div>basaltjs.dev</div>
      </div>
    </div>,
    OG_SIZE,
  );
}
