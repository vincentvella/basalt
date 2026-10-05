/**
 * The globals React Native's runtime installs, named here instead of taken from
 * whichever ambient declaration happens to be in scope.
 *
 * Which `.d.ts` describes these depends on the React Native version. On main,
 * `src/types/globals.d.ts` declares `Blob`, `File`, `FileReader` and the
 * animation-frame functions by hand -- and its `Blob` is missing the `close()`
 * the installed class really has. At the pinned release CI builds against that
 * file is not in the program at all, so `@types/node`'s `buffer` Blob wins and
 * the other three are simply undeclared.
 *
 * So the apps import from here. Merging into the ambient name instead typechecks
 * against one of those two and fails against the other, which is how this was
 * found: green locally, red on all nine CI shards.
 *
 * The shapes are the ones in React Native's own Libraries/Blob, not the web's.
 *
 * @format
 */
'use strict';

/** Libraries/Blob/Blob.js. `close()` is the one method that is RN's, not the web's. */
export type RuntimeBlob = {
  readonly size: number;
  readonly type: string;
  slice(start?: number, end?: number, contentType?: string): RuntimeBlob;
  /**
   * Releases the bytes, which native holds and no collector here can reach.
   * Declared because the class has it; see Blob.js, where it is the method the
   * doc comment is mostly about.
   */
  close(): void;
};

/**
 * Both fields are optional, unlike `BlobOptions` in BlobTypes.js.
 *
 * BlobManager.createFromParts reads `options ? options.type : ''` and
 * `options ? options.lastModified : Date.now()`, so a caller that passes one and
 * not the other is reading what it asked for. Requiring both made this file pass
 * a `lastModified` it does not care about.
 */
export type RuntimeBlobOptions = {type?: string; lastModified?: number};

/** Libraries/Blob/File.js, which extends Blob and adds a name. */
export type RuntimeFile = RuntimeBlob & {
  readonly name: string;
  readonly lastModified: number;
};

/** Libraries/Blob/FileReader.js, in the half these apps use. */
export type RuntimeFileReader = {
  result: string | ArrayBuffer | null;
  error: Error | null;
  onload: (() => void) | null;
  onerror: (() => void) | null;
  readAsText(blob: RuntimeBlob): void;
  readAsDataURL(blob: RuntimeBlob): void;
  readAsArrayBuffer(blob: RuntimeBlob): void;
};

// One cast, in one place. Everything optional is optional because the point of
// probe.tsx is to find out which of these a desktop actually got.
const runtime = globalThis as unknown as {
  Blob: new (
    parts?: ReadonlyArray<RuntimeBlob | string>,
    options?: RuntimeBlobOptions,
  ) => RuntimeBlob;
  File: new (
    parts: ReadonlyArray<RuntimeBlob | string>,
    name: string,
    options?: RuntimeBlobOptions,
  ) => RuntimeFile;
  FileReader: new () => RuntimeFileReader;
  requestAnimationFrame?: (callback: (time: number) => void) => number;
  requestIdleCallback?: (callback: () => void) => number;
};

export const Blob = runtime.Blob;
export const File = runtime.File;
export const FileReader = runtime.FileReader;

/**
 * These two are read rather than re-exported, because a probe asks whether the
 * runtime installed them at all and a binding captured at import time would
 * answer for the module's first moment instead of the probe's.
 */
export function requestAnimationFrameOrNull(): typeof runtime.requestAnimationFrame {
  return runtime.requestAnimationFrame;
}

export function requestIdleCallbackOrNull(): typeof runtime.requestIdleCallback {
  return runtime.requestIdleCallback;
}
