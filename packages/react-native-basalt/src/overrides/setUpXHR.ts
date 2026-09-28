/**
 * React Native's XHR/fetch globals, with one of them repaired.
 *
 * This file exists for a single defect, and everything else in it is a copy of
 * React Native's own `Libraries/Core/setUpXHR.js`.
 *
 * ## The defect
 *
 * `fetch` is whatwg-fetch, and whatwg-fetch sets `xhr.responseType = 'blob'`
 * on *every* request whenever `Blob` and `FileReader` are globals -- which they
 * are here, since phase 31. React Native's XMLHttpRequest then asks the
 * platform for a `blob` response and expects `{blobId, offset, size}` back.
 *
 * ReactCxxPlatform's NetworkingModule has no blob case at all: its
 * `encodeResponseBody` handles `base64` and returns every other body as a
 * string. So the response arrives as a string, and the `response` getter throws
 *
 *     Invalid response for blob - expecting object, was string: ...
 *
 * before whatwg-fetch has even built a Response. That is not a failure mode of
 * `.blob()` -- it takes down `.text()` and `.json()` too, on a 200, in release
 * builds as much as in development. Every `fetch` in an app, in other words.
 * Supplying `Blob` is what triggered it: the polyfill upgraded itself onto a
 * path the platform had never implemented.
 *
 * ## The repair
 *
 * A `blob` response type is served through `base64`, which upstream *does*
 * implement and which is the only response type that survives the trip
 * intact -- a JavaScript string cannot carry arbitrary bytes, so `text` would
 * corrupt anything that is not UTF-8, and corrupting binary downloads silently
 * would be worse than the throw this replaces.
 *
 * So: ask for `arraybuffer` (which the XHR maps to a native `base64` request),
 * re-encode those bytes as base64, and hand them to BlobModule's `base64` part
 * type -- which core/BlobModule.cpp decodes back to the original bytes. The
 * round trip is lossless, and `.blob()` on a PNG gives back that PNG.
 *
 * The proper fix is upstream: a blob case in ReactCxxPlatform's
 * NetworkingModule, at which point this file can go. NetworkingModule is a CRTP
 * TurboModule spec whose delivery method is private and non-virtual, so it
 * cannot be subclassed from here -- replacing it wholesale would mean
 * reimplementing every method to change one.
 */

'use strict';

const {polyfillGlobal} = require('react-native-basalt/upstream/Libraries/Utilities/PolyfillFunctions');
const base64 = require('base64-js');
const {createResponseBodyReader} = require('../responseBody');

// One reader for the process: what it learns about this platform on the first
// decisive body applies to every response after it.
const readResponseBody = createResponseBodyReader();

let warnedAboutUnencodedBodies = false;

/**
 * Said once, because it is a property of the build rather than of a request.
 *
 * A text body is still correct -- that is the common case and why this is a
 * warning rather than an error -- but a binary one crossed into JavaScript as a
 * string and cannot be recovered here.
 */
function warnOnceAboutUnencodedBodies(): void {
  if (warnedAboutUnencodedBodies) {
    return;
  }
  warnedAboutUnencodedBodies = true;
  console.warn(
    "basalt: this React Native's ReactCxxPlatform delivers response bodies " +
      'unencoded even when base64 was asked for, so a binary body read through ' +
      '`.blob()` or `.arrayBuffer()` will be damaged. Text bodies, including ' +
      'every `.json()`, are unaffected. Fixed upstream after 0.86 by ' +
      "NetworkingModule's encodeResponseBody; see src/responseBody.ts.",
  );
}

const BaseXMLHttpRequest = require('react-native-basalt/upstream/Libraries/Network/XMLHttpRequest').default;
const BlobManager = require('react-native-basalt/upstream/Libraries/Blob/BlobManager').default;
const NativeBlobModule = require('react-native-basalt/upstream/Libraries/Blob/NativeBlobModule').default;

// Same shape as the one inside BlobManager, which is not exported.
function uuidv4() {
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => {
    const r = (Math.random() * 16) | 0;
    const v = c === 'x' ? r : (r & 0x3) | 0x8;
    return v.toString(16);
  });
}

// Patched on the prototype rather than in a subclass.
//
// A subclass only fixes the class it is: anything that imports
// `Libraries/Network/XMLHttpRequest` directly rather than reading the global
// gets the unpatched one, and the two are indistinguishable until a response
// arrives. Patching the prototype fixes every instance however it was made.
const proto = BaseXMLHttpRequest.prototype;

/**
 * The accessors this file wraps, on React Native's own XMLHttpRequest.
 *
 * A getter is required and a setter is not, which is not laxness: `response`
 * is read-only on every XMLHttpRequest, including React Native's, so demanding
 * a setter for it fails on a perfectly correct one. That mistake was made here
 * once and only the running app found it -- the types were happy either way.
 *
 * A missing *getter* is a different thing: it means this override no longer
 * matches the React Native it is shadowing, which is worth failing loudly on
 * rather than quietly not wrapping.
 */
