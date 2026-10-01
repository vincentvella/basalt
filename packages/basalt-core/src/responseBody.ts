/**
 * What the platform actually delivered for a `blob` response, and how to make a
 * Blob out of it.
 *
 * `src/overrides/setUpXHR.ts` asks for `arraybuffer` when JavaScript asked for
 * `blob`, because React Native's XHR turns that into a native request for
 * `base64` -- and base64 is the only response encoding ReactCxxPlatform has that
 * can carry arbitrary bytes through a JavaScript string intact.
 *
 * **Except that it only encodes on some versions.** `encodeResponseBody` is a
 * fix that landed in ReactCxxPlatform's NetworkingModule after 0.86; before it,
 * `didReceiveNetworkData` passes the body through whatever was asked for. And
 * ReactCxxPlatform is fetched at the app's exact React Native version -- see
 * native/bootstrap.sh for why one vendored copy cannot serve several -- so both
 * behaviours are live at once across supported versions, and neither the app nor
 * this package chooses which.
 *
 * Handing a raw body to `BlobModule.createFromParts` as `base64` is what the
 * failure looked like: the decoder skips the characters that are not in the
 * alphabet and returns whatever the rest happens to mean, so a 98-byte JSON body
 * became one byte, or none. `fetch(...).json()` then failed with "JSON Parse
 * error: Unexpected character: v" -- a complaint about a byte that appears
 * nowhere in the response, which is a long way from the cause. Found in kino,
 * where every call to its daemon failed this way.
 *
 * So the delivered string is examined rather than assumed.
 *
 * **One observation settles it.** Whether the platform encodes is a property of
 * the build, not of a response, so it is learned rather than re-decided. A body
 * that *cannot* be base64 -- one containing a character outside the alphabet, or
 * a length that is not a multiple of four -- proves the platform does not
 * encode, and that verdict holds for every response afterwards. A platform that
 * does encode never produces such a body, so the verdict can only ever be
 * reached correctly.
 *
 * **Content-Length settles it outright, when there is one.** The delivered
 * string is either the body or its base64, and those have different lengths, so
 * a byte count from the server picks one. That is exact and needs nothing
 * learned. It is not always available -- a chunked response has no
 * Content-Length, and kino's daemon sends every response chunked -- which is why
 * it is a shortcut rather than the whole answer.
 *
 * **The window where it can still be wrong, stated.** With no Content-Length and
 * nothing yet learned, a body that looks like canonical base64 is taken as
 * base64. Most bodies are decisive: any JSON object or array contains `{`, `"`
 * or `,`. A few are not -- `null`, `true` and `1234` are all four characters
 * from the alphabet -- so a non-encoding platform whose *first* blob response is
 * chunked *and* one of those is read wrongly, once.
 *
/** One part for `BlobModule.createFromParts`, with the size the Blob should claim. */
export interface ResponseBlobPart {
  part: {data: string; type: 'base64' | 'string'};
  size: number;
  /** False when the platform did not encode, which the caller reports once. */
  encoded: boolean;
}

/**
 * Canonical base64: four-character groups from the alphabet, with at most two
 * `=` and only at the end.
 */
export function looksLikeBase64(value: string): boolean {
  if (value.length === 0 || value.length % 4 !== 0) {
    return false;
  }
  return /^[A-Za-z0-9+/]+={0,2}$/.test(value);
}

/** The number of bytes `value` is as UTF-8, which is what a Blob's size means. */
export function utf8Length(value: string): number {
  let bytes = 0;
  for (let i = 0; i < value.length; i++) {
    const code = value.charCodeAt(i);
    if (code < 0x80) {
      bytes += 1;
    } else if (code < 0x800) {
      bytes += 2;
    } else if (code >= 0xd800 && code <= 0xdbff) {
      // A surrogate pair is one character of four bytes; skip its low half.
      bytes += 4;
      i++;
    } else {
      bytes += 3;
    }
  }
  return bytes;
}

/** The decoded length of canonical base64, from its length and padding alone. */
export function base64Length(encoded: string): number {
  const padding = encoded.endsWith('==') ? 2 : encoded.endsWith('=') ? 1 : 0;
  return Math.max(0, (encoded.length / 4) * 3 - padding);
}

/**
 * A reader that remembers what it has learned about this platform.
 *
 * One per process in the override; a factory rather than module state so that a
 * test can start from not knowing.
 */
export function createResponseBodyReader(): (
  delivered: string,
  contentLength?: number | null,
) => ResponseBlobPart {
  // null while nothing decisive has been seen.
  let platformEncodes: boolean | null = null;

  return function read(delivered: string, contentLength?: number | null): ResponseBlobPart {
    const couldBeBase64 = looksLikeBase64(delivered);
    if (!couldBeBase64 && delivered.length > 0) {
      // Decisive: a platform that encodes could not have produced this.
      platformEncodes = false;
    }

    // A byte count from the server, where there is one, decides it outright --
    // and teaches the same lesson for the responses that arrive without one.
    //
    // Never from an empty body: base64 of no bytes is the empty string, so the
    // two possibilities are identical and there is nothing to learn. Worth
    // stating because `looksLikeBase64('')` is false, which would otherwise read
    // as "this platform does not encode".
    if (typeof contentLength === 'number' && contentLength >= 0 && delivered.length > 0) {
      const rawFits = utf8Length(delivered) === contentLength;
      const base64Fits = couldBeBase64 && base64Length(delivered) === contentLength;
      // Only when exactly one fits. An empty body fits both, and says nothing.
      if (rawFits !== base64Fits) {
        platformEncodes = base64Fits;
      }
    }

    // Unknown counts as encoding, because on a platform that does, every body
    // looks like base64 and nothing decisive ever arrives.
    const asBase64 = couldBeBase64 && platformEncodes !== false;
    if (asBase64) {
      return {
        part: {data: delivered, type: 'base64'},
        size: base64Length(delivered),
        encoded: true,
      };
    }
    // The body itself. Correct for a text body, which is what almost every
    // `fetch(...).json()` is; a binary body was already damaged before it
    // reached here, because it crossed into JavaScript as a string.
    return {
      part: {data: delivered, type: 'string'},
      size: utf8Length(delivered),
      encoded: false,
    };
  };
}
