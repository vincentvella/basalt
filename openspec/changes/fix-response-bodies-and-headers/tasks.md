# Tasks

## 1. The body

- [x] 1.1 `src/responseBody.ts`: decide rather than assume, and say why the
      decision cannot be made from content alone -- `null`, `true` and `1234`
      are all valid base64.
- [x] 1.2 Content-Length settles it exactly where there is one; a chunked
      response has none, so the learned verdict still carries those.
- [x] 1.3 Tests for both platforms' behaviour, including the empty body, which
      is base64 of nothing *and* nothing and must teach neither.

## 2. The headers

- [x] 2.1 `src/responseHeaders.ts`: pairs to an object, duplicates joined, two
      casings of one name treated as one.
- [x] 2.2 Wrapped at `setResponseHeaders`, which is the single funnel every
      reader goes through -- `fetch`'s included, and it never touches the XHR.
- [x] 2.3 Tests, including a malformed pair, because this parses something that
      crossed a bridge.

## 3. What is still open

- [ ] 3.1 An end-to-end scenario for either. Both were found in an app and fixed
      against one; this repository's own harness does not serve a request to
      itself, so nothing here would notice a regression.
- [ ] 3.2 `ExpoFetchModule` itself; see the proposal.
