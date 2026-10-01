// `BlobModule` and `FileReaderModule`: what `Blob`, `File` and `FileReader`
// stand on.
//
// A stock Expo app asks for BlobModule during startup -- `fetch` and
// `XMLHttpRequest` both register a blob handler if one exists -- and phase 29
// found it was the one missing module a real app would notice. Anything
// fetching binary data needs it.
//
// Both modules are portable in full. A blob is a byte array with a name on
// every platform, and reading one as text or as a data URL is arithmetic. The
// bytes live in BlobRegistry.h; these two are the JavaScript-facing half.

#pragma once

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>

#include <string>

namespace basalt {

// base64, both ways.
//
// Exposed rather than kept file-local because every `fetch` on this platform
// now goes through the decoder -- see src/overrides/setUpXHR.js -- and because
// the padded tail is the classic base64 bug, which only shows on inputs whose
// length is not a multiple of three. native/tests/test_blobs.cpp covers it.
namespace detail {
std::string base64Encode(const std::string &bytes);
std::string base64Decode(const std::string &text);
} // namespace detail

class DesktopBlobModule : public facebook::react::NativeBlobModuleCxxSpec<DesktopBlobModule> {
 public:
  explicit DesktopBlobModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeBlobModuleCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "BlobModule";

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);

  // `parts` is an array of `{data, type}`: a `string` part carries its text,
  // and a `blob` part carries another blob's `{blobId, offset, size}`. The
  // concatenation is stored under `blobId`.
  void createFromParts(facebook::jsi::Runtime &rt,
                       facebook::jsi::Array parts,
                       facebook::jsi::String blobId);

  void release(facebook::jsi::Runtime &rt, facebook::jsi::String blobId);

  // The three below are about wiring blobs into networking and websockets, and
  // none of them can do anything here. See the .cpp for why, which is a
  // limitation of ReactCxxPlatform rather than of this platform.
  void addNetworkingHandler(facebook::jsi::Runtime &rt);
  void addWebSocketHandler(facebook::jsi::Runtime &rt, double id);
  void removeWebSocketHandler(facebook::jsi::Runtime &rt, double id);
  void sendOverSocket(facebook::jsi::Runtime &rt, facebook::jsi::Object blob, double socketID);
};

class DesktopFileReaderModule
    : public facebook::react::NativeFileReaderModuleCxxSpec<DesktopFileReaderModule> {
 public:
  explicit DesktopFileReaderModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeFileReaderModuleCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "FileReaderModule";

  // Both take a blob handle -- `{blobId, offset, size}` -- and return a
  // promise, because that is what the spec says even though the bytes are
  // already in memory and the answer is immediate.
  facebook::jsi::Value readAsDataURL(facebook::jsi::Runtime &rt, facebook::jsi::Object blob);
  facebook::jsi::Value readAsText(facebook::jsi::Runtime &rt,
                                  facebook::jsi::Object blob,
                                  facebook::jsi::String encoding);
};

} // namespace basalt
