#include "GtkImageLoader.h"

#include "ImageBytes.h"

#include <string>
#include <thread>
#include <utility>

namespace basalt {

// Carries one completed load from the worker thread to the main thread.
struct GtkImageLoader::Pending {
  GtkImageLoader *loader;
  std::string uri;
  std::string bytes;
  std::string error;
  Callback callback;
};

GtkImageLoader::GtkImageLoader() = default;

GtkImageLoader::~GtkImageLoader() {
  for (auto &[uri, texture] : cache_) {
    g_clear_object(&texture);
  }
  cache_.clear();
}

// Caches the texture and drops whatever that pushed out. See core/ImageCache.h
// for the policy; the bytes are this file's to measure, because only GDK knows
// how big a decoded texture is.
void GtkImageLoader::remember(const std::string &uri, GdkTexture *texture) {
  cache_[uri] = texture;

  // Four bytes a pixel, which is what GDK decodes to. Not asked of the texture
  // because `gdk_texture_get_format` reports the *source* format and the
  // memory is what matters here.
  const size_t bytes = static_cast<size_t>(gdk_texture_get_width(texture)) *
                       static_cast<size_t>(gdk_texture_get_height(texture)) * 4u;

  for (const std::string &evicted : policy_.insert(uri, bytes)) {
    const auto it = cache_.find(evicted);
    if (it != cache_.end()) {
      g_clear_object(&it->second);
      cache_.erase(it);
    }
  }
}

gboolean GtkImageLoader::deliver(gpointer data) {
  auto *pending = static_cast<Pending *>(data);

  GdkTexture *texture = nullptr;
  std::string error = pending->error;

  if (error.empty()) {
    // Decoding happens here rather than on the worker thread: GdkTexture is a
    // GObject, and creating one is only safe once, on the thread that will use
    // it. The expensive part -- the I/O -- already happened off-thread.
    GBytes *bytes = g_bytes_new(pending->bytes.data(), pending->bytes.size());
    GError *decodeError = nullptr;
    texture = gdk_texture_new_from_bytes(bytes, &decodeError);
    g_bytes_unref(bytes);
    if (texture == nullptr) {
      error = decodeError != nullptr ? decodeError->message : "could not decode image";
      g_clear_error(&decodeError);
    } else {
      pending->loader->remember(pending->uri, texture);
    }
  }

  pending->callback(texture, error);
  delete pending;
  return G_SOURCE_REMOVE;
}

void GtkImageLoader::load(const std::string &uri, Callback &&callback) {
  if (uri.empty()) {
    callback(nullptr, "empty source uri");
    return;
  }

  if (const auto it = cache_.find(uri); it != cache_.end()) {
    policy_.noteUse(uri);
    callback(it->second, {});
    return;
  }

  auto *pending = new Pending{this, uri, {}, {}, std::move(callback)};

  std::thread([pending]() {
    // The fetch is in core, shared with the AppKit loader: a URI means the same
    // thing on both, and only the decode below differs.
    fetchImageBytes(pending->uri, &pending->bytes, &pending->error);
    g_idle_add_full(G_PRIORITY_DEFAULT, deliver, pending, nullptr);
  }).detach();
}


void GtkImageLoader::loadImage(const std::string &uri,
                               const facebook::react::IImageLoaderOnLoadCallback &&onLoad) {
  // Copied because the callback outlives this call: `load` may answer on a
  // later turn of the main loop, and the reference it was handed is gone by
  // then.
  auto callback = onLoad;
  load(uri, [callback](GdkTexture *texture, const std::string &error) {
    if (texture == nullptr) {
      // The message rather than a size: `Image.getSize` rejects with it, and a
      // zero-by-zero success would be indistinguishable from a 0x0 image.
      callback(0.0, 0.0, error.empty() ? "could not load image" : error.c_str());
      return;
    }
    callback(static_cast<double>(gdk_texture_get_width(texture)),
             static_cast<double>(gdk_texture_get_height(texture)),
             nullptr);
  });
}

facebook::react::IImageLoader::CacheStatus GtkImageLoader::getCacheStatus(
    const std::string &uri) {
  // Memory or nothing: there is no disk cache here, and saying `Disk` would be
  // claiming a persistence this has not got.
  return cache_.find(uri) != cache_.end()
      ? facebook::react::IImageLoader::CacheStatus::Memory
      : facebook::react::IImageLoader::CacheStatus::None;
}

} // namespace basalt
