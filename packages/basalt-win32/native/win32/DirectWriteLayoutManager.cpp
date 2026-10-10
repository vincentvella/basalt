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
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <vector>

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

  // `adjustsFontSizeToFit`: how far the fonts have to shrink for this paragraph
  // to fit the box Yoga offered. A no-op ratio of 1 for every paragraph that did
  // not ask, which is almost all of them -- the search builds a layout per probe
  // and must not run otherwise.
  const basalt::FontFit fit =
      basalt::win32::textFitScale(attributedString,
                                  paragraphAttributes,
                                  maxWidth,
                                  static_cast<float>(layoutConstraints.maximumSize.height));
  const auto layout =
      basalt::win32::buildTextLayout(attributedString, paragraphAttributes, fit);
  const basalt::win32::RnTextSize size =
      layout == nullptr ? basalt::win32::RnTextSize{} : layout->measure(maxWidth);

  // DirectWrite can report a line slightly wider than the width it was given,
  // for an unbreakable run. Yoga treats the returned size as final, so clamping
  // here keeps the paragraph inside the box its parent allotted.
  measured.size = Size{
      .width = std::min(static_cast<Float>(size.width), layoutConstraints.maximumSize.width),
      .height = static_cast<Float>(size.height),
  };

  // Inline views (`<Text><View/></Text>`) reach here as attachment fragments,
  // and `ParagraphShadowNode` positions each child by the frame reported here.
  // The paragraph reserved a box for each through an `IDWriteInlineObject`; this
  // reads back where they landed. See `attachmentBoxes`.
  //
  // In fragment order, which is the order the boxes come back in, because both
  // walk the same run list. A fragment whose view measured to nothing gets a
  // zero frame rather than a position: there is no box to find, and reporting
  // one would move a view that has no size.
  const std::vector<basalt::win32::RnAttachmentBox> boxes =
      layout == nullptr ? std::vector<basalt::win32::RnAttachmentBox>{}
                        : layout->attachmentBoxes(maxWidth);
  size_t at = 0;
  for (const auto &fragment : attributedString.getFragments()) {
    if (!fragment.isAttachment()) {
      continue;
    }
    const basalt::win32::RnAttachmentBox box =
        at < boxes.size() ? boxes[at] : basalt::win32::RnAttachmentBox{};
    at++;
    measured.attachments.push_back(TextMeasurement::Attachment{
        .frame = {.origin = {.x = box.x, .y = box.y},
                  .size = {.width = box.width, .height = box.height}},
        // Still always false, as it is on GTK: it means the view fell outside
        // the paragraph after `numberOfLines` cut it, and telling that one
        // attachment is on a line DirectWrite has trimmed needs more than the
        // position -- the same half-answer the GTK entry declined to ship.
        .isClipped = false,
    });
  }

  {
    const std::lock_guard<std::mutex> lock(measurementCacheMutex());
    measurementCache()[key] = measured;
  }
  return measured;
}

} // namespace facebook::react
