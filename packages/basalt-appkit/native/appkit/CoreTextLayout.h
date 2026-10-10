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

#include "FontFitting.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/attributedstring/TextAttributes.h>

namespace basalt {

// Builds a layout for `attributedString`.
//
// `fit` is `adjustsFontSizeToFit`'s answer: the ratio every font size is
// multiplied by and the two bounds it is clamped against. The default is what a
// paragraph that never asked gets, which changes nothing -- see
// core/FontFitting.h.
RnTextLayout *buildTextLayout(const facebook::react::AttributedString &attributedString,
                              const facebook::react::ParagraphAttributes &paragraphAttributes,
                              basalt::FontFit fit = {});

// How far this paragraph's fonts have to shrink to fit a box, for
// `adjustsFontSizeToFit`.
//
// The search is core's; this is the half that measures, which means building a
// layout per probe -- about eight of them -- and is why it runs only for a
// paragraph that asked. Both the measurement seam and the mounting manager call
// it, with the constraints and the frame respectively, so the text is painted
// at the size it was measured at.
basalt::FontFit textFitScale(const facebook::react::AttributedString &attributedString,
                             const facebook::react::ParagraphAttributes &paragraphAttributes,
                             float maxWidth,
                             float maxHeight);

// One fragment's text with its `textTransform` applied, which has to happen
// before anything measures the string *and* before anything indexes into it: a
// transform can change the length -- ß uppercases to SS -- so an attachment
// after one would otherwise be looked up at the wrong offset. Shared with
// CoreTextLayoutManager.mm for exactly that reason.
NSString *transformedFragmentText(const facebook::react::AttributedString::Fragment &fragment,
                                  NSString *text);

// The attributes for a whole string, for anything that holds its own text
// rather than a layout built here -- the equivalent of Pango's
// buildTextAttributes, and what a <TextInput> will need.
// `rightToLeft` is the paragraph's resolved direction, which the relative
// alignments need; see core/TextDirection.h. A caller that holds its own text
// and resolves its own direction -- an NSTextField -- passes false and leaves
// `natural` to AppKit.
NSDictionary<NSAttributedStringKey, id> *buildTextAttributes(
    const facebook::react::TextAttributes &textAttributes,
    basalt::FontFit fit = {},
    bool rightToLeft = false);

// `fontVariant` in Core Text's vocabulary, which is not OpenType's: Apple's
// older AAT pairs, a feature type and a selector inside it, as upstream's
// `RCTFontFeatures` uses.
//
// Exposed for the suite rather than for callers. The mapping is the part this
// platform owns and a wrong selector would be invisible: `[NSFont
// fontWithDescriptor:]` resolves against a real font and **drops features that
// font does not have**, so asking the resolved font what it kept measures the
// machine's fonts rather than this code. The system font has small caps and
// tabular figures and has neither oldstyle figures nor a twentieth stylistic
// set, which is how that was found.
NSArray *fontFeaturesFor(const facebook::react::TextAttributes &textAttributes);

// The desktop's text scale, which on macOS is 1 and is a function anyway.
//
// macOS publishes no scalar for this. Accessibility's "Text size" works through
// the text-style ramp instead: an app opts in by asking
// `[NSFont preferredFontForTextStyle:options:]` for its fonts, and the system
// answers a larger font for the style. That is `dynamicTypeRamp`'s shape rather
// than `fontSizeMultiplier`'s, and it is a different feature; backlog/text.md
// records it with that call named.
//
// So this exists to say so in one place, and to keep the host's wiring the same
// shape as GTK's, which does have a scale to read.
float appKitTextScale();

} // namespace basalt
