import type { ReactNode } from "react";
import versions from "@/lib/supported-versions.json";

import styles from "./styles.module.css";

/**
 * Which React Native works here, rendered from the file that decides it.
 *
 * `packages/basalt-core/supported-versions.json` is not documentation: it is
 * what `bootstrap.sh` reads before it downloads anything and what `doctor`
 * checks an app against, and a version missing from it fails the build with a
 * message naming the file. So the table people read is that table, imported,
 * rather than a second copy of it in prose that nobody regenerates. Adding a
 * version is still building against it and running both suites.
 *
 * Used on the landing page and in Getting started, which had a hand-written
 * table of this and a paragraph of it between them.
 */

type Row = {
  key: string;
  label: string;
  // The exact release this was run against, where there is one. A minor is
  // what is claimed and a patch is what was actually built, and saying both
  // is the difference between a claim and a measurement.
  verified: string | null;
  state: "yes" | "untested" | "no";
  fastRefresh: boolean | null;
  note: string;
};

// "0.83 - 0.85" sorts by the newest it covers and "0.82 and older" by the one
// it names, so the column runs newest first whatever is added next.
function sortKey(reactNative: string): number {
  const found = reactNative.match(/\d+\.\d+/g) ?? [];
  return Math.max(...found.map(Number), 0);
}

function rows(): Row[] {
  const supported: Row[] = versions.supported.map((entry) => ({
    key: entry.reactNative,
    label: entry.reactNative,
    verified: entry.verified ?? null,
    state: "yes" as const,
    fastRefresh: entry.fastRefresh,
    // `notes` is written for somebody choosing a version. What a version
    // cannot do is its own column.
    note: entry.notes ?? "",
  }));

  const rest: Row[] = versions["known-unsupported"].map((entry) => ({
    key: entry.reactNative,
    label: entry.reactNative,
    verified: null,
    // "Never built or tested" is a different answer from "tried, and stopped".
    state: /never built or tested/i.test(entry.why)
      ? ("untested" as const)
      : ("no" as const),
    fastRefresh: null,
    note: entry.why,
  }));

  return [...supported, ...rest].sort(
    (a, b) => sortKey(b.key) - sortKey(a.key),
  );
}

const WORD: Record<Row["state"], string> = {
  yes: "Works",
  untested: "Untested",
  no: "No",
};

export default function VersionMatrix(): ReactNode {
  return (
    <table className={styles.matrix}>
      <thead>
        <tr>
          <th>React Native</th>
          <th>Supported</th>
          <th>Fast Refresh</th>
          <th>What it is</th>
        </tr>
      </thead>
      <tbody>
        {rows().map((row) => (
          <tr key={row.key}>
            <td className={styles.version}>
              {row.label}
              {row.verified != null && (
                <span className={styles.verified}>
                  tested on {row.verified}
                </span>
              )}
            </td>
            <td>
              <span className={`${styles.state} ${styles[row.state]}`}>
                {WORD[row.state]}
              </span>
            </td>
            <td className={styles.fast}>
              {row.fastRefresh == null ? "" : row.fastRefresh ? "Yes" : "No"}
            </td>
            <td className={styles.note}>{row.note}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
