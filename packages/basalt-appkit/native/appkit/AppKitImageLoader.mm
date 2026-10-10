#import "AppKitImageLoader.h"

#include "ImageBytes.h"

#import <ImageIO/ImageIO.h>

#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace basalt {

// Carries one completed load from the worker thread to the main thread.
struct AppKitImageLoader::Pending {
  AppKitImageLoader *loader;
  std::string uri;
  CGImageRef image;
  // Every frame and its delay, for a file that has more than one. Decoded on
  // the worker thread with the first frame, because a CGImage is immutable and
  // the expensive part is the same work either way.
  std::vector<CGImageRef> frames;
  std::vector<unsigned> delaysMs;
  unsigned loopCount;
  std::string error;
  Callback callback;
  // Called from the worker thread as the bytes arrive, rather than from the
  // main-queue delivery below: see the header.
  basalt::ImageProgress onProgress;
};

AppKitImageLoader::AppKitImageLoader() = default;

AppKitImageLoader::~AppKitImageLoader() {
  for (auto &[uri, image] : cache_) {
    (void)uri;
    CGImageRelease(image);
  }
  cache_.clear();
  forgetAnimations();
}

// Releases every frame of every animation.
//
// Each frame carries two references: the one the decode produced, which this
// class owns, and the one the NSArray took when the frame went in. So both have
// to go: the `CGImageRelease` drops the decode's, and nilling the array lets
// ARC drop the array's.
void AppKitImageLoader::forgetAnimations() {
  for (auto &[uri, animation] : animations_) {
    (void)uri;
    releaseAnimation(animation);
  }
  animations_.clear();
}

void AppKitImageLoader::releaseAnimation(Animation &animation) {
  for (id frame in animation.frames) {
    CGImageRelease((__bridge CGImageRef)frame);
  }
  animation.frames = nil;
  animation.delaysMs = nil;
}

namespace {

// ImageIO rather than NSImage. An NSImage is a list of representations at
// different sizes with a resolution attached, and asking one for a CGImage
// means telling it a size and a context -- so a 160x100 PNG can come back
// 320x200 on a Retina display, which then measures wrong. CGImageSource hands
// back exactly what is in the file.
// One frame's delay in milliseconds, or nothing when the format carries none.
//
// Three dictionaries for three formats, and the unclamped delay first where
// there is one: ImageIO's clamped value has already had a floor of its own
// applied, and the clamp belongs in core/ImageAnimation.h where both hosts and
// the next one can see it.
std::optional<unsigned> delayOfFrame(CGImageSourceRef source, size_t index) {
  CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, index, nullptr);
  if (properties == nullptr) {
    return std::nullopt;
  }

  std::optional<unsigned> milliseconds;
  for (CFStringRef container : {kCGImagePropertyGIFDictionary,
                                kCGImagePropertyPNGDictionary,
                                kCGImagePropertyWebPDictionary}) {
    const void *found = CFDictionaryGetValue(properties, container);
    if (found == nullptr) {
      continue;
    }
    CFDictionaryRef dictionary = static_cast<CFDictionaryRef>(found);
    for (CFStringRef key : {kCGImagePropertyGIFUnclampedDelayTime,
                            kCGImagePropertyGIFDelayTime,
                            kCGImagePropertyAPNGUnclampedDelayTime,
                            kCGImagePropertyAPNGDelayTime,
                            kCGImagePropertyWebPUnclampedDelayTime,
                            kCGImagePropertyWebPDelayTime}) {
      const void *value = CFDictionaryGetValue(dictionary, key);
      if (value == nullptr) {
        continue;
      }
      double seconds = 0.0;
      if (CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &seconds)) {
        milliseconds = static_cast<unsigned>(seconds * 1000.0 + 0.5);
        break;
      }
    }
    if (milliseconds.has_value()) {
      break;
    }
  }

  CFRelease(properties);
  return milliseconds;
}

// How many times round, zero for forever.
//
// There is no "absent" case to handle, which was measured rather than assumed:
// ImageIO answers `kCGImagePropertyGIFLoopCount` with 1 for a GIF carrying no
// NETSCAPE2.0 extension at all, which is the format's way of saying play once.
// So the fallback below is unreachable for a GIF and left as forever for a
// format that really says nothing.
unsigned loopCountOf(CGImageSourceRef source) {
  CFDictionaryRef properties = CGImageSourceCopyProperties(source, nullptr);
  if (properties == nullptr) {
    return 0;
  }

  unsigned loops = 0;
  for (auto [container, key] :
       {std::pair<CFStringRef, CFStringRef>{kCGImagePropertyGIFDictionary,
                                            kCGImagePropertyGIFLoopCount},
        std::pair<CFStringRef, CFStringRef>{kCGImagePropertyPNGDictionary,
                                            kCGImagePropertyAPNGLoopCount},
        std::pair<CFStringRef, CFStringRef>{kCGImagePropertyWebPDictionary,
                                            kCGImagePropertyWebPLoopCount}}) {
    const void *found = CFDictionaryGetValue(properties, container);
    if (found == nullptr) {
      continue;
    }
    const void *value = CFDictionaryGetValue(static_cast<CFDictionaryRef>(found), key);
    int count = 0;
    if (value != nullptr &&
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &count) &&
        count > 0) {
      loops = static_cast<unsigned>(count);
      break;
    }
  }

  CFRelease(properties);
  return loops;
}

