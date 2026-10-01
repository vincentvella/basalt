// Turning React Native's AttributedString into something Core Text can lay out.
//
// The counterpart of basalt-gtk's PangoTextLayout, and it exists
// for the same reason: this is the one place that knows both halves, and it is
// deliberately used by both sides of the text problem.
//
//   - `TextLayoutManager::measure` (CoreTextLayoutManager.mm) builds a layout to
//     ask Core Text how big the text is, so Yoga can lay it out.
//   - `AppKitMountingManager` builds the same layout and hands it to the view,
//     which draws it.
//
// Measuring and painting must agree, so they must not build layouts
// differently. Sharing this is what guarantees that.
//
// Unlike the Pango side, nothing here needs a mutex. Core Text is documented as
// thread-safe, and Fabric measures on its layout thread while AppKit draws on
// the main one. Pango's font map is not, which is why that file serialises
// every call and this one does not.
//
// The paragraph object itself is in RnTextLayout.h, which has no React Native in
// it, so the view layer can draw one without depending on React Native.

#pragma once

#import "RnTextLayout.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/attributedstring/TextAttributes.h>

namespace basalt {

// Builds a layout for `attributedString`.
RnTextLayout *buildTextLayout(const facebook::react::AttributedString &attributedString,
                              const facebook::react::ParagraphAttributes &paragraphAttributes);

// The attributes for a whole string, for anything that holds its own text
// rather than a layout built here -- the equivalent of Pango's
// buildTextAttributes, and what a <TextInput> will need.
NSDictionary<NSAttributedStringKey, id> *buildTextAttributes(
    const facebook::react::TextAttributes &textAttributes);

} // namespace basalt
