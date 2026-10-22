import Link from "next/link";
import type { ReactNode } from "react";

/**
 * The pieces the landing page is built from.
 *
 * Tailwind rather than the stylesheet this page used to carry: the site is
 * Tailwind now, and a second way of styling one page is the thing that rots.
 */

export function Section({
  children,
  tinted = false,
}: {
  children: ReactNode;
  tinted?: boolean;
}) {
  return (
    <section
      className={
        tinted
          ? "border-y border-fd-border bg-fd-card py-16 px-4"
          : "py-16 px-4"
      }
    >
      <div className="mx-auto w-full max-w-6xl">{children}</div>
    </section>
  );
}

export function SectionTitle({ children }: { children: ReactNode }) {
  return (
    <h2 className="mb-4 text-3xl font-semibold tracking-tight">{children}</h2>
  );
}

export function SectionLede({ children }: { children: ReactNode }) {
  return <p className="mb-10 max-w-3xl text-fd-muted-foreground">{children}</p>;
}

export function Body({ children }: { children: ReactNode }) {
  return <p className="mb-4 text-fd-muted-foreground">{children}</p>;
}

export function Foot({ children }: { children: ReactNode }) {
  return <p className="mt-10 max-w-3xl text-fd-muted-foreground">{children}</p>;
}

/** The one accent, as a button. The second is the quiet one beside it. */
export function Button({
  href,
  children,
  primary = false,
}: {
  href: string;
  children: ReactNode;
  primary?: boolean;
}) {
  return (
    <Link
      href={href}
      className={
        primary
          ? "rounded-lg bg-fd-primary px-5 py-3 font-medium text-fd-primary-foreground transition-opacity hover:opacity-90"
          : "rounded-lg border border-fd-border bg-fd-card px-5 py-3 font-medium transition-colors hover:bg-fd-accent"
      }
    >
      {children}
    </Link>
  );
}
