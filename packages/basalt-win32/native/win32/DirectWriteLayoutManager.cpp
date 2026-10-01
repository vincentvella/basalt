// Replaces React Native's stub TextLayoutManager with one over DirectWrite.
//
// React Native's cxx platform ships a header and a stub .cpp that ignores every
// attribute and returns `layoutConstraints.minimumSize`. The header is already
// generic, so this project keeps it and supplies only the implementation --
// `cmake/ReactNativeCore.cmake` removes the stub source from
// `react_renderer_textlayoutmanager` after `add_subdirectory` so the two do not
// collide. Same arrangement as Pango's and Core Text's; see
// `docs/DECISIONS.md`.

#include "DirectWriteLayout.h"

#include "FontRegistry.h"

#include <react/renderer/textlayoutmanager/TextLayoutManager.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace facebook::react {
namespace {

std::mutex &measurementCacheMutex() {
  static std::mutex mutex;
  return mutex;
}

// Not React Native's own `textMeasureCache_`, which has no way to be emptied.
//
// A font registered since a measurement invalidates it: the same string with
// the same attributes measures differently once its family exists. That is not
// expressible in the cache key, which React Native defines, so the cache has to
// be droppable instead -- and without that, a font loaded after first render,
// which is how `useFonts` and every other loader works, appears to do nothing.
std::unordered_map<TextMeasureCacheKey, TextMeasurement> &measurementCache() {
  static std::unordered_map<TextMeasureCacheKey, TextMeasurement> cache;
  static unsigned long generation = 0;
  const unsigned long current = basalt::fontGeneration();
  if (current != generation) {
    cache.clear();
    generation = current;
  }
  return cache;
}

} // namespace

TextLayoutManager::TextLayoutManager(const std::shared_ptr<const ContextContainer> &contextContainer)
    : contextContainer_(contextContainer), textMeasureCache_(kSimpleThreadSafeCacheSizeCap) {}

TextMeasurement TextLayoutManager::measure(const AttributedStringBox &attributedStringBox,
                                           const ParagraphAttributes &paragraphAttributes,
                                           const TextLayoutContext &layoutContext,
                                           const LayoutConstraints &layoutConstraints) const {
  const auto &attributedString = attributedStringBox.getValue();

  // pointScaleFactor joined the cache key in React Native 0.87. On 0.86 the key
  // has three fields and naming a fourth is a compile error. Its absence only
  // makes the cache coarser there, and since nothing below uses the factor,
  // coarser is harmless.
  const TextMeasureCacheKey key{
      .attributedString = attributedString,
      .paragraphAttributes = paragraphAttributes,
      .layoutConstraints = layoutConstraints,
#if BASALT_RN_MINOR >= 87
      .pointScaleFactor = layoutContext.pointScaleFactor,
#endif
  };

#if BASALT_RN_MINOR < 87
  (void)layoutContext;
#endif

  {
    const std::lock_guard<std::mutex> lock(measurementCacheMutex());
    auto &cache = measurementCache();
    const auto hit = cache.find(key);
    if (hit != cache.end()) {
      return hit->second;
    }
  }

  TextMeasurement measured;

  // An infinite maximum width means "do not wrap".
  const float maxWidth = std::isinf(layoutConstraints.maximumSize.width)
      ? -1.0f
      : static_cast<float>(layoutConstraints.maximumSize.width);

  const auto layout = basalt::win32::buildTextLayout(attributedString, paragraphAttributes);
  const basalt::win32::RnTextSize size =
      layout == nullptr ? basalt::win32::RnTextSize{} : layout->measure(maxWidth);

  // DirectWrite can report a line slightly wider than the width it was given,
  // for an unbreakable run. Yoga treats the returned size as final, so clamping
  // here keeps the paragraph inside the box its parent allotted.
  measured.size = Size{
      .width = std::min(static_cast<Float>(size.width), layoutConstraints.maximumSize.width),
      .height = static_cast<Float>(size.height),
  };

  // Inline views (`<Text><View/></Text>`) reach here as attachment fragments.
  // Reporting a zero frame for each keeps the count right, which is what
  // ParagraphShadowNode iterates over, but they are not positioned -- the same
  // gap both other desktops have, for the same reason.
  for (const auto &fragment : attributedString.getFragments()) {
    if (fragment.isAttachment()) {
      measured.attachments.push_back(TextMeasurement::Attachment{
          .frame = {.origin = {.x = 0, .y = 0}, .size = {.width = 0, .height = 0}},
          .isClipped = false,
      });
    }
  }

  {
    const std::lock_guard<std::mutex> lock(measurementCacheMutex());
    measurementCache()[key] = measured;
  }
  return measured;
}

} // namespace facebook::react
