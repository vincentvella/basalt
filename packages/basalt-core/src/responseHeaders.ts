/**
 * The response headers, in the shape React Native's XMLHttpRequest expects.
 *
 * ReactCxxPlatform's NetworkingModule holds them as
 * `std::vector<std::pair<std::string, std::string>>` and bridges that straight
 * to JavaScript, which makes an array of two-element arrays:
 *
 *     [["Content-Type", "application/json"], ["Content-Length", "580"]]
 *
 * React Native's XHR expects an object. `setResponseHeaders` does
 * `Object.keys(headers)` to build its lower-cased lookup, so an array gives it
 * the keys "0", "1", "2" and values that are arrays. Nothing throws: the headers
 * are all there, correctly, and every lookup misses.
 *
 * What that looked like: `getResponseHeader('content-type')` null on a response
 * that plainly had one, `fetch(...).headers.get(...)` null for everything, and
 * `getAllResponseHeaders()` returning lines like `0: Server,BaseHTTP/0.6`. So a
 * `Response` carried no content type, which is also why a Blob built from one
 * had `type: ''`.
 *
 * Converted here rather than reported, because nothing about it is ambiguous and
 * no app can act on it.
 *
 * **Duplicates are joined with ", "**, which is what the XHR specification says
 * `getResponseHeader` returns for a header that appeared more than once, and
 * what iOS and Android already hand over -- they build the object natively. The
 * one header this is wrong for is `Set-Cookie`, which may not be joined; it is
 * also the one header XHR refuses to return at all, so nothing here can reach
 * it either way.
 */

export type DeliveredHeaders = ReadonlyArray<readonly [string, string]> | Record<string, string>;

/** True for the array-of-pairs shape ReactCxxPlatform sends. */
export function isHeaderPairs(value: unknown): value is ReadonlyArray<readonly [string, string]> {
  return Array.isArray(value);
}

/**
 * An object of header names to values. Anything already an object is passed
 * through untouched, so a ReactCxxPlatform that starts sending one needs no
 * change here.
 */
export function headersToObject(delivered: unknown): Record<string, string> | null {
  if (delivered == null) {
    return null;
  }
  if (!isHeaderPairs(delivered)) {
    return delivered as Record<string, string>;
  }

  const headers: Record<string, string> = {};
  for (const pair of delivered) {
    // Defensive about the pair rather than the array: this is parsing something
    // that crossed a bridge, and a malformed entry should be skipped rather than
    // become the string "undefined".
    if (!Array.isArray(pair) || pair.length < 2) {
      continue;
    }
    const [name, value] = pair;
    if (typeof name !== 'string' || typeof value !== 'string') {
      continue;
    }
    // Case-insensitively the same header, so that a server sending `Vary` and
    // `vary` produces one entry rather than two that shadow each other.
    const existing = Object.keys(headers).find(
      key => key.toLowerCase() === name.toLowerCase(),
    );
    if (existing === undefined) {
      headers[name] = value;
    } else {
      headers[existing] = `${headers[existing]}, ${value}`;
    }
  }
  return headers;
}
