# A response arrives as the server sent it

## Why

Every call kino made to its own daemon failed with `JSON Parse error: Unexpected
character: v` -- against a server that answered the same request correctly to
curl, and with a `v` that appears nowhere in any response it sends.

Two defects, both in how a response crosses into JavaScript, and both invisible
until an app read a response properly:

**The body.** `fetch` asks for a blob response whenever `FileReader` and `Blob`
exist, which is every Expo app. This platform serves that by asking React Native
for `arraybuffer`, which becomes a native request for base64 -- the only encoding
ReactCxxPlatform has that carries arbitrary bytes through a JavaScript string
intact. But `encodeResponseBody` only landed upstream after 0.86, and
ReactCxxPlatform is fetched at the app's own version, so on a supported React
Native the body comes back unencoded and was base64-*decoded* anyway. A 98-byte
JSON body became one byte.

**The headers.** They crossed intact and in the wrong shape: ReactCxxPlatform
sends `std::vector<std::pair<std::string, std::string>>`, which bridges to an
array of pairs, and React Native's XHR does `Object.keys` on it expecting an
object. Nothing threw. `getResponseHeader('content-type')` answered null on a
response that plainly had one, every `fetch(...).headers.get(...)` answered null,
and `getAllResponseHeaders()` returned lines reading `0: Server,BaseHTTP/0.6`.

## What Changes

- A blob response carries the bytes the server sent, on every supported React
  Native, whether or not that version encodes them.
- Response headers are readable, by every reader: the XHR's, `fetch`'s, and the
  `type` a Blob built from a response carries.

## Capabilities

### Modified Capabilities
- `blobs-and-networking`

## Impact

- `src/responseBody.ts`: whether the delivered string is the body or its base64,
  decided per platform rather than assumed.
- `src/responseHeaders.ts`: the shape conversion.
- `src/overrides/setUpXHR.ts`: both, applied where the response is built.

## Not built

- **`ExpoFetchModule`.** Expo's own `fetch` is routed around rather than ported:
  the platform sets expo's `EXPO_PUBLIC_USE_RN_FETCH` before the bundle runs, so
  React Native's fetch -- which works here -- stays. Porting it means a pair of
  SharedObject-derived native classes with per-instance events and a streamed
  body. Until then `expo/fetch`, imported directly, reports the module missing,
  and a response body is buffered rather than streamed.
- **A binary body on a React Native that does not encode.** It crossed into
  JavaScript as a string before anything here could see it, and is not
  recoverable. Said once, in the log, rather than silently.
