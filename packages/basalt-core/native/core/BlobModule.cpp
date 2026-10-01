#include "BlobModule.h"

#include "BlobRegistry.h"

#include <glog/logging.h>

#include <array>
#include <cstdint>

namespace basalt {

using facebook::jsi::Array;
using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

namespace {

// A blob handle as JavaScript passes it: {blobId, offset, size}.
struct BlobHandle {
  std::string blobId;
  size_t offset{0};
  size_t size{0};
};

BlobHandle readHandle(Runtime &rt, const Object &blob) {
  BlobHandle handle;
  handle.blobId = blob.getProperty(rt, "blobId").asString(rt).utf8(rt);
  const Value offset = blob.getProperty(rt, "offset");
  const Value size = blob.getProperty(rt, "size");
  handle.offset = offset.isNumber() ? static_cast<size_t>(offset.asNumber()) : 0;
  handle.size = size.isNumber() ? static_cast<size_t>(size.asNumber()) : 0;
  return handle;
}

// A promise already settled, since the bytes never left memory.
//
// Through JavaScript's own `Promise.resolve` rather than through jsi's
// AsyncPromise: this needs no thread hop and no invoker, and borrowing the
// machinery for one would only add a way for it to be wrong.
Value resolved(Runtime &rt, Value &&value) {
  auto promise = rt.global().getPropertyAsObject(rt, "Promise");
  return promise.getPropertyAsFunction(rt, "resolve").callWithThis(rt, promise, value);
}

Value rejected(Runtime &rt, const std::string &message) {
  auto promise = rt.global().getPropertyAsObject(rt, "Promise");
  auto error = rt.global()
                   .getPropertyAsFunction(rt, "Error")
                   .callAsConstructor(rt, String::createFromUtf8(rt, message));
  return promise.getPropertyAsFunction(rt, "reject").callWithThis(rt, promise, error);
}

} // namespace

namespace detail {

std::string base64Encode(const std::string &bytes) {
  static constexpr std::string_view kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);

  size_t i = 0;
  while (i + 2 < bytes.size()) {
    const uint32_t triple = (static_cast<unsigned char>(bytes[i]) << 16) |
        (static_cast<unsigned char>(bytes[i + 1]) << 8) |
        static_cast<unsigned char>(bytes[i + 2]);
    out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
    out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
    out.push_back(kAlphabet[triple & 0x3F]);
    i += 3;
  }

  // The tail, padded. Getting this wrong is the classic base64 bug and it only
  // shows on inputs whose length is not a multiple of three.
  if (i < bytes.size()) {
    const size_t remaining = bytes.size() - i;
    uint32_t triple = static_cast<unsigned char>(bytes[i]) << 16;
    if (remaining == 2) {
      triple |= static_cast<unsigned char>(bytes[i + 1]) << 8;
    }
    out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
    out.push_back(remaining == 2 ? kAlphabet[(triple >> 6) & 0x3F] : '=');
    out.push_back('=');
  }

  return out;
}

// The inverse, for the `base64` part type below.
//
// Skips anything outside the alphabet rather than rejecting it, which covers
// the padding and any newlines a transport introduced. A truncated group -- one
// leftover character, which encodes nothing -- is dropped.
std::string base64Decode(const std::string &text) {
  static constexpr std::string_view kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string out;
  out.reserve((text.size() / 4) * 3);

  uint32_t group = 0;
  int digits = 0;
  for (const char c : text) {
    const size_t value = kAlphabet.find(c);
    if (value == std::string_view::npos) {
      continue;
    }
    group = (group << 6) | static_cast<uint32_t>(value);
    if (++digits == 4) {
      out.push_back(static_cast<char>((group >> 16) & 0xFF));
      out.push_back(static_cast<char>((group >> 8) & 0xFF));
      out.push_back(static_cast<char>(group & 0xFF));
      group = 0;
      digits = 0;
    }
  }

  // A trailing group of two digits carries one byte, three carries two.
  if (digits == 2) {
    out.push_back(static_cast<char>((group >> 4) & 0xFF));
  } else if (digits == 3) {
    out.push_back(static_cast<char>((group >> 10) & 0xFF));
    out.push_back(static_cast<char>((group >> 2) & 0xFF));
  }

  return out;
}

} // namespace detail

// ---------------------------------------------------------------------------
// BlobModule
// ---------------------------------------------------------------------------

Object DesktopBlobModule::getConstants(Runtime &rt) {
  Object constants(rt);
  // `blob:`, which is what the web and iOS use. Android says `content:` because
  // its blobs are served through a ContentProvider; nothing here is.
  //
  // BLOB_URI_HOST is deliberately absent: URL.js only prefixes a host when this
  // is a string, and a blob URL with an empty authority -- `blob://` -- is a
  // worse thing to hand anybody than one without.
  constants.setProperty(rt, "BLOB_URI_SCHEME", String::createFromUtf8(rt, "blob"));
  return constants;
}

