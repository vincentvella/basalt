// The same seam GtkTextPeer.h is, for AppKit.
//
// A single-line field is an NSTextField and a multiline one is an NSTextView,
// and they agree on almost nothing. An NSTextField has a `stringValue`, no
// selection of its own -- the window lends it a field editor while it has
// focus -- a `placeholderString`, and a `bezeled` flag. An NSTextView has a
// `string`, owns its own selection because it *is* the editor, has no
// placeholder, and draws its background through a different property.
//
// Rather than branch at each of the twenty-eight call sites in
// AppKitTextInput.mm, they go through here. Functions over a protocol because
// RnAppKitView needs the text for its tree dump and knows nothing about either
// concrete class.
//
// Offsets are in UTF-16 units, which is what NSRange means and what
// NSAttributedString counts -- the same thing React Native's JavaScript means
// by a string index. The GTK seam says characters for the same reason: each
// platform's native unit is the one its string type uses.

#pragma once

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

// What a peer tells the manager when it takes focus. NSTextField cannot report
// blur usefully -- its field editor is the first responder, not it -- so only
// the gaining half is here; the delegate supplies the rest.
@protocol RnAppKitTextPeerOwner <NSObject>
- (void)rnFieldDidBecomeFirstResponder:(NSInteger)tag;
@end

// Builds the peer. `secure` and `multiline` are exclusive: React Native has no
// multiline secure field, and NSTextView has no way to be one.
NSView *RnPeerNew(BOOL multiline, BOOL secure, NSInteger tag, id owner, id delegate);

BOOL RnPeerIsMultiline(NSView *_Nullable peer);

NSString *RnPeerText(NSView *_Nullable peer);
void RnPeerSetText(NSView *_Nullable peer, NSString *text);

// The selection, in UTF-16 units. An unfocused NSTextField has no field editor
// and so no selection; the caret is reported at the end of its text, which is
// where one would appear.
NSRange RnPeerSelection(NSView *_Nullable peer);
void RnPeerSetSelection(NSView *_Nullable peer, NSRange range);

void RnPeerSetEditable(NSView *_Nullable peer, BOOL editable);

// `contextMenuHidden`: whether a right-click or a Control-click opens the
// peer's own editing menu.
//
// AppKit builds that menu on demand through `-menuForEvent:`, so the peers
// refuse there rather than having a menu taken away from them -- which is the
// one place that covers both gestures, and leaves the menu intact for a field
// that stops asking.
void RnPeerSetContextMenuHidden(NSView *_Nullable peer, BOOL hidden);
BOOL RnPeerContextMenuHidden(NSView *_Nullable peer);

// `spellCheck` and `autoCorrect`, as three states: see core/TextChecking.h for
// why unset is not false. Mirrored here rather than taken from that header
// because this one is plain Objective-C and its callers should stay that way.
typedef NS_ENUM(NSInteger, RnTextChecking) {
  RnTextCheckingUnset,
  RnTextCheckingOn,
  RnTextCheckingOff,
};

// Applies both, and remembers them.
//
// Remembering is the point: the two properties live on an NSTextView, and a
// single-line field is an NSTextField which *borrows* one -- its field editor,
// which exists only while it is focused. So a field asked before it is focused
// has nowhere to put them, and the peer applies them again when the editor
// appears.
void RnPeerSetTextChecking(NSView *_Nullable peer,
                           RnTextChecking spellCheck,
                           RnTextChecking autoCorrect);

// What the peer was last asked for, which is not always what AppKit did with
// it: macOS gates continuous spell checking on a user-wide setting
// (`NSAllowContinuousSpellChecking`), and refuses a per-view request when the
// person has turned the feature off for everything. So the request is what a
// test can assert on any machine, and AppKit's answer is asserted only where
// the machine allows it.
RnTextChecking RnPeerSpellCheck(NSView *_Nullable peer);
RnTextChecking RnPeerAutoCorrect(NSView *_Nullable peer);

// `selectionColor` and `cursorColor`, which live on the same NSTextView the
// spell-checking flags do and are remembered for the same reason: a single-line
// field borrows the window's field editor and only has one while it is focused.
// So these are stored and applied again in `becomeFirstResponder`.
//
// Null means "the app did not ask", which leaves AppKit's own colour alone --
// the system accent for a selection and the text colour for a caret. That is a
// third state rather than a default, exactly as it is for the checking flags:
// `SharedColor`'s unset value is zero, and passing that through would paint
// every caret black.
//
// The caret is `insertionPointColor` and the selection is the background colour
// inside `selectedTextAttributes`, which is a dictionary this replaces one key
// of rather than wholesale: the rest of it is AppKit's, including whether the
// selection dims when the window loses focus.
void RnPeerSetEditorColours(NSView *_Nullable peer,
                            NSColor *_Nullable selection,
                            NSColor *_Nullable caret);
NSColor *_Nullable RnPeerSelectionColour(NSView *_Nullable peer);
NSColor *_Nullable RnPeerCaretColour(NSView *_Nullable peer);
void RnPeerSetTextStyle(NSView *_Nullable peer, NSFont *_Nullable font,
                        NSColor *_Nullable colour, NSTextAlignment alignment);

// An NSTextField has a placeholder property; an NSTextView has none, so its
// peer draws one when it is empty. Same call for both.
void RnPeerSetPlaceholder(NSView *_Nullable peer, NSAttributedString *_Nullable placeholder);
NSAttributedString *_Nullable RnPeerPlaceholder(NSView *_Nullable peer);

// `maxLength`. Zero means no limit.
//
// AppKit offers nothing for this on either peer, so both refuse the edit that
// would cross the limit: an NSTextView through its delegate, and an NSTextField
// through an NSFormatter, which is the documented way to constrain one.
void RnPeerSetMaxLength(NSView *_Nullable peer, NSInteger maxLength);

// Whether an edit is allowed, for the delegate to ask. Refusing whole rather
// than truncating, so a paste that would overflow is rejected instead of
// half-applied.
BOOL RnPeerAllowsChange(NSView *_Nullable peer, NSRange range, NSString *_Nullable replacement);

// The responder that does the editing: the field editor for an NSTextField,
// and the view itself for an NSTextView, which is its own editor. This is what
// tells whether a key or a selection change belongs to this peer.
NSResponder *_Nullable RnPeerEditor(NSView *_Nullable peer);

NS_ASSUME_NONNULL_END