CGImageRef decode(const std::string &bytes,
                  std::string *error,
                  std::vector<CGImageRef> *frames = nullptr,
                  std::vector<unsigned> *delaysMs = nullptr,
                  unsigned *loopCount = nullptr) {
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

  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  if (image == nullptr) {
    CFRelease(source);
    *error = "could not decode image";
    return nullptr;
  }

  // The rest of the frames, when there are any. A multi-page TIFF reaches here
  // too and is deliberately not animated: it carries no per-frame delay, so
  // `delayOfFrame` answers nothing, the frames are dropped, and the caller sees
  // the still first page -- which is what a multi-page TIFF is.
  if (frames != nullptr && delaysMs != nullptr && loopCount != nullptr) {
    const size_t count = CGImageSourceGetCount(source);
    if (count > 1) {
      std::vector<unsigned> delays;
      std::vector<CGImageRef> decoded;
      bool everyFrameHasADelay = true;
      for (size_t index = 0; index < count; index++) {
        const std::optional<unsigned> delay = delayOfFrame(source, index);
        if (!delay.has_value()) {
          everyFrameHasADelay = false;
          break;
        }
        CGImageRef frame = index == 0 ? CGImageRetain(image)
                                      : CGImageSourceCreateImageAtIndex(source, index, nullptr);
        if (frame == nullptr) {
          everyFrameHasADelay = false;
          break;
        }
        decoded.push_back(frame);
        delays.push_back(*delay);
      }
      if (everyFrameHasADelay) {
        *frames = std::move(decoded);
        *delaysMs = std::move(delays);
        *loopCount = loopCountOf(source);
      } else {
        for (CGImageRef frame : decoded) {
          CGImageRelease(frame);
        }
      }
    }
  }

  CFRelease(source);
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
  size_t bytes = CGImageGetBytesPerRow(image) * CGImageGetHeight(image);
  // Every frame of an animation, because every frame is decoded and held: a
  // sixty-frame GIF is sixty images and accounting for one of them would let
  // the cache hold far more than it was told to.
  if (const auto found = animations_.find(uri); found != animations_.end()) {
    for (id frame in found->second.frames) {
      CGImageRef one = (__bridge CGImageRef)frame;
      if (one != image) {
        bytes += CGImageGetBytesPerRow(one) * CGImageGetHeight(one);
      }
    }
  }

  for (const std::string &evicted : policy_.insert(uri, bytes)) {
    const auto it = cache_.find(evicted);
    if (it != cache_.end()) {
      CGImageRelease(it->second);
      cache_.erase(it);
    }
    if (const auto animated = animations_.find(evicted); animated != animations_.end()) {
      releaseAnimation(animated->second);
      animations_.erase(animated);
    }
  }
}

void AppKitImageLoader::load(const std::string &uri,
                             Callback &&callback,
                             basalt::ImageProgress onProgress) {
  if (uri.empty()) {
    callback(nullptr, "empty source uri");
    return;
  }

  if (const auto it = cache_.find(uri); it != cache_.end()) {
    policy_.noteUse(uri);
    callback(it->second, {});
    return;
  }

  auto *pending = new Pending{this,      uri, nullptr,            {}, {}, 0U, {},
                              std::move(callback), std::move(onProgress)};

  std::thread([pending]() {
    std::string bytes;
    // Both the fetch and the decode run here. A CGImage is immutable and
    // thread-safe, so unlike the GTK side there is nothing that has to wait for
    // the main thread except the delivery itself.
    if (fetchImageBytes(pending->uri, &bytes, &pending->error, pending->onProgress)) {
      pending->image = decode(bytes, &pending->error, &pending->frames, &pending->delaysMs,
                              &pending->loopCount);
    }

    dispatch_async_f(dispatch_get_main_queue(), pending, [](void *data) {
      std::unique_ptr<Pending> done{static_cast<Pending *>(data)};
      if (done->image != nullptr) {
        // The frames first, so that `remember` can account for all of them in
        // one insert rather than growing the cache's idea of this URI twice.
        if (done->frames.size() > 1) {
          done->loader->rememberAnimation(done->uri, done->frames, done->delaysMs,
                                          done->loopCount);
        }
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

// Keeps the frames as NSArrays, built once here. The arrays are what the view
// is handed, and handing back the same arrays each time is what lets it tell a
// re-mount from a new animation; see RnAppKitView's setRnImageFrames.
void AppKitImageLoader::rememberAnimation(const std::string &uri,
                                          const std::vector<CGImageRef> &frames,
                                          const std::vector<unsigned> &delaysMs,
                                          unsigned loopCount) {
  if (const auto found = animations_.find(uri); found != animations_.end()) {
    // A second decode of the same URI, which a cache miss on one and a hit on
    // the other can produce. The frames already held are the ones views are
    // drawing, so these are dropped rather than swapped in.
    for (CGImageRef frame : frames) {
      CGImageRelease(frame);
    }
    return;
  }

  NSMutableArray *images = [NSMutableArray arrayWithCapacity:frames.size()];
  NSMutableArray<NSNumber *> *delays = [NSMutableArray arrayWithCapacity:delaysMs.size()];
  for (size_t index = 0; index < frames.size(); index++) {
    // The array's own retain, and the decode's reference is what this class
    // releases on eviction.
    [images addObject:(__bridge id)frames[index]];
    [delays addObject:@(delaysMs[index])];
  }

  animations_[uri] = Animation{images, delays, loopCount};
}

const AppKitImageLoader::Animation *AppKitImageLoader::animation(const std::string &uri) {
  const auto found = animations_.find(uri);
  return found == animations_.end() ? nullptr : &found->second;
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
