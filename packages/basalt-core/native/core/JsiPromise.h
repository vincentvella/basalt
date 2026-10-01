// Settled promises, built the only way a jsi host function can build one:
// through JavaScript's own `Promise`.
//
// Every asynchronous native API on this platform is synchronous underneath --
// reading the clipboard, registering a font, asking whether a URL can be opened
// are all immediate calls. The JavaScript signatures are async because they are
// async *somewhere*, usually on a platform where the work crosses a process
// boundary. So the honest implementation is to do the work and hand back a
// promise that is already settled, rather than to invent a thread.
//
// A header rather than a file because these are three lines each and the
// alternative -- a fourth copy in the next module -- is how the first three
// happened.

#pragma once

#include <jsi/jsi.h>

#include <string>

namespace basalt {

// `Promise.resolve(value)`.
inline facebook::jsi::Value resolved(facebook::jsi::Runtime &runtime,
                                     facebook::jsi::Value &&value) {
  return runtime.global()
      .getPropertyAsObject(runtime, "Promise")
      .getPropertyAsFunction(runtime, "resolve")
      .call(runtime, std::move(value));
}

// `Promise.resolve()`, for a function whose result is that it finished.
inline facebook::jsi::Value resolved(facebook::jsi::Runtime &runtime) {
  return resolved(runtime, facebook::jsi::Value::undefined());
}

// `Promise.reject(new Error(message))`. An Error rather than a string: a
// rejection reason that is not an Error loses its stack, and every JavaScript
// caller assumes `error.message`.
inline facebook::jsi::Value rejected(facebook::jsi::Runtime &runtime,
                                     const std::string &message) {
  facebook::jsi::Function error = runtime.global().getPropertyAsFunction(runtime, "Error");
  facebook::jsi::Value reason =
      error.callAsConstructor(runtime, facebook::jsi::String::createFromUtf8(runtime, message));
  return runtime.global()
      .getPropertyAsObject(runtime, "Promise")
      .getPropertyAsFunction(runtime, "reject")
      .call(runtime, std::move(reason));
}

} // namespace basalt
