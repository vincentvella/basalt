/**
 * The title bar requests currently mounted, and what they add up to.
 *
 * Kept apart from TitleBar.tsx, with no React Native in it, so that it can be
 * tested with nothing but Node -- this is the part with rules in it.
 *
 * Several screens may each ask for something, as they do of <StatusBar>, and
 * the most recently mounted request wins. But key by key: a screen that only
 * sets a title does not reset the colour an app shell underneath it set, and
 * when it unmounts the shell's title comes back.
 *
 * @format
 */

export const TITLE_BAR_KEYS = [
  'title',
  'style',
  'backgroundColor',
  'textColor',
  'borderColor',
] as const;

export type TitleBarKey = (typeof TITLE_BAR_KEYS)[number];

/** The system's title bar, or none and the app draws its own. */
export type TitleBarStyle = 'native' | 'hidden';

/**
 * A colour as React Native accepts one -- a string, or the number
 * `processColor` produces. Deliberately not React Native's `ColorValue`: this
 * file has no React Native in it, which is what lets it be tested under plain
 * Node.
 */
export type TitleBarColor = string | number | null;

export type TitleBarOptions = {
  title?: string | null;
  style?: TitleBarStyle;
  backgroundColor?: TitleBarColor;
  textColor?: TitleBarColor;
  borderColor?: TitleBarColor;
};

/** What a stack of requests resolves to: the same keys, none of them required. */
export type TitleBarRequest = TitleBarOptions;

export type TitleBarStack = {
  push(options?: TitleBarOptions | null): number;
  update(id: number, options?: TitleBarOptions | null): void;
  remove(id: number): void;
  resolve(): TitleBarRequest;
  readonly size: number;
};

function pick(options?: TitleBarOptions | null): TitleBarRequest {
  const picked: TitleBarRequest = {};
  for (const key of TITLE_BAR_KEYS) {
    if (options != null && options[key] !== undefined) {
      // Each key's type is preserved by the assignment below; the index here is
      // the one place the compiler cannot see that, because `key` ranges over
      // the union.
      (picked as Record<TitleBarKey, unknown>)[key] = options[key];
    }
  }
  return picked;
}

export function createTitleBarStack(): TitleBarStack {
  const entries: Array<{id: number; options: TitleBarRequest}> = [];
  let nextId = 1;

  return {
    push(options?: TitleBarOptions | null): number {
      const id = nextId++;
      entries.push({id, options: pick(options)});
      return id;
    },

    update(id: number, options?: TitleBarOptions | null): void {
      const entry = entries.find(candidate => candidate.id === id);
      if (entry != null) {
        entry.options = pick(options);
      }
    },

    remove(id: number): void {
      const index = entries.findIndex(candidate => candidate.id === id);
      if (index !== -1) {
        entries.splice(index, 1);
      }
    },

    // Later entries override earlier ones, one key at a time.
    resolve(): TitleBarRequest {
      const resolved: TitleBarRequest = {};
      for (const {options} of entries) {
        Object.assign(resolved, options);
      }
      return resolved;
    },

    get size(): number {
      return entries.length;
    },
  };
}

// Whether two resolved requests would ask the host for the same thing, so that
// a re-render that changed nothing does not cross into native at all.
export function sameRequest(
  a?: TitleBarRequest | null,
  b?: TitleBarRequest | null,
): boolean {
  return TITLE_BAR_KEYS.every(key => (a ?? {})[key] === (b ?? {})[key]);
}
