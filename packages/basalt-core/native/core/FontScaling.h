// `allowFontScaling`, `maxFontSizeMultiplier` and the system's text scale.
//
// Three props and one platform setting that together decide what size a
// fragment is actually drawn at. Both hosts had the same four lines of it,
// multiplying by `fontSizeMultiplier` when it was set, and neither read the two
// props that are there to control that multiplication: an app that says
// `allowFontScaling={false}` means "do not grow this text when the system is
// set to large text", which is the usual thing to say about a label whose box
// cannot grow.
//
// ## Where the multiplier comes from, which differs from iOS
//
// `fontSizeMultiplier` is a prop, and React Native's JavaScript never sets it:
// it stays NaN. iOS gets its scaling from `dynamicTypeRamp` and `UIFontMetrics`
// instead, and Android gets it from the device configuration's `fontScale`,
// which is a scalar the platform owns. A desktop is the Android shape of the
// problem: GTK publishes a text scale through `GtkSettings:gtk-xft-dpi`, and
// Windows through `UISettings.TextScaleFactor`, so the host passes what its
// platform says and nothing has to invent one. macOS publishes no scalar at
// all, which is why its scale is 1 and why that is recorded rather than faked.
//
// So the two multiply, which is Android's arrangement: the system's scale times
// whatever the prop says. Not "the prop when it is set, the scale otherwise",
// which was the first version of this and was wrong in a way worth recording:
// `TextAttributes::defaultTextAttributes()` sets `fontSizeMultiplier` to **1**
// rather than to NaN, so every fragment arrives with one set and a prop-first
// rule cancels the platform's scale on every paragraph in the app. The probe
// that found it printed `size=16 scale=1.6`.
//
// The clamping is upstream's, copied from
// `RCTEffectiveFontSizeMultiplierFromTextAttributes`: `maxFontSizeMultiplier`
// applies only when it is at least 1, because 0 and NaN are both how React
// Native spells "no limit", and a limit below 1 would shrink text that nothing
// asked to shrink. It applies to the product, so it caps total growth, which is
// what an app asking for "no more than a quarter larger" means.
//
// `dynamicTypeRamp` is not here. It names a `UIFontMetrics` text style, which
// is an iOS API with no desktop equivalent; backlog/text.md records it.

#pragma once

#include <react/renderer/attributedstring/TextAttributes.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>

namespace basalt {

namespace detail {

inline std::atomic<float> &systemFontScaleStorage() {
  static std::atomic<float> scale{1.0F};
  return scale;
}

} // namespace detail

// What a test asked for, or nothing.
//
// `BASALT_TEST_FONT_SCALE` is here because two of the three platforms publish
// no text scale at all, so a scenario on them could otherwise never see the
// difference `allowFontScaling` makes, and a feature with no test that fails
// when it is removed is not finished.
//
// Not in core/TestSettle.h's list of scripted input, deliberately: that list is
// what a host schedules on its timer, and this schedules nothing. It is a
// setting read once, the way `BASALT_TEST_DIALOG` answers rather than acts.
inline float testFontScale() {
  static const float requested = []() -> float {
    const char *const value = std::getenv("BASALT_TEST_FONT_SCALE");
    if (value == nullptr || *value == '\0') {
      return 0.0F;
    }
    char *end = nullptr;
    const double parsed = std::strtod(value, &end);
    if (end == value || !(parsed > 0.0)) {
      return 0.0F;
    }
    return static_cast<float>(parsed);
  }();
  return requested;
}

// The scale the platform reports, which a host reads once at startup.
//
// Read once, and on the main thread, because that is where a platform's
// settings object may be touched: GTK measures text off the main thread, so
// asking `GtkSettings` from inside the text builder would be asking it from the
// wrong thread. The cost is that a scale changed while the app is running does
// not reflow text that is already laid out; backlog/text.md records the signal
// that would.
inline void setSystemFontScale(float scale) {
  detail::systemFontScaleStorage().store(scale > 0.0F ? scale : 1.0F,
                                         std::memory_order_relaxed);
}

// What the text builders multiply by. A test's scale wins over the platform's,
// which is the only way a platform that has none can be tested at all.
inline float systemFontScale() {
  const float requested = testFontScale();
  return requested > 0.0F
      ? requested
      : detail::systemFontScaleStorage().load(std::memory_order_relaxed);
}

// What `fontSize` is multiplied by, given what the platform reports.
//
// `systemScale` is the platform's text scale: 1 where there is none, and
// whatever the setting says where there is. A scale that is not a positive
// number is 1, because a zero or negative one would make text disappear and a
// setting is not worth trusting that far.
inline float effectiveFontSizeMultiplier(
    const facebook::react::TextAttributes &textAttributes,
    float systemScale) {
  if (!textAttributes.allowFontScaling.value_or(true)) {
    return 1.0F;
  }

  if (!(systemScale > 0.0F)) {
    systemScale = 1.0F;
  }
  // A multiplier that is not a positive number is 1 rather than nothing: NaN is
  // "unset" and zero would multiply the text away.
  const float requested = (!std::isnan(textAttributes.fontSizeMultiplier) &&
                           textAttributes.fontSizeMultiplier > 0.0)
      ? static_cast<float>(textAttributes.fontSizeMultiplier)
      : 1.0F;
  const float multiplier = requested * systemScale;

  const float limit = std::isnan(textAttributes.maxFontSizeMultiplier)
      ? 0.0F
      : static_cast<float>(textAttributes.maxFontSizeMultiplier);
  return limit >= 1.0F ? std::min(limit, multiplier) : multiplier;
}

// The size a fragment is drawn at, in density-independent pixels.
//
// `defaultFontSize` is the host's own default, which differs between them
// because the platforms' system fonts do; `fontSize` being unset is NaN rather
// than zero.
inline float effectiveFontSize(
    const facebook::react::TextAttributes &textAttributes,
    float defaultFontSize,
    float systemScale) {
  const float size = std::isnan(textAttributes.fontSize)
      ? defaultFontSize
      : static_cast<float>(textAttributes.fontSize);
  return size * effectiveFontSizeMultiplier(textAttributes, systemScale);
}

} // namespace basalt
