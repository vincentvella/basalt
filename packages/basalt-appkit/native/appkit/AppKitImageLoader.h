// Loading the pixels behind <Image>.
//
// React Native's cxx platform does not do this for us. `ImageManager` has a
// platform variant under `imagemanager/platform/cxx`, and every method in it is
// a stub that returns an empty `ImageRequest` -- so no `ImageResponse` ever
// arrives, and `ImageState` never carries anything to render. The platform view
// is expected to load its own image, which is also what Android does (Fresco,
// from `ReactImageView`, not from the shadow node).
//
// So the mounting manager reads the URI straight off `ImageProps::sources` and
// asks this to produce a CGImage.
//
// Which URIs work is in core/ImageBytes.h, shared with the GTK loader. What is
// here is the decode and the cache, which is the only part that differs --
// and one thing that genuinely differs beyond spelling: a CGImage is immutable
// and thread-safe, so decoding happens on the worker thread. The GTK side has
// to decode on the main thread, because a GdkTexture is a GObject.

#pragma once

#import <Cocoa/Cocoa.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ImageBytes.h"
#include "ImageCache.h"

#include <react/io/IImageLoader.h>

namespace basalt {

// Also React Native's own `IImageLoader`, which is what `Image.getSize` and
// `Image.prefetch` reach. Upstream's `ImageLoaderModule` takes one and
// `ReactCxxTurboModuleProvider` never supplies it -- see
// docs/backlog/upstream.md -- so the host registers the module itself with
// this as the loader.
class AppKitImageLoader : public facebook::react::IImageLoader {
 public:
  // `image` is null when the load failed, and `error` says why. Always called
  // on the main thread, possibly synchronously if the image is cached.
  using Callback = std::function<void(CGImageRef image, const std::string &error)>;

  AppKitImageLoader();
  ~AppKitImageLoader();

  AppKitImageLoader(const AppKitImageLoader &) = delete;
  AppKitImageLoader &operator=(const AppKitImageLoader &) = delete;
  AppKitImageLoader(AppKitImageLoader &&) = delete;
  AppKitImageLoader &operator=(AppKitImageLoader &&) = delete;

  // `onProgress` is optional and is `basalt::ImageProgress`: how much of an
  // http fetch has arrived, called from the fetching thread. Only an http URI
  // reports anything; see core/ImageBytes.h.
  //
  // Not marshalled to the main thread, unlike the callback above, and that is
  // the point of it: a Fabric event emitter may be dispatched from any thread,
  // and a progress tick that had to wait for the main queue would arrive after
  // the image it is about.
  void load(const std::string &uri,
            Callback &&callback,
            basalt::ImageProgress onProgress = {});

  // The frames of an animated image, or null.
  //
  // Separate from `load` because being animated is a property of the file
  // rather than of the request: `load` answers with the first frame, which is
  // what a still <Image> draws and what an animated one starts on, and this
  // says whether there is more. Both come out of the same decode, so asking
  // costs nothing.
  //
  // The arrays are the loader's own and stay at the same addresses for as long
  // as the URI is cached, which is what lets a view tell "the same animation
  // again" from "a different one" when a layout-only mutation re-applies the
  // props. The pointer is good until the next load, since an eviction can take
  // the entry away.
  struct Animation {
    // CGImageRefs, one per frame, bridged into an NSArray so that
    // RnAppKitView.h stays free of C++ -- the same reason the paragraph is
    // handed over as an object.
    NSArray *frames;
    // The file's own delays in milliseconds, unclamped: the clamp is
    // core/ImageAnimation.h's and belongs where the arithmetic is.
    NSArray<NSNumber *> *delaysMs;
    // Zero for forever, which is what a looping GIF carries.
    unsigned loopCount;
  };
  const Animation *animation(const std::string &uri);

  // --- IImageLoader ----------------------------------------------------------
  //
  // The same decode as `load`, reporting the size rather than the pixels. It
  // goes through the same cache, so `Image.getSize` on something already on
  // screen answers without touching the disk or the network.
  void loadImage(const std::string &uri,
                 const facebook::react::IImageLoaderOnLoadCallback &&onLoad) override;

  facebook::react::IImageLoader::CacheStatus getCacheStatus(const std::string &uri) override;

 private:
  struct Pending;

  // Decoded images, keyed by URI. CGImages are immutable and shareable, so two
  // <Image>s with the same source draw the same object.
  //
  // Nothing evicts from this yet; see docs/BACKLOG.md.
  // Caches an image and releases whatever the policy dropped.
  void remember(const std::string &uri, CGImageRef image);
  void rememberAnimation(const std::string &uri,
                         const std::vector<CGImageRef> &frames,
                         const std::vector<unsigned> &delaysMs,
                         unsigned loopCount);
  void releaseAnimation(Animation &animation);
  void forgetAnimations();

  std::unordered_map<std::string, CGImageRef> cache_;
  // Only for the files that have more than one frame, so a still image costs
  // nothing. Keyed by the same URI and dropped by the same eviction.
  std::unordered_map<std::string, Animation> animations_;
  // Which URI goes next, and when. The images are this class's; the decision
  // is shared with the other two hosts. See core/ImageCache.h.
  basalt::ImageCachePolicy policy_;
};

} // namespace basalt
