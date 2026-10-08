// The macOS implementation of React Native's text measurement seam.
//
// `TextLayoutManager` is declared in React Native's cxx platform variant, and
// its only implementation there is a stub that ignores every attribute and
// returns `layoutConstraints.minimumSize`. cmake/ReactNativeCore.cmake drops
// that stub from the build, so a platform either provides this or fails to
// link -- which is the second of the three seams phase 19 found, and the reason
// macOS could not register a Paragraph descriptor until now.
//
// The header is unchanged and shared; only the .cpp differs, which is what
// React Native's platform/ split is for. Its Linux counterpart is
// basalt-gtk's PangoTextLayoutManager.cpp, and the two are
// deliberately the same file with a different engine inside.
//
// Yoga calls this during layout, on whichever thread is committing, so it must
// be thread-safe and it must be fast.

#import "CoreTextLayout.h"

#include "FontRegistry.h"

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

  TextMeasurement measured;
  @autoreleasepool {
    // An infinite maximum width means "do not wrap".
    const CGFloat maxWidth = std::isinf(layoutConstraints.maximumSize.width)
        ? -1.0
        : static_cast<CGFloat>(layoutConstraints.maximumSize.width);

    RnTextLayout *layout = basalt::buildTextLayout(attributedString, paragraphAttributes);
    const CGSize size = [layout sizeForWidth:maxWidth];

    // Core Text can report a line slightly wider than the width it was given,
    // for an unbreakable run. Yoga treats the returned size as final, so
    // clamping here keeps the paragraph inside the box its parent allotted.
    const auto natural = Size{
        .width = std::min(static_cast<Float>(size.width), layoutConstraints.maximumSize.width),
        .height = static_cast<Float>(size.height),
    };

    // Inline views (`<Text><View/></Text>`) reach here as attachment fragments.
    // Reporting a zero frame for each keeps the count right, which is what
    // ParagraphShadowNode iterates over, but they are not positioned yet --
    // the same gap the GTK side has, for the same reason.
    // Inline views (`<Text><View/></Text>`), one attachment per view and in
    // fragment order, which is how ParagraphShadowNode pairs them back up. A run
    // delegate reserved each box during layout, so these are where the paragraph
    // put them rather than the zeroes this used to report. See backlog/text.md.
    //
    // The index arithmetic is the part to be careful about: fragment strings are
    // UTF-8 and measured in bytes, while NSAttributedString counts UTF-16, so the
    // offset is accumulated in UTF-16 units as each fragment is appended. Using
    // byte offsets here would be right for ASCII and wrong the moment a paragraph
    // contains an emoji before the view.
    TextMeasurement::Attachments attachments;
    NSUInteger utf16At = 0;
    for (const auto &fragment : attributedString.getFragments()) {
      NSString *text = [NSString stringWithUTF8String:fragment.string.c_str()];
      // The transformed text, because that is what was laid out: a
      // `textTransform` that changes the length would otherwise put every
      // attachment after it at the wrong index.
      text = text != nil ? basalt::transformedFragmentText(fragment, text) : nil;
      const NSUInteger start = utf16At;
      utf16At += text == nil ? 0 : text.length;

      if (!fragment.isAttachment()) {
        continue;
      }

      const auto &size = fragment.parentShadowView.layoutMetrics.frame.size;
      if (size.width <= 0 || size.height <= 0) {
        // Nothing measured it, so there is no box and no position to invent.
        attachments.push_back(TextMeasurement::Attachment{.frame = {}, .isClipped = false});
        continue;
      }

      const CGRect box = [layout frameForCharacterIndex:start width:maxWidth];
      if (CGRectIsNull(box)) {
        // Off the end of a truncated paragraph, which is what isClipped is for.
        attachments.push_back(TextMeasurement::Attachment{.frame = {}, .isClipped = true});
        continue;
      }

      attachments.push_back(TextMeasurement::Attachment{
          .frame =
              {
                  .origin = {.x = static_cast<Float>(box.origin.x),
                             .y = static_cast<Float>(box.origin.y)},
                  // The size React Native measured, so a rounding difference in
                  // the delegate cannot move the view off its own layout's box.
                  .size = size,
              },
          .isClipped = false,
      });
    }

    measured = TextMeasurement{.size = layoutConstraints.clamp(natural), .attachments = attachments};
  }

  {
    const std::lock_guard<std::mutex> lock(measurementCacheMutex());
    measurementCache().emplace(key, measured);
  }

  return measured;
}

} // namespace facebook::react
