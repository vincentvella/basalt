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
