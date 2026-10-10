// `adjustsFontSizeToFit`, and the two props that bound it.
//
// A label that says "Delete all messages" in a button sized for "Delete" has
// two ways to go wrong: it can overflow, or it can be cut off. React Native's
// third way is to shrink the text until it fits, which is what this prop asks
// for, and the two beside it -- `minimumFontSize` and `maximumFontSize` -- say
// how far that is allowed to go.
//
// ## Why it is here rather than in three hosts
//
// Finding the ratio is a search, and the search is the same on every platform:
// measure, compare, halve the interval. What differs is the measuring, which is
// the one thing each toolkit knows and this does not -- so the search takes the
// measurement as a callable and nothing in this file knows what a glyph is.
//
// The alternative was three bisections, which is how the three scroll ranges
// and the three snapping rules started before `core/ScrollBounds.h` and
// `core/ScrollSnap.h`.
//
// ## The algorithm is upstream's, deliberately
//
// `NSTextStorage+FontScaling.m` is a bisection between a ratio of 1/128 and
// one of 128, with three rules worth naming because each one is a decision:
//
//   - **Text that already fits is not touched.** Upstream returns before the
//     search, which is why the prop never *grows* text although the interval
//     reaches 128: a paragraph that fits is done.
//   - **A fit within one per cent of the box's width is good enough**, and the
//     search stops there rather than finding the largest ratio that fits.
//     Without it the search runs its full depth on every paragraph.
//   - **The last ratio that fitted wins** when the interval closes, which is
//     not the same as the last ratio tried: the final probe is as likely to be
//     one that did not fit.
//
// Each font size is multiplied by the ratio and then clamped to the two props,
// so a paragraph of mixed sizes keeps its proportions until the clamp bites.

#pragma once

#include <react/renderer/attributedstring/ParagraphAttributes.h>

#include <algorithm>
#include <cmath>

namespace basalt {

// A size, in the logical points everything here measures in.
struct FontFitBox {
  double width{0.0};
  double height{0.0};
};

// How to scale a paragraph's fonts, and how far.
//
// A ratio of exactly 1 means "leave every size alone", which is both the
// default and what a paragraph that already fits resolves to -- and it skips
// the clamp as well, because `minimumFontSize` and `maximumFontSize` bound the
// *fitting* rather than the text. A 200pt heading in an app that set a maximum
// of 96 is still 200pt when it fits.
struct FontFit {
  double ratio{1.0};
  double minimum{4.0};
  double maximum{96.0};
  // `minimumFontScale`: the same floor as `minimumFontSize`, said as a ratio
  // instead. Android's spelling of the prop, which iOS ignores -- and there is
  // no reason to ignore it here, the search being a search over exactly this
  // ratio. Zero means the app said nothing.
  double minimumRatio{0.0};

  bool scales() const { return ratio != 1.0; }

  double apply(double size) const {
    if (ratio == 1.0) {
      return size;
    }
    return std::clamp(size * ratio, minimum, maximum);
  }
};

// iOS's own defaults for the two bounds, which React Native documents nowhere
// else: 4 and 96 points, from `RCTTextLayoutManager`.
inline constexpr double kFontFitMinimumSize = 4.0;
inline constexpr double kFontFitMaximumSize = 96.0;

// The bounds a paragraph asked for, with those defaults for the props it left
// unset -- which arrive as NaN, React Native's "nothing here".
inline FontFit fontFitLimits(const facebook::react::ParagraphAttributes &paragraphAttributes) {
  FontFit fit;
  if (!std::isnan(paragraphAttributes.minimumFontSize)) {
    fit.minimum = static_cast<double>(paragraphAttributes.minimumFontSize);
  }
  if (!std::isnan(paragraphAttributes.maximumFontSize)) {
    fit.maximum = static_cast<double>(paragraphAttributes.maximumFontSize);
  }
  // A minimum above the maximum is an app contradicting itself; the minimum
  // wins, so the text is readable rather than invisible.
  fit.maximum = std::max(fit.maximum, fit.minimum);
  // `minimumFontScale`, clamped to the range React Native's own comment gives
  // for it: 0.01 to 1. A scale of 1 is a paragraph that may not shrink at all,
  // which is a legal thing to ask for and worth not treating as "unset".
  if (!std::isnan(paragraphAttributes.minimumFontScale)) {
    fit.minimumRatio = std::clamp(static_cast<double>(paragraphAttributes.minimumFontScale),
                                  0.01,
                                  1.0);
  }
  return fit;
}

// Whether a box is one a paragraph can be fitted to at all.
//
// An infinite dimension is Yoga saying "as much as you need", and there is
// nothing to shrink to fit: a paragraph measured unconstrained always fits.
inline bool fontFitBoxIsFinite(const FontFitBox &box) {
  return std::isfinite(box.width) && std::isfinite(box.height) && box.width > 0.0
      && box.height > 0.0;
}

// The ratio that makes `measure` fit inside `box`.
//
// `measure` is called with a ratio and answers the size the paragraph takes
// with every font size scaled by it -- which is the host's job, and the only
// part of this that knows what text is.
template <typename Measure>
FontFit fontFitToBox(FontFit limits, const Measure &measure, const FontFitBox &box) {
  if (!fontFitBoxIsFinite(box)) {
    return limits;
  }

  const auto fits = [&box](const FontFitBox &size) {
    return size.width <= box.width && size.height <= box.height;
  };

  // Text that already fits is not touched, which is upstream's first move and
  // the reason this never grows anything.
  const FontFitBox unscaled = measure(1.0);
  if (fits(unscaled)) {
    return limits;
  }

  // `minimumFontScale` is the floor when an app named one, and upstream's
  // 1/128 otherwise. Searching below the floor would be searching for an
  // answer the app has refused.
  double bottom = std::max(1.0 / 128.0, limits.minimumRatio);
  double top = 1.0;
  // Upstream's floor for "nothing fitted", so a paragraph that cannot fit at
  // all comes out tiny rather than unscaled -- and no smaller than the ratio
  // the app allowed.
  double lastThatFits = std::max(0.02, limits.minimumRatio);
  double ratio = (top + bottom) / 2.0;

  // A fit within this much of the box's width is good enough. Without it the
  // search runs its full depth on every paragraph that needs shrinking.
  constexpr double kCloseEnough = 0.01;
  // And this is when the interval has closed, which bounds the loop at about
  // eight probes.
  constexpr double kSettled = 0.005;

  while (true) {
    FontFit candidate = limits;
    candidate.ratio = ratio;
    const FontFitBox size = measure(ratio);
    if (fits(size)) {
      if (box.width - size.width < box.width * kCloseEnough) {
        return candidate;
      }
      bottom = ratio;
      lastThatFits = ratio;
    } else {
      top = ratio;
    }

    const double next = (top + bottom) / 2.0;
    if (std::abs(top - bottom) < kSettled || std::abs(top - next) < kSettled
        || std::abs(bottom - next) < kSettled) {
      FontFit settled = limits;
      settled.ratio = lastThatFits;
      return settled;
    }
    ratio = next;
  }
}

} // namespace basalt
