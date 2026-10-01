// The pixels behind an <Image>, fetched and decoded off the UI thread.
//
// React Native's cxx `ImageManager` is a stub -- `requestImage` returns an
// `ImageRequest` with no observer coordinator, so no `ImageResponse` ever
// arrives and `ImageState` never carries anything to render. The platform view
// is expected to load its own pixels, which is what Android does too, from
// `ReactImageView` rather than from the shadow node. `docs/DECISIONS.md` records
// the same conclusion for GTK.
//
// Two rules fall out of the lifetimes, and both are the reason this is a class
// rather than a function.
//
// **A load must survive the view that asked for it.** A view can be deleted
// while its bytes are still in flight, so the completion may not capture a
// view; the mounting manager looks the tag up again when it runs. That is the
// same arrangement GTK settled on.
//
// **A load must survive the loader.** The worker thread and the queued
// completion both outlive any particular call, and a mounting manager can be
// destroyed between the two -- which happens on surface teardown, not only at
// exit. So the cache lives in a shared state object that the in-flight work
// holds a reference to, and a completion arriving after the loader is gone
// finds the state alive and the callback already neutered. The AppKit loader
// captures its `this` raw and has the bug this avoids.

#pragma once

#include "RnWin32Image.h"

#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>

#include "ImageCache.h"

#include <react/io/IImageLoader.h>

namespace basalt::win32 {

// Also React Native's own `IImageLoader`, which is what `Image.getSize` and
// `Image.prefetch` reach. Upstream's `ImageLoaderModule` takes one and
// `ReactCxxTurboModuleProvider` never supplies it -- see
// docs/backlog/upstream.md -- so the host registers the module itself with
// this as the loader.
class Win32ImageLoader : public facebook::react::IImageLoader {
 public:
  // `image` is null when the load failed, and `error` says why. Always called
  // on the UI thread, and possibly before `load` has returned -- a cache hit is
  // answered synchronously, because making it asynchronous would mean an
  // <Image> that is already decoded still flickers through a frame of nothing.
  using Callback = std::function<void(std::shared_ptr<RnWin32Image> image,
                                      const std::string &error)>;

  Win32ImageLoader();
  ~Win32ImageLoader();

  Win32ImageLoader(const Win32ImageLoader &) = delete;
  Win32ImageLoader &operator=(const Win32ImageLoader &) = delete;

  void load(const std::string &uri, Callback callback);

  // --- IImageLoader ----------------------------------------------------------
  //
  // The same decode as `load`, reporting the size rather than the pixels. It
  // goes through the same cache, so `Image.getSize` on something already on
  // screen answers without touching the disk or the network.
  void loadImage(const std::string &uri,
                 const facebook::react::IImageLoaderOnLoadCallback &&onLoad) override;

  facebook::react::IImageLoader::CacheStatus getCacheStatus(const std::string &uri) override;

  // Drops every decoded image at once, for a host shutting a surface down.
  // Ordinary eviction is core/ImageCache.h's, which runs on every insert.
  void clearCache();

 private:
  struct State {
    // Locked, rather than relying on "everything happens on the UI thread".
    //
    // That reasoning was tried and is wrong here in a way worth recording,
    // because it looks right: `load` is called on the UI thread and every
    // completion is posted back to it, so a lock seems redundant. But
    // `postToUiThread` runs its work *inline* when no host has installed a
    // message loop, and inline on a worker thread is not the UI thread. The
    // load below avoids that case entirely by not spawning a worker when there
    // is nothing to marshal back to; this lock is the belt to that pair of
    // braces, and it costs a hash lookup's worth of nothing.
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<RnWin32Image>> cache;
    // Which URI goes next, and when. Shared with the other two hosts; the
    // images are this loader's. See core/ImageCache.h. Guarded by the mutex
    // beside it, like the cache it decides for.
    basalt::ImageCachePolicy policy;
    // Cleared when the loader is destroyed, so a completion that outlives it
    // delivers nowhere instead of into freed memory.
    bool alive = true;
  };

  std::shared_ptr<State> state_;
};

} // namespace basalt::win32
