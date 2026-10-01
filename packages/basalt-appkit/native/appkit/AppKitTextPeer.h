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
