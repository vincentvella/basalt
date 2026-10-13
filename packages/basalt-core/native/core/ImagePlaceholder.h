// Which image a view draws before the real one arrives.
//
// `<Image defaultSource>` is an image to show while the source loads, and
// `loadingIndicatorSource` is Android's spelling of the same idea. React
// Native's own rule, from `RCTImageComponentView`, is the part worth copying
// and the part each host would otherwise get differently:
//
//   - the placeholder is drawn as soon as it is available, which on a bundled
//     asset is the same frame;
//   - the real image replaces it when it arrives;
//   - and when the real image *fails*, the placeholder stays. A broken remote
//     image showing the placeholder is iOS's behaviour and is better than a
//     view that goes empty, which is what every host here did.
//
// The choice between the two props is here because it is not a host's: an app
// sets one or the other, Android's `Image.js` refuses both at once with a
// message of its own, and nothing below the props layer should have an opinion
// about which it prefers.
//
// Only the names. Loading, decoding and drawing differ per host -- a
// GdkTexture, a CGImage, an RnWin32Image -- and each already knows how to ask
// its loader for a uri.

#pragma once

#include <react/renderer/components/image/ImageProps.h>

#include <string>

namespace basalt {

// The uri to draw until the source lands, or empty when the app named none.
//
// `defaultSource` first, because an app that sets both has been told by
// Android's JavaScript that it may not, and the one React Native documents for
// every platform is the one to honour.
inline std::string imagePlaceholderUri(const facebook::react::ImageProps &props) {
  if (!props.defaultSource.uri.empty()) {
    return props.defaultSource.uri;
  }
  return props.loadingIndicatorSource.uri;
}

} // namespace basalt
