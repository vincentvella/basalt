#import "AppKitImageLoader.h"

#include "ImageBytes.h"

#import <ImageIO/ImageIO.h>

#include <thread>
#include <utility>

namespace basalt {

// Carries one completed load from the worker thread to the main thread.
struct AppKitImageLoader::Pending {
  AppKitImageLoader *loader;
  std::string uri;
  CGImageRef image;
  std::string error;
  Callback callback;
};

AppKitImageLoader::AppKitImageLoader() = default;

AppKitImageLoader::~AppKitImageLoader() {
  for (auto &[uri, image] : cache_) {
    (void)uri;
    CGImageRelease(image);
  }
  cache_.clear();
}

namespace {

// ImageIO rather than NSImage. An NSImage is a list of representations at
// different sizes with a resolution attached, and asking one for a CGImage
// means telling it a size and a context -- so a 160x100 PNG can come back
// 320x200 on a Retina display, which then measures wrong. CGImageSource hands
// back exactly what is in the file.
CGImageRef decode(const std::string &bytes, std::string *error) {
  if (bytes.empty()) {
    *error = "no image data";
    return nullptr;
  }

  CFDataRef data = CFDataCreate(kCFAllocatorDefault,
                                reinterpret_cast<const UInt8 *>(bytes.data()),
                                static_cast<CFIndex>(bytes.size()));
  if (data == nullptr) {
    *error = "could not wrap image data";
    return nullptr;
  }

  CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
  CFRelease(data);
  if (source == nullptr) {
    *error = "unrecognised image format";
    return nullptr;
  }

  // Index 0: an animated GIF and a multi-page TIFF both have more, and drawing
  // the first frame is the honest thing to do until there is an animator. See
  // docs/BACKLOG.md.
  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (image == nullptr) {
    *error = "could not decode image";
    return nullptr;
  }
  return image;
}

} // namespace

// Caches the image and releases whatever that pushed out. See core/ImageCache.h
// for the policy; the bytes are this file's to measure, because only Core
// Graphics knows how big a decoded image is.
void AppKitImageLoader::remember(const std::string &uri, CGImageRef image) {
  cache_[uri] = image;

  // Height times the row stride, which is what the decode actually allocated
  // -- rows are padded, so width times four is an underestimate on some
  // widths and this is not.
  const size_t bytes = CGImageGetBytesPerRow(image) * CGImageGetHeight(image);

  for (const std::string &evicted : policy_.insert(uri, bytes)) {
    const auto it = cache_.find(evicted);
    if (it != cache_.end()) {
      CGImageRelease(it->second);
      cache_.erase(it);
    }
  }
}

void AppKitImageLoader::load(const std::string &uri, Callback &&callback) {
  if (uri.empty()) {
    callback(nullptr, "empty source uri");
    return;
  }

  if (const auto it = cache_.find(uri); it != cache_.end()) {
    policy_.noteUse(uri);
    callback(it->second, {});
    return;
  }

  auto *pending = new Pending{this, uri, nullptr, {}, std::move(callback)};

  std::thread([pending]() {
    std::string bytes;
    // Both the fetch and the decode run here. A CGImage is immutable and
    // thread-safe, so unlike the GTK side there is nothing that has to wait for
    // the main thread except the delivery itself.
    if (fetchImageBytes(pending->uri, &bytes, &pending->error)) {
      pending->image = decode(bytes, &pending->error);
    }

    dispatch_async_f(dispatch_get_main_queue(), pending, [](void *data) {
      std::unique_ptr<Pending> done{static_cast<Pending *>(data)};
      if (done->image != nullptr) {
        // The cache takes the reference the decode produced; the callback
        // borrows it, and a view that keeps the image retains its own.
        done->loader->remember(done->uri, done->image);
      }
      done->callback(done->image, done->error);
    });
  }).detach();
}


void AppKitImageLoader::loadImage(const std::string &uri,
                                  const facebook::react::IImageLoaderOnLoadCallback &&onLoad) {
  // Copied because the callback outlives this call: `load` answers on a later
  // turn of the main queue, and the reference it was handed is gone by then.
  auto callback = onLoad;
  load(uri, [callback](CGImageRef image, const std::string &error) {
    if (image == nullptr) {
      // The message rather than a size: `Image.getSize` rejects with it, and a
      // zero-by-zero success would be indistinguishable from a 0x0 image.
      callback(0.0, 0.0, error.empty() ? "could not load image" : error.c_str());
      return;
    }
    callback(static_cast<double>(CGImageGetWidth(image)),
             static_cast<double>(CGImageGetHeight(image)),
             nullptr);
  });
}

facebook::react::IImageLoader::CacheStatus AppKitImageLoader::getCacheStatus(
    const std::string &uri) {
  // Memory or nothing: there is no disk cache here, and saying `Disk` would be
  // claiming a persistence this has not got.
  return cache_.find(uri) != cache_.end()
      ? facebook::react::IImageLoader::CacheStatus::Memory
      : facebook::react::IImageLoader::CacheStatus::None;
}

} // namespace basalt
