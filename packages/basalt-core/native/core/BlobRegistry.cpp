#include "BlobRegistry.h"

// For std::min, which libstdc++ does not promise through <string>.
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace basalt {

namespace {

struct Registry {
  std::mutex mutex;
  std::unordered_map<std::string, std::string> blobs;
};

Registry &registry() {
  static Registry instance;
  return instance;
}

} // namespace

void storeBlob(const std::string &blobId, std::string bytes) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.blobs[blobId] = std::move(bytes);
}

std::optional<std::string> blobSlice(const std::string &blobId, size_t offset, size_t size) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);

  const auto it = state.blobs.find(blobId);
  if (it == state.blobs.end()) {
    return std::nullopt;
  }

  const std::string &bytes = it->second;
  if (offset >= bytes.size()) {
    return std::string{};
  }
  return bytes.substr(offset, std::min(size, bytes.size() - offset));
}

std::optional<std::string> blobBytes(const std::string &blobId) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  const auto it = state.blobs.find(blobId);
  if (it == state.blobs.end()) {
    return std::nullopt;
  }
  return it->second;
}

size_t blobSize(const std::string &blobId) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  const auto it = state.blobs.find(blobId);
  return it == state.blobs.end() ? 0 : it->second.size();
}

void releaseBlob(const std::string &blobId) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.blobs.erase(blobId);
}

size_t blobCount() {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  return state.blobs.size();
}

} // namespace basalt
