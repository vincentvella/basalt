#include "Win32ImageLoader.h"

#include "ImageBytes.h"
#include "PlatformServices.h"
#include "Win32UiThread.h"

#include <cstdint>
#include <thread>
#include <utility>

namespace basalt::win32 {

using basalt::hasUiThread;
using basalt::postToUiThread;

Win32ImageLoader::Win32ImageLoader() : state_(std::make_shared<State>()) {}

Win32ImageLoader::~Win32ImageLoader() {
  // The state outlives this object whenever a load is in flight. Marking it
  // dead is what makes the completion a no-op rather than a use-after-free.
  const std::lock_guard<std::mutex> lock(state_->mutex);
  state_->alive = false;
}

void Win32ImageLoader::clearCache() {
  const std::lock_guard<std::mutex> lock(state_->mutex);
  state_->cache.clear();
  state_->policy.clear();
}

void Win32ImageLoader::load(const std::string &uri,
                            Callback callback,
                            basalt::ImageProgress onProgress) {
  if (!callback) {
    return;
  }
  if (uri.empty()) {
    callback(nullptr, "empty uri");
    return;
  }

  // Answered synchronously, deliberately: an <Image> whose pixels are already
  // decoded should not flicker through a frame of nothing on its way back to
  // the screen. Both other platforms answer a cache hit inline too.
  {
    const std::lock_guard<std::mutex> lock(state_->mutex);
    if (const auto it = state_->cache.find(uri); it != state_->cache.end()) {
      const auto image = it->second;
      state_->policy.noteUse(uri);
      // Outside the lock would be tidier; inside is fine because the callback
      // never re-enters the loader, and holding it across the call is what
      // stops a concurrent clearCache from dropping the entry underneath.
      callback(image, {});
      return;
    }
  }

  // Fetch and decode, and where they run depends on whether there is anywhere
  // to come back to.
  const auto work = [uri, onProgress = std::move(onProgress)](
                        std::shared_ptr<RnWin32Image> &image,
                        RnWin32ImageFrames &frames,
                        std::string &error) {
    std::string bytes;
    if (!fetchImageBytes(uri, &bytes, &error, onProgress)) {
      if (error.empty()) {
        error = "could not fetch " + uri;
      }
      return;
    }
    if (bytes.empty()) {
      error = "no bytes for " + uri;
      return;
    }
    image = RnWin32Image::fromEncodedBytes(
        reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
    if (image == nullptr) {
      error = "could not decode " + uri;
      return;
    }
    // The same bytes again, as an animation. A second decode rather than one
    // path for both, because the still decode above is what every <Image> goes
    // through and must not change: a frame count of one leaves this empty and
    // costs a header read.
    frames = RnWin32Image::framesFromEncodedBytes(
        reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
  };

  auto state = state_;
  const auto deliver = [state, uri](const std::shared_ptr<RnWin32Image> &image,
                                    const RnWin32ImageFrames &frames,
                                    const std::string &error,
                                    const Callback &callback) {
    // The loader may have gone while this was in flight -- a surface torn down,
    // not only the process exiting. The state is still here, which is the point
    // of it being shared, and there is simply nothing to deliver to.
    {
      const std::lock_guard<std::mutex> lock(state->mutex);
      if (!state->alive) {
        return;
      }
      if (image != nullptr) {
        state->cache[uri] = image;
        if (frames.animated()) {
          state->animations[uri] = frames;
        }

        // Four bytes a pixel, premultiplied BGRA, which is what
        // `fromEncodedBytes` decodes to and what RnWin32Image's own stride
        // says. Every frame of an animation, because every frame is a decoded
        // bitmap that is held. See core/ImageCache.h for which URI goes next.
        size_t bytes = static_cast<size_t>(image->width()) *
                       static_cast<size_t>(image->height()) * 4u;
        for (const auto &frame : frames.frames) {
          if (frame != nullptr) {
            bytes += static_cast<size_t>(frame->width()) *
                     static_cast<size_t>(frame->height()) * 4u;
          }
        }
        for (const std::string &evicted : state->policy.insert(uri, bytes)) {
          state->cache.erase(evicted);
          state->animations.erase(evicted);
        }
      }
    }
    callback(image, error);
  };

  // No message loop to post back to, so no worker either.
  //
  // This is the case the harness and the tests are in, and going asynchronous
  // anyway would be actively wrong rather than merely pointless:
  // `postToUiThread` runs its work inline when nothing is installed, so the
  // completion would run *on the worker* and reach straight into the mounting
  // manager's registry from the wrong thread. Staying synchronous keeps that
  // configuration single-threaded, which is what it already assumes everywhere
  // else.
  if (!hasUiThread()) {
    std::shared_ptr<RnWin32Image> image;
    RnWin32ImageFrames frames;
    std::string error;
    work(image, frames, error);
    deliver(image, frames, error, callback);
    return;
  }

  // With a host, both halves go to a worker. GTK reads on a worker and decodes
  // back on the main thread, because a GdkTexture is a GObject and the
  // expensive part there is the read; WIC has neither constraint -- its factory
  // is agile and the decode is the expensive part -- so the whole job leaves
  // the UI thread and only the delivery comes back.
  //
  // Detached rather than joined: nothing waits for an image, and everything the
  // thread touches is either its own or the shared state above.
  std::thread([work, deliver, callback = std::move(callback)]() mutable {
    std::shared_ptr<RnWin32Image> image;
    RnWin32ImageFrames frames;
    std::string error;
    work(image, frames, error);
    postToUiThread(
        [deliver, image, frames, error, callback = std::move(callback)]() {
          deliver(image, frames, error, callback);
        });
  }).detach();
}

RnWin32ImageFrames Win32ImageLoader::animation(const std::string &uri) {
  const std::lock_guard<std::mutex> lock(state_->mutex);
  const auto found = state_->animations.find(uri);
  return found == state_->animations.end() ? RnWin32ImageFrames{} : found->second;
}

void Win32ImageLoader::loadImage(const std::string &uri,
                                 const facebook::react::IImageLoaderOnLoadCallback &&onLoad) {
  // Copied because the callback may outlive this call, as it does on the other
  // two hosts; here a cache hit answers inline, and a miss does not.
  auto callback = onLoad;
  load(uri, [callback](std::shared_ptr<RnWin32Image> image, const std::string &error) {
    if (image == nullptr) {
      // The message rather than a size: `Image.getSize` rejects with it, and a
      // zero-by-zero success would be indistinguishable from a 0x0 image.
      callback(0.0, 0.0, error.empty() ? "could not load image" : error.c_str());
      return;
    }
    callback(static_cast<double>(image->width()), static_cast<double>(image->height()), nullptr);
  });
}

facebook::react::IImageLoader::CacheStatus Win32ImageLoader::getCacheStatus(
    const std::string &uri) {
  // Memory or nothing: there is no disk cache here, and saying `Disk` would be
  // claiming a persistence this has not got.
  const std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->cache.find(uri) != state_->cache.end()
      ? facebook::react::IImageLoader::CacheStatus::Memory
      : facebook::react::IImageLoader::CacheStatus::None;
}

} // namespace basalt::win32
