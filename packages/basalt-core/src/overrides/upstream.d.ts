/**
 * `basalt-core/upstream/...`, which only Metro can resolve.
 *
 * It is not a real package. `UPSTREAM_PREFIX` in metro-config.ts turns that
 * specifier into an absolute path inside whichever React Native is being
 * bundled, so that neither Babel's deep-import plugin nor Metro's
 * package-exports check has anything to warn about. Node cannot resolve it and
 * neither can TypeScript.
 *
 * `any`, deliberately and narrowly. What is behind this prefix is React
 * Native's *internals* -- HMRClient, PolyfillFunctions, the base
 * XMLHttpRequest, a native component's generated wrapper -- none of which
 * React Native exports types for, at paths it reserves the right to move. A
 * fabricated type here would be a guess that the compiler would then enforce
 * against the files that shadow them, which is worse than no type: it would be
 * confidently wrong at exactly the seam this project has least control over.
 *
 * The files that use it are rewrites of React Native's own, and what makes
 * them correct is matching React Native's behaviour, which is checked by
 * running them rather than by this declaration.
 *
 * @format
 */

// The shorthand form, which types every import from this prefix as `any` --
// default and named alike. Both shapes appear in the files below, and
// enumerating the names is not possible for a wildcard.
declare module 'basalt-core/upstream/*';
