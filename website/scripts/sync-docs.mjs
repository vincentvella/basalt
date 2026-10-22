// The repository's own docs are read on GitHub at least as often as here, so
// they stay plain markdown in `docs/` and this turns them into the site's
// Contributing tree before each build: frontmatter from each file's heading,
// the sidebar order, and links between them rewritten for the site.
//
// The same shape velmux's site uses, with one difference: that repo's docs are
// the whole of its docs, and these are the half written for somebody working
// *on* Basalt. The half written for somebody building an app is MDX in
// `content/docs`, authored here, because several of those pages are more than
// prose -- a generated support table, a version matrix read out of the file
// the build enforces.
import {
  copyFileSync,
  mkdirSync,
  readdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { join } from "node:path";

const site = new URL("..", import.meta.url).pathname;
const repo = join(site, "..");
const docs = join(repo, "docs");
const out = join(site, "content/contributing");
const github = "https://github.com/vincentvella/basalt/blob/main";

// What the site does not render, and why: BACKLOG.md and backlog/ are a
// working list that changes several times a day and reads as noise next to the
// four documents here, and ci-performance.md is a measurement log. All three
// stay in the repository, where they are read.
const skip = new Set(["BACKLOG.md", "ci-performance.md"]);

// Sidebar order; anything else follows alphabetically. The two groups the
// Docusaurus sidebar had -- how it works, then working on it -- in one list,
// because four files do not need headings over them.
const order = ["ARCHITECTURE", "DECISIONS", "TESTING", "PORTING"];

rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });

const pages = [];
for (const file of readdirSync(docs)) {
  if (!file.endsWith(".md") || skip.has(file)) continue;
  const slug = file.replace(/\.md$/, "");
  pages.push(slug);

  let text = readFileSync(join(docs, file), "utf8");
  const title = text.match(/^# (.+)$/m)?.[1] ?? slug;
  // Fumadocs renders the title from the frontmatter, so the heading would
  // print it twice. Anchored to the start of a line and not across them.
  text = text.replace(/^# .+\n+/m, "");

  text = text
    // Another contributing doc: ARCHITECTURE.md#x -> /contributing/ARCHITECTURE#x
    .replace(/\]\(([A-Za-z-]+)\.md(#[^)]*)?\)/g, (_whole, page, hash = "") =>
      skip.has(`${page}.md`)
        ? `](${github}/docs/${page}.md${hash})`
        : `](/contributing/${page}${hash})`,
    )
    // backlog/foo.md and anything else under docs/ that the site does not
    // render: send the reader to GitHub rather than to a 404.
    .replace(
      /\]\((backlog\/[^)]+)\)/g,
      (_, path) => `](${github}/docs/${path})`,
    )
    // ../packages/..., ../scripts/...: the repository, which is not here.
    .replace(/\]\(\.\.\/([^)]+)\)/g, (_, path) => `](${github}/${path})`);

  // For search results and link previews: the first sentence of the first
  // paragraph, as plain text.
  const paragraph =
    text
      .split(/\n\s*\n/)
      .find((p) => p.trim() && !/^[#!|`<\-*]/.test(p.trim())) ?? "";
  const plain = paragraph
    .replace(/\s+/g, " ")
    .replace(/\*\*|`/g, "")
    .replace(/\[([^\]]+)\]\([^)]*\)/g, "$1")
    .trim();
  const description = (plain.match(/^.+?[.!?](\s|$)/)?.[0] ?? plain)
    .trim()
    .slice(0, 200);

  writeFileSync(
    join(out, `${slug}.md`),
    `---\ntitle: ${JSON.stringify(title)}\ndescription: ${JSON.stringify(description)}\n---\n\n${text}`,
  );
}

// The version matrix renders this, and it is `packages/basalt-core`'s: the
// file bootstrap reads before it downloads anything and doctor checks an app
// against. Copied in rather than imported across the repository because the
// bundler resolves nothing outside the site, and copied *here* so that the
// copy cannot be stale: every build runs this first.
copyFileSync(
  join(repo, "packages/basalt-core/supported-versions.json"),
  join(site, "lib/supported-versions.json"),
);

pages.sort(
  (a, b) =>
    (order.indexOf(a) + 1 || 99) - (order.indexOf(b) + 1 || 99) ||
    a.localeCompare(b),
);
writeFileSync(
  join(out, "meta.json"),
  `${JSON.stringify({ title: "Contributing", pages }, null, 2)}\n`,
);
console.log(`synced ${pages.length} contributing docs into the site`);
