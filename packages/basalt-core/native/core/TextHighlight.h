// What a selection looks like.
//
// A colour, in one place, for the same reason `core/FocusRing.h` holds the
// focus ring's: a highlight that is the GTK theme's blue on one desktop,
// `NSColor.selectedTextBackgroundColor` on another and `COLOR_HIGHLIGHT` on a
// third is a difference an app did not ask for and cannot control. React Native
// has no prop for it either -- `selectionColor` belongs to `<TextInput>`, not
// to a paragraph -- so there is nothing an app could use to put the three back
// in step afterwards.
//
// The focus ring's blue, at the alpha a wash wants. The highlight is drawn
// *under* the glyphs and nothing recolours them, so it has to leave text of any
// colour legible through it; a solid accent with white text is the other
// convention and needs a second decision about the text, which React Native
// gave the paragraph and this platform does not get to overrule.
//
// Separate from core/TextSelection.h, which is the state machine, because each
// host's *widget* layer paints this and has no React Native in it: the same
// split core/TextVerticalAlign.h is on the other side of.

#pragma once

namespace basalt {

constexpr float kTextSelectionRed = 0.259F;
constexpr float kTextSelectionGreen = 0.522F;
constexpr float kTextSelectionBlue = 0.957F;
constexpr float kTextSelectionAlpha = 0.35F;

} // namespace basalt
