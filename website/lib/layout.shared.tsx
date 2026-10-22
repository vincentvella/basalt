import type { BaseLayoutProps } from "fumadocs-ui/layouts/shared";
import { appName, contributingRoute, docsRoute, gitConfig } from "./shared";

/**
 * The navigation every layout shares: the mark, the two docs trees, and the
 * repository.
 */
export function baseOptions(): BaseLayoutProps {
  return {
    nav: {
      title: (
        <>
          <img
            src="/img/logo-light.svg"
            alt=""
            width={22}
            height={22}
            className="dark:hidden"
          />
          <img
            src="/img/logo-dark.svg"
            alt=""
            width={22}
            height={22}
            className="hidden dark:block"
          />
          {appName}
        </>
      ),
    },
    links: [
      { text: "Docs", url: docsRoute, active: "nested-url" },
      { text: "Contributing", url: contributingRoute, active: "nested-url" },
    ],
    githubUrl: `https://github.com/${gitConfig.user}/${gitConfig.repo}`,
  };
}
