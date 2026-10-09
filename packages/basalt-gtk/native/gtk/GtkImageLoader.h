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
// asks this to produce a texture.
//
// Scheme support:
//   file://       read from disk
//   http, https   fetched with libcurl, on a worker thread
//   data:         decoded inline
//   bare paths    treated as file paths, which is what a `require()`d asset
//                 looks like once Metro has resolved it in a release bundle
//
// Everything is decoded off the main thread and delivered back onto it, because
// GdkTexture is cheap to hand around but decoding a large PNG is not something
// to do in a mount transaction.

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "ImageCache.h"

#include <react/io/IImageLoader.h>

namespace basalt {

// Also React Native's own `IImageLoader`, which is what `Image.getSize` and
// `Image.prefetch` reach. Upstream's `ImageLoaderModule` takes one and
// `ReactCxxTurboModuleProvider` never supplies it -- see
// docs/backlog/upstream.md -- so the host registers the module itself with
// this as the loader.
class GtkImageLoader : public facebook::react::IImageLoader {
 public:
  // `texture` is null when the load failed, and `error` says why. Always called
  // on the GTK main thread, possibly synchronously if the image is cached.
  using Callback = std::function<void(GdkTexture *texture, const std::string &error)>;

  GtkImageLoader();
  ~GtkImageLoader();

  GtkImageLoader(const GtkImageLoader &) = delete;
  GtkImageLoader &operator=(const GtkImageLoader &) = delete;
  GtkImageLoader(GtkImageLoader &&) = delete;
  GtkImageLoader &operator=(GtkImageLoader &&) = delete;

  void load(const std::string &uri, Callback &&callback);

  // The animation behind an animated image, or null.
  //
  // Separate from `load` because being animated is a property of the file
  // rather than of the request: `load` answers with the first frame, which is
  // what a still <Image> paints and what an animated one starts on, and this
  // says whether there is more. Both come out of the same decode.
  //
  // gdk-pixbuf owns the frames, and offers an iterator with a clock rather than
  // indexed access, which is why this hands over the animation itself where the
  // AppKit loader hands over an array of frames. The object is the loader's and
  // is the same object each time, which is what lets a view tell a re-mount
  // from a new animation. Good until the next load, since an eviction can take
  // it away.
  GdkPixbufAnimation *animation(const std::string &uri);

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

  static gboolean deliver(gpointer data);

  // Decoded textures, keyed by URI. Textures are immutable and shareable, so
  // two <Image>s with the same source paint the same object.
  //
  // Caches a texture and releases whatever the policy dropped.
  void remember(const std::string &uri, GdkTexture *texture);
  void rememberAnimation(const std::string &uri, GBytes *bytes);

  std::unordered_map<std::string, GdkTexture *> cache_;
  // Only the files that turned out to have more than one frame, so a still
  // image costs nothing. Keyed by the same URI and dropped by the same
  // eviction.
  //
  // The frames inside are gdk-pixbuf's and are not measured by the cache
  // policy: there is no way to ask a GdkPixbufAnimation how much it is holding,
  // and the AppKit side can because it decoded the frames itself. So an
  // animated image is accounted as its first frame on this host. Recorded in
  // docs/backlog/image.md rather than guessed at.
  std::unordered_map<std::string, GdkPixbufAnimation *> animations_;
  // Which URI goes next, and when. The textures are this class's; the decision
  // is shared with the other two hosts. See core/ImageCache.h.
  basalt::ImageCachePolicy policy_;
};

} // namespace basalt
