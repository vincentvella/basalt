// The bytes behind a Blob.
//
// React Native's `Blob` is a handle, not data: JavaScript holds
// `{blobId, offset, size}` and the bytes live natively, so that a 20MB file
// picked by a user is never copied into the JavaScript heap. This is where they
// live.
//
// Entirely portable -- a blob is a byte array with a name, on every platform --
// which is why it is here and not behind a seam. The only part of blobs that
// touches an operating system is sending one as an HTTP body, and that is in
// HttpClient.cpp, which is also shared.

#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace basalt {

// Stores `bytes` under `blobId`, replacing anything already there.
void storeBlob(const std::string &blobId, std::string bytes);

// A slice of a stored blob, or nullopt if the id is unknown.
//
// The range is clamped rather than rejected: JavaScript computes offsets from
// sizes it was told, and a rounding disagreement should produce a short read
// rather than an exception three layers from the cause.
std::optional<std::string> blobSlice(const std::string &blobId, size_t offset, size_t size);

// The whole of a stored blob, or nullopt if the id is unknown.
std::optional<std::string> blobBytes(const std::string &blobId);

size_t blobSize(const std::string &blobId);

// `BlobModule.release`. JavaScript owns the lifetime: a Blob that goes out of
// scope there has to say so, because nothing here can tell.
void releaseBlob(const std::string &blobId);

// How many blobs are held. For tests and for a future leak check -- nothing
// evicts on its own, by design.
size_t blobCount();

} // namespace basalt