void DesktopBlobModule::createFromParts(Runtime &rt, Array parts, String blobId) {
  std::string bytes;

  const size_t count = parts.size(rt);
  for (size_t i = 0; i < count; i++) {
    Object part = parts.getValueAtIndex(rt, i).asObject(rt);
    const std::string type = part.getProperty(rt, "type").asString(rt).utf8(rt);

    if (type == "string") {
      bytes += part.getProperty(rt, "data").asString(rt).utf8(rt);
      continue;
    }

    // Not one of React Native's own part types. `fetch` on this platform routes
    // a `responseType: 'blob'` response through base64 -- see
    // src/overrides/setUpXHR.js -- because ReactCxxPlatform's NetworkingModule
    // delivers every body as a JavaScript string, and a string cannot carry
    // arbitrary bytes intact. Base64 can, and this is where it is turned back
    // into bytes.
    if (type == "base64") {
      bytes += detail::base64Decode(part.getProperty(rt, "data").asString(rt).utf8(rt));
      continue;
    }

    if (type == "blob") {
      const BlobHandle handle = readHandle(rt, part.getProperty(rt, "data").asObject(rt));
      if (auto slice = blobSlice(handle.blobId, handle.offset, handle.size)) {
        bytes += *slice;
      } else {
        // A part naming a blob that has been released. Skipping it makes the
        // result quietly short; saying so at least leaves a trace, and throwing
        // would take down a Blob constructor that JavaScript considers valid.
        LOG(WARNING) << "blob part refers to released blob " << handle.blobId;
      }
      continue;
    }

    LOG(WARNING) << "unknown blob part type '" << type << "'";
  }

  storeBlob(blobId.utf8(rt), std::move(bytes));
}

void DesktopBlobModule::release(Runtime &rt, String blobId) {
  releaseBlob(blobId.utf8(rt));
}

void DesktopBlobModule::addNetworkingHandler(Runtime &rt) {
  (void)rt;
  // On iOS and Android this registers a handler with the networking module so
  // that a request body of `{blob: ...}` is sent as bytes and a response with
  // `responseType: 'blob'` comes back as one.
  //
  // ReactCxxPlatform's NetworkingModule has no such hook -- it does not mention
  // blobs at all -- so there is nothing to register with. Request bodies still
  // work, because core/HttpClient.cpp resolves a blob body through the registry
  // itself; `responseType: 'blob'` does not, and cannot until upstream's
  // networking module grows a seam.
  //
  // Silent rather than warning: JavaScript calls this once at startup on every
  // platform, and a warning every app sees and nobody can act on is noise.
}

void DesktopBlobModule::addWebSocketHandler(Runtime &rt, double id) {
  (void)rt;
  (void)id;
}

void DesktopBlobModule::removeWebSocketHandler(Runtime &rt, double id) {
  (void)rt;
  (void)id;
}

void DesktopBlobModule::sendOverSocket(Runtime &rt, Object blob, double socketID) {
  (void)blob;
  (void)socketID;
  (void)rt;
  // Sending a blob over a websocket needs the socket, and WebSocketModule is
  // ReactCxxPlatform's rather than ours. Unlike the handlers above this one is
  // a call an app makes deliberately, so it says so rather than vanishing.
  LOG(WARNING) << "sendOverSocket is not implemented: binary websocket frames "
                  "need a hook ReactCxxPlatform's WebSocketModule does not have";
}

// ---------------------------------------------------------------------------
// FileReaderModule
// ---------------------------------------------------------------------------

Value DesktopFileReaderModule::readAsDataURL(Runtime &rt, Object blob) {
  const BlobHandle handle = readHandle(rt, blob);
  auto slice = blobSlice(handle.blobId, handle.offset, handle.size);
  if (!slice) {
    return rejected(rt, "Unable to resolve data for blob: " + handle.blobId);
  }

  // The type is on the handle, not in the registry: two Blobs can share bytes
  // and disagree about what they are.
  std::string type;
  const Value typeValue = blob.getProperty(rt, "type");
  if (typeValue.isString()) {
    type = typeValue.asString(rt).utf8(rt);
  }
  if (type.empty()) {
    // What the web does for a Blob with no type, and what iOS does here.
    type = "application/octet-stream";
  }

  const std::string url = "data:" + type + ";base64," + detail::base64Encode(*slice);
  return resolved(rt, Value(String::createFromUtf8(rt, url)));
}

Value DesktopFileReaderModule::readAsText(Runtime &rt, Object blob, String encoding) {
  const BlobHandle handle = readHandle(rt, blob);
  auto slice = blobSlice(handle.blobId, handle.offset, handle.size);
  if (!slice) {
    return rejected(rt, "Unable to resolve data for blob: " + handle.blobId);
  }

  // Only UTF-8. React Native's JavaScript defaults to it and nothing in the
  // library asks for another, but an app can, and a wrong answer that looks
  // right is worse than a rejection that names the encoding.
  const std::string requested = encoding.utf8(rt);
  if (!requested.empty() && requested != "UTF-8" && requested != "utf-8" &&
      requested != "utf8") {
    return rejected(rt, "Unsupported encoding for readAsText: " + requested);
  }

  return resolved(rt, Value(String::createFromUtf8(rt, *slice)));
}

} // namespace basalt
