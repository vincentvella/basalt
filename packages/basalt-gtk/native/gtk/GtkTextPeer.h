// The six things a <TextInput> does to its peer, for either kind of peer.
//
// A single-line field is a GtkText, which is a GtkEditable; a multiline one is
// a GtkTextView, which is not -- it owns a GtkTextBuffer and every one of
// GtkEditable's calls has a different name and a different shape there. Rather
// than branch at each of the fifteen call sites in GtkTextInput.cpp, they go
// through here.
//
// C rather than C++ because RnView.cpp uses it too, and that file deliberately
// knows nothing about React Native or about C++ types crossing its boundary.
//
// Offsets are in characters, not bytes. GtkEditable counts characters and
// GtkTextBuffer offers both; React Native means characters, so that is what
// crosses this seam.

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

// Builds the peer for a field. Multiline gets a GtkTextView with wrapping on,
// which is the whole visible difference: the measured box is already the right
// height, and without wrapping the text would run off the end of one line
// inside it.
GtkWidget *rn_peer_new(gboolean multiline);

gboolean rn_peer_is_multiline(GtkWidget *peer);

// The peer's whole contents. Never NULL; free with g_free.
char *rn_peer_get_text(GtkWidget *peer);

void rn_peer_set_text(GtkWidget *peer, const char *text);

// The caret, as a character offset.
int rn_peer_get_position(GtkWidget *peer);
void rn_peer_set_position(GtkWidget *peer, int position);

// The selection, or FALSE with the out parameters untouched when there is
// none -- which is GtkEditable's contract, kept for both so callers need only
// learn one.
gboolean rn_peer_get_selection_bounds(GtkWidget *peer, int *start, int *end);
void rn_peer_select_region(GtkWidget *peer, int start, int end);

void rn_peer_set_editable(GtkWidget *peer, gboolean editable);

// `spellCheck`, as GTK has it: an input *hint*, which is a suggestion to the
// input method rather than an instruction to a checker. Three states, because
// unset is not false; see core/TextChecking.h.
//
// There is no hint for `autoCorrect`. GTK's nearest is `WORD_COMPLETION`, which
// offers completions rather than correcting what was typed, so the prop is
// recorded instead of approximated. backlog/text.md has it.
//
// 0 unset, 1 on, 2 off, matching core's enum. An int rather than the enum
// because this header is plain C against GTK and its callers include it as
// such.
void rn_peer_set_spell_check(GtkWidget *peer, int flag);

// `autoCapitalize`, which GTK has as three more input hints: capitalise every
// character, every word, or the first word of each sentence. `none` is the
// absence of all three rather than `GTK_INPUT_HINT_LOWERCASE`, which asks for
// something else entirely -- lowercasing what was typed.
//
// React Native's default is `sentences`, as on iOS, and the prop is a plain
// enum with no "did not say", so a hint is always set. 0 none, 1 words,
// 2 sentences, 3 characters, matching core's enum order.
void rn_peer_set_auto_capitalize(GtkWidget *peer, int type);

// `keyboardType`, which GTK has as an input *purpose*: what the text is for,
// which is what decides an on-screen keyboard's layout and what an input
// method offers. A desktop with a hardware keyboard shows nothing different,
// which is why this is a purpose rather than a keyboard -- and why a Linux
// tablet is the machine where it shows.
//
// Takes GTK's own enum value, chosen in GtkTextInput.cpp from React Native's
// fourteen keyboard types: the mapping is a platform decision and belongs
// beside the platform.
void rn_peer_set_input_purpose(GtkWidget *peer, int purpose);

// Where the "changed" and cursor signals live: the widget for a GtkText, the
// buffer for a GtkTextView. Callers connect to this rather than to the peer.
GObject *rn_peer_signal_source(GtkWidget *peer);

G_END_DECLS

G_BEGIN_DECLS

// The app's font and colour, which neither peer takes the same way.
//
// A GtkText takes a PangoAttrList directly. A GtkTextView has no such
// property: the equivalent is a tag applied over the whole buffer, refreshed
// whenever the style or the text changes. Both matter -- a peer left in the
// GTK theme's font and colour is dark text on a dark field, which is the bug
// the single-line path already had a comment about.
void rn_peer_set_attributes(GtkWidget *peer, PangoAttrList *attributes);

// The placeholder. A GtkText has a property for it; a GtkTextView has none, so
// its peer is a subclass that draws one when the buffer is empty.
void rn_peer_set_placeholder(GtkWidget *peer, const char *placeholder);
const char *rn_peer_get_placeholder(GtkWidget *peer);

// The three colours a text field takes from CSS rather than from the
// PangoAttrList above: `placeholderTextColor`, `selectionColor` and
// `cursorColor`.
//
// Neither peer has a property for any of them. The placeholder, the selection
// and the caret are a CSS node, a CSS node and a CSS property respectively, and
// a PangoAttrList describes none of the three -- which is the whole reason
// these props were parsed and then dropped on this host for as long as they
// were.
//
// Each colour is NULL when the prop was absent, and absent is not transparent:
// a colour nobody asked for has to leave GTK's own theme colour alone rather
// than resolve to black. That is why these are pointers rather than a GdkRGBA
// each.
void rn_peer_set_colors(GtkWidget *peer,
                        const GdkRGBA *placeholder,
                        const GdkRGBA *selection,
                        const GdkRGBA *cursor);

// The CSS class carrying this peer's colours, or NULL when none of the three
// props was set. Interned, so it outlives the peer and can be compared by
// pointer.
const char *rn_peer_get_colors_class(GtkWidget *peer);

// The CSS rules behind that class, or NULL when there are none.
//
// Here because a widget's resolved style is not reachable without
// GtkStyleContext, which GTK 4.10 deprecated and 4.22 has already moved under
// gtk/deprecated/. So this is the only way anything without a screen -- the
// test suite, in practice -- can see that a colour reached GTK at all rather
// than being parsed and dropped the way these three used to be. Owned by the
// peer; valid until the colours are set again.
const char *rn_peer_get_colors_css(GtkWidget *peer);

// The placeholder colour the multiline peer will draw with, which is the one
// colour of the three that does not go through CSS: a GtkTextView has no
// placeholder node because this file draws its placeholder itself.
//
// FALSE with `out` untouched when no colour was set, and FALSE for a GtkText,
// whose placeholder GTK draws from the stylesheet above.
gboolean rn_peer_get_placeholder_color(GtkWidget *peer, GdkRGBA *out);

// Hidden characters, for `secureTextEntry`. Single line only: a multiline
// secure field is not a thing React Native offers, and GtkTextView has no
// visibility property.
void rn_peer_set_visibility(GtkWidget *peer, gboolean visible);

// `maxLength`. Single line only -- GtkTextBuffer has no equivalent, and
// enforcing it by hand would fight the controlled loop.
void rn_peer_set_max_length(GtkWidget *peer, int max_length);

G_END_DECLS

G_BEGIN_DECLS

// Typing, as the user would. `position` is in/out: it arrives as where to
// insert and leaves as where the caret ended up, which is GtkEditable's
// contract and is what makes inserting twice in a row work.
void rn_peer_insert_text(GtkWidget *peer, const char *text, int length, int *position);

G_END_DECLS