function getterOf(name: string): () => unknown {
  const descriptor = Object.getOwnPropertyDescriptor(proto, name);
  if (descriptor?.get == null) {
    throw new Error(
      `react-native-basalt: XMLHttpRequest has no ${name} getter to wrap; ` +
        'this override no longer matches this React Native.',
    );
  }
  return descriptor.get;
}

function setterOf(name: string): (value: unknown) => void {
  const descriptor = Object.getOwnPropertyDescriptor(proto, name);
  if (descriptor?.set == null) {
    throw new Error(
      `react-native-basalt: XMLHttpRequest has no ${name} setter to wrap; ` +
        'this override no longer matches this React Native.',
    );
  }
  return descriptor.set;
}

const baseResponseType = {get: getterOf('responseType'), set: setterOf('responseType')};
const baseResponse = {get: getterOf('response')};

Object.defineProperty(proto, 'responseType', {
  configurable: true,
  get() {
    // The caller asked for a blob and should be told it is getting one, even
    // though `arraybuffer` is what was really requested.
    return this.__rnbBlobRequested === true ? 'blob' : baseResponseType.get.call(this);
  },
  set(responseType) {
    const wantsBlob = responseType === 'blob';
    this.__rnbBlobRequested = wantsBlob;
    this.__rnbBlob = undefined;
    // Everything other than 'blob' is passed straight through, so this changes
    // the behaviour of exactly one response type.
    baseResponseType.set.call(this, wantsBlob ? 'arraybuffer' : responseType);
  },
});

Object.defineProperty(proto, 'response', {
  configurable: true,
  get() {
    if (this.__rnbBlobRequested !== true) {
      return baseResponse.get.call(this);
    }
    if (this.__rnbBlob !== undefined) {
      return this.__rnbBlob;
    }

    // Deliberately not `baseResponse.get`: that getter switches on the private
    // `_responseType`, and going through it again is what this is working
    // around. `_response` is the base64 text the platform delivered, because
    // the setter above asked for `arraybuffer` -- so the bytes are the
    // server's, exactly, and decoding them here needs nothing from the base
    // class.
    const encoded = this._response;
    if (typeof encoded !== 'string' || encoded === '' || !NativeBlobModule) {
      this.__rnbBlob = BlobManager.createFromParts([]);
      return this.__rnbBlob;
    }

    // Whether that string is base64 is not something to assume: the encoding
    // this asked for is only applied by ReactCxxPlatform after 0.86, and the
    // version in use is the app's. See src/responseBody.ts, and the failure it
    // was written for.
    const {part, size, encoded: wasEncoded} = readResponseBody(encoded);
    if (!wasEncoded) {
      warnOnceAboutUnencodedBodies();
    }

    const blobId = uuidv4();
    NativeBlobModule.createFromParts([part], blobId);
    this.__rnbBlob = BlobManager.createFromOptions({
      blobId,
      offset: 0,
      size,
      type: this.getResponseHeader('content-type') ?? '',
    });
    return this.__rnbBlob;
  },
});

polyfillGlobal('XMLHttpRequest', () => BaseXMLHttpRequest);
polyfillGlobal('FormData', () => require('react-native-basalt/upstream/Libraries/Network/FormData').default);

polyfillGlobal('fetch', () => require('react-native-basalt/upstream/Libraries/Network/fetch').fetch);
polyfillGlobal('Headers', () => require('react-native-basalt/upstream/Libraries/Network/fetch').Headers);
polyfillGlobal('Request', () => require('react-native-basalt/upstream/Libraries/Network/fetch').Request);
polyfillGlobal('Response', () => require('react-native-basalt/upstream/Libraries/Network/fetch').Response);
polyfillGlobal('WebSocket', () => require('react-native-basalt/upstream/Libraries/WebSocket/WebSocket').default);
polyfillGlobal('Blob', () => require('react-native-basalt/upstream/Libraries/Blob/Blob').default);
polyfillGlobal('File', () => require('react-native-basalt/upstream/Libraries/Blob/File').default);
polyfillGlobal('FileReader', () => require('react-native-basalt/upstream/Libraries/Blob/FileReader').default);
polyfillGlobal('URL', () => require('react-native-basalt/upstream/Libraries/Blob/URL').URL);
polyfillGlobal('URLSearchParams', () => require('react-native-basalt/upstream/Libraries/Blob/URL').URLSearchParams);
// The abort API moved into React Native's own source in 0.87; 0.86 polyfills
// it from the `abort-controller` package, which exports `AbortSignal` where
// React Native's exports `AbortSignal_public`. The resolver sends both
// specifiers to whichever exists -- see UPSTREAM_FALLBACKS in metro-config --
// and this takes whichever name came back, because a missing module is a
// bundling error that no try/catch here could have caught anyway.
polyfillGlobal(
  'AbortController',
  () =>
    require('react-native-basalt/upstream/src/private/webapis/dom/abort-api/AbortController')
      .AbortController,
);
polyfillGlobal('AbortSignal', () => {
  const module = require(
    'react-native-basalt/upstream/src/private/webapis/dom/abort-api/AbortSignal',
  );
  return module.AbortSignal_public ?? module.AbortSignal;
});
