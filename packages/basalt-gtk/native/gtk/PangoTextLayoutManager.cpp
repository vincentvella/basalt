// The Linux implementation of React Native's text measurement seam.
//
// `TextLayoutManager` is declared in React Native's cxx platform variant, and
// its only implementation there is a stub that ignores every attribute and
// returns `layoutConstraints.minimumSize`. That is why text has never had a
// size on this platform. This file replaces that stub -- the header is
// unchanged and shared, only the .cpp differs, and cmake/ReactNativeCore.cmake
// drops React Native's from the build so these definitions are the only ones.
//
// Yoga calls this during layout, on whichever thread is committing, so it must
// be thread-safe and it must be fast. `textMeasureCache_` handles the second
// part: Yoga measures the same string repeatedly while resolving flex.

#include "FontRegistry.h"
#include "PangoTextLayout.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include <react/renderer/textlayoutmanager/TextLayoutManager.h>

namespace facebook::react {

namespace {

std::mutex &measurementCacheMutex() {
  static std::mutex mutex;
  return mutex;
}

// Emptied whenever a font is registered; see the note at its first use.
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

  // pointScaleFactor joined the cache key in React Native 0.87. On 0.81 the key
  // has three fields and naming a fourth is a compile error. Its absence only
  // makes the cache coarser there, and since nothing below uses the factor --
  // see the note further down -- coarser is harmless.
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

  // Not React Native's `textMeasureCache_`, which has no way to be emptied.
  //
  // A font registered since a measurement invalidates it: the same string with
  // the same attributes measures differently once its family exists. That is
  // not expressible in the cache key, which React Native defines, so the cache
  // has to be droppable instead -- and without that, a font loaded after first
  // render, which is how `useFonts` and every other loader works, appears to do
  // nothing at all.
  {
    const std::lock_guard<std::mutex> lock(measurementCacheMutex());
    auto &cache = measurementCache();
    const auto hit = cache.find(key);
    if (hit != cache.end()) {
      return hit->second;
    }
  }

  const TextMeasurement measured = [&]() {
    // An infinite maximum width means "do not wrap"; Pango wants -1 for that.
    const float maxWidth = std::isinf(layoutConstraints.maximumSize.width)
        ? -1.0F
        : static_cast<float>(layoutConstraints.maximumSize.width);

    // pointScaleFactor is part of the cache key above but not of the layout:
    // sizes here are logical, and GTK scales the rendered result. Keeping it in
    // the key only makes the cache finer-grained than it strictly needs to be.
    // `adjustsFontSizeToFit`: how far the fonts have to shrink for this
    // paragraph to fit the box Yoga offered. A no-op ratio of 1 for every
    // paragraph that did not ask, which is almost all of them -- the search
    // builds a layout per probe and must not run otherwise.
    const basalt::FontFit fit = basalt::textFitScale(
        attributedString,
        paragraphAttributes,
        maxWidth,
        static_cast<float>(layoutConstraints.maximumSize.height));
    PangoLayout *layout =
        basalt::buildTextLayout(attributedString, paragraphAttributes, maxWidth, fit);

    float width = 0;
    float height = 0;
    basalt::textLayoutSize(layout, &width, &height);
    // Before the layout goes: the attachment positions are only knowable from
    // it, and it used to be dropped on the line above.
    const auto attachmentRects = basalt::attachmentFrames(layout, attributedString);
    g_object_unref(layout);

    // Pango can report a line slightly wider than the width it was given, for
    // an unbreakable run. Yoga treats the returned size as final, so clamping
    // here keeps the paragraph inside the box its parent allotted.
    const auto measured = Size{
        .width = std::min(static_cast<Float>(width), layoutConstraints.maximumSize.width),
        .height = static_cast<Float>(height),
    };

    // Inline views (`<Text><View/></Text>`) reach here as attachment fragments,
    // one per view, and `ParagraphShadowNode` pairs them back up by order. The
    // layout reserved a box for each through a shape attribute, so these are the
    // boxes it put them in rather than the zeroes this used to report.
    //
    // isClipped stays false. It would mean the view fell outside the paragraph
    // after `numberOfLines` truncated it, and the limit is enforced now, for
    // 'clip' as well as for the three ellipsizing modes. What is still missing
    // is a way to tell that one particular attachment is on a line that went.
    // For clip the position can be compared with the visible height, because
    // the line is still in the layout; for head, middle and tail Pango has
    // already dropped the line, and what index_to_pos answers for a byte offset
    // inside a dropped one is documented nowhere. One answer covering all four
    // modes is worth more than a half-answer covering one, so this is left as
    // it was. See backlog/text.md.
    TextMeasurement::Attachments attachments;
    attachments.reserve(attachmentRects.size());
    for (const auto &frame : attachmentRects) {
      attachments.push_back(TextMeasurement::Attachment{
          .frame = frame,
          .isClipped = false,
      });
    }

    return TextMeasurement{.size = layoutConstraints.clamp(measured), .attachments = attachments};
  }();

  {
    const std::lock_guard<std::mutex> lock(measurementCacheMutex());
    measurementCache().emplace(key, measured);
  }

  return measured;
}

} // namespace facebook::react
