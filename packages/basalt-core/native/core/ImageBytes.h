// Reading the bytes behind an <Image> source.
//
// Not platform-specific, which is why it is here. A URI is a URI: `file://`,
// a bare path, `data:` and `http(s)` mean the same things on every desktop, and
// the only part of image loading that differs is decoding the bytes into
// whatever the toolkit paints -- a GdkTexture on GTK, a CGImage on AppKit.
//
// Written second, which is the useful part: this began as private code inside
// GtkImageLoader, and porting <Image> to macOS was the point at which "the same
// thing twice" became visible.

#pragma once

#include <functional>
#include <string>

namespace basalt {

// Schemes understood:
//   file://       read from disk, percent-decoded
//   data:         base64 payloads only
//   http, https   fetched with libcurl
//   bare paths    treated as file paths, which is what a `require()`d asset
//                 looks like once Metro has resolved it in a release bundle
//
// How much of an http fetch has arrived, which is what `<Image onProgress>`
// reports. `total` is zero when the server sent no `Content-Length`, which is
// not an error and is why it is passed on rather than turned into a fraction
// here: an app showing a bar needs to know the difference between "half way"
// and "no idea".
//
// Called from the fetching thread, repeatedly, and never after the fetch has
// returned. Only an http or https URI reports anything: a file or a data URI
// arrives whole, and inventing a 0% and a 100% for them would be reporting
// progress that never existed.
using ImageProgress = std::function<void(long long loaded, long long total)>;

// Blocking, so call it off the thread that draws. Returns false and fills
// `error` on failure, in which case `out` is left empty.
bool fetchImageBytes(const std::string &uri,
                     std::string *out,
                     std::string *error,
                     const ImageProgress &onProgress = {});

} // namespace basalt
