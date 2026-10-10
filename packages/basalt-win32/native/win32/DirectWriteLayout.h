// AttributedString -> RnWin32TextLayout, and nothing else.
//
// Separate from RnWin32TextLayout.h for the reason that file states: the view
// layer draws paragraphs and must not gain a React Native dependency to do it.
// This is the file that does know about React Native, and it is the only place
// a `TextAttributes` is turned into a `RnTextStyle`.
//
// Shared by measurement and painting -- `DirectWriteLayoutManager.cpp` calls it
// from `TextLayoutManager::measure`, and `Win32MountingManager` calls it when a
// Paragraph's state arrives. That sharing is the point: if the two built
// layouts differently, Yoga would allot a box computed one way and the view
// would paint text laid out another. `docs/DECISIONS.md` makes the same
// argument for Pango.

#pragma once

#include <optional>

#include "FontFitting.h"
#include "RnWin32TextLayout.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/attributedstring/TextAttributes.h>

#include <memory>
#include <string>

namespace basalt::win32 {

// One fragment's attributes, as this platform understands them. Exposed so the
// mounting manager can ask the same question about a lone run without building
// a whole paragraph.
// `fit` is `adjustsFontSizeToFit`'s answer: the ratio the font size is
// multiplied by and the two bounds it is clamped against. The default changes
// nothing, which is what a fragment in a paragraph that never asked gets.
// `rightToLeft` is the paragraph's resolved direction, which DirectWrite needs
// told: it never asks the text. Left out, the prop alone decides, which is what
// a caller with no paragraph to resolve means -- a `<TextInput>`. See
// core/TextDirection.h.
RnTextStyle buildTextStyle(const facebook::react::TextAttributes &textAttributes,
                           basalt::FontFit fit = {},
                           std::optional<bool> rightToLeft = std::nullopt);

// One fragment's text with its `textTransform` applied, which has to happen
// before anything measures the string. Exposed for the suite, and for the same
// reason the AppKit host exposes its own: the case mapping is this platform's
// (`LCMapStringEx`), and whether it agrees with GLib's and NSString's on the
// awkward cases (the German sharp s, an accented letter, a word starting with a
// digit) is a thing to assert rather than hope for.
std::string transformedFragmentText(const facebook::react::TextAttributes &textAttributes,
                                    const std::string &text);

std::shared_ptr<RnWin32TextLayout>
buildTextLayout(const facebook::react::AttributedString &attributedString,
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

} // namespace basalt::win32
