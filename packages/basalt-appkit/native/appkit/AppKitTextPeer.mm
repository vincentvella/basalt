#import "AppKitTextPeer.h"

#import <objc/runtime.h>

// Both peers report gaining focus the same way, and neither superclass does it
// for us. See the protocol's header comment for why only the gaining half.
// The two properties, on an NSTextView. Unset leaves the platform's own answer
// alone, which is the whole reason the flag has three states.
static void RnApplyTextChecking(NSTextView *_Nullable editor,
                                RnTextChecking spellCheck,
                                RnTextChecking autoCorrect) {
  if (editor == nil) {
    return;
  }
  if (spellCheck != RnTextCheckingUnset) {
    editor.continuousSpellCheckingEnabled = spellCheck == RnTextCheckingOn;
  }
  if (autoCorrect != RnTextCheckingUnset) {
    editor.automaticSpellingCorrectionEnabled = autoCorrect == RnTextCheckingOn;
  }
}

// `selectionColor` and `cursorColor`, on the same NSTextView and under the same
// rule: nil leaves AppKit's own answer alone, which is the third state the
// props have.
static void RnApplyEditorColours(NSTextView *_Nullable editor,
                                 NSColor *_Nullable selection,
                                 NSColor *_Nullable caret) {
  if (editor == nil) {
    return;
  }
  if (caret != nil) {
    editor.insertionPointColor = caret;
  }
  if (selection != nil) {
    // One key of the dictionary rather than the whole of it: the rest is
    // AppKit's, including how a selection dims when its window loses focus.
    NSMutableDictionary *attributes = [editor.selectedTextAttributes mutableCopy];
    if (attributes == nil) {
      attributes = [NSMutableDictionary dictionary];
    }
    attributes[NSBackgroundColorAttributeName] = selection;
    editor.selectedTextAttributes = attributes;
  }
}

@interface RnAppKitTextField : NSTextField
@property(nonatomic, assign) NSInteger rnTag;
@property(nonatomic, strong, nullable) NSColor *rnSelectionColour;
@property(nonatomic, strong, nullable) NSColor *rnCaretColour;
@property(nonatomic, weak) id<RnAppKitTextPeerOwner> rnOwner;
@property(nonatomic, assign) RnTextChecking rnSpellCheck;
@property(nonatomic, assign) RnTextChecking rnAutoCorrect;
// `contextMenuHidden`. A flag rather than a torn-down menu: AppKit builds the
// menu on demand, through -menuForEvent:, so refusing there is the one place
// that covers a right-click and a Control-click both.
@property(nonatomic, assign) BOOL rnContextMenuHidden;
@end

@implementation RnAppKitTextField
- (NSMenu *)menuForEvent:(NSEvent *)event {
  // `contextMenuHidden`: nil is how a view says it has no context menu, and
  // AppKit then offers none rather than falling back to a default.
  if (_rnContextMenuHidden) {
    return nil;
  }
  return [super menuForEvent:event];
}

// The same refusal, for the menu this field does not own.
//
// A focused single-line field is edited through the *window's shared field
// editor*, a plain NSTextView nobody here subclasses, and that view is the one
// under the pointer: -menuForEvent: above is never asked, so the flag reached
// the field and stopped there. Assigning `menu = nil` on the editor does not
// help either, which a probe established before this was written -- the editor
// rebuilds its menu inside its own -menuForEvent:.
//
// This is the documented way into that rebuild. A field editor's delegate is
// the control it is editing for, which is this field, and NSTextView asks its
// delegate to approve the menu it has just built. Returning nil is the whole
// of it.
- (NSMenu *)textView:(NSTextView *)view
                menu:(NSMenu *)menu
            forEvent:(NSEvent *)event
             atIndex:(NSUInteger)index {
  (void)view;
  (void)event;
  (void)index;
  if (_rnContextMenuHidden) {
    return nil;
  }
  return menu;
}

- (BOOL)becomeFirstResponder {
  const BOOL became = [super becomeFirstResponder];
  if (became) {
    // The field editor exists now and did not before, which is the only moment
    // the spell-checking flags can be put on it. See RnPeerSetTextChecking.
    if ([self.currentEditor isKindOfClass:NSTextView.class]) {
      RnApplyTextChecking((NSTextView *)self.currentEditor, _rnSpellCheck, _rnAutoCorrect);
      RnApplyEditorColours(
          (NSTextView *)self.currentEditor, _rnSelectionColour, _rnCaretColour);
    }
    [_rnOwner rnFieldDidBecomeFirstResponder:_rnTag];
  }
  return became;
}
@end

@interface RnAppKitSecureTextField : NSSecureTextField
@property(nonatomic, assign) NSInteger rnTag;
@property(nonatomic, strong, nullable) NSColor *rnSelectionColour;
@property(nonatomic, strong, nullable) NSColor *rnCaretColour;
@property(nonatomic, weak) id<RnAppKitTextPeerOwner> rnOwner;
@property(nonatomic, assign) RnTextChecking rnSpellCheck;
@property(nonatomic, assign) RnTextChecking rnAutoCorrect;
// `contextMenuHidden`. A flag rather than a torn-down menu: AppKit builds the
// menu on demand, through -menuForEvent:, so refusing there is the one place
// that covers a right-click and a Control-click both.
@property(nonatomic, assign) BOOL rnContextMenuHidden;
@end

@implementation RnAppKitSecureTextField
- (NSMenu *)menuForEvent:(NSEvent *)event {
  // `contextMenuHidden`: nil is how a view says it has no context menu, and
  // AppKit then offers none rather than falling back to a default.
  if (_rnContextMenuHidden) {
    return nil;
  }
  return [super menuForEvent:event];
}

// The same refusal, for the menu this field does not own.
//
// A focused single-line field is edited through the *window's shared field
// editor*, a plain NSTextView nobody here subclasses, and that view is the one
// under the pointer: -menuForEvent: above is never asked, so the flag reached
// the field and stopped there. Assigning `menu = nil` on the editor does not
// help either, which a probe established before this was written -- the editor
// rebuilds its menu inside its own -menuForEvent:.
//
// This is the documented way into that rebuild. A field editor's delegate is
// the control it is editing for, which is this field, and NSTextView asks its
// delegate to approve the menu it has just built. Returning nil is the whole
// of it.
- (NSMenu *)textView:(NSTextView *)view
                menu:(NSMenu *)menu
            forEvent:(NSEvent *)event
             atIndex:(NSUInteger)index {
  (void)view;
  (void)event;
  (void)index;
  if (_rnContextMenuHidden) {
    return nil;
  }
  return menu;
}

- (BOOL)becomeFirstResponder {
  const BOOL became = [super becomeFirstResponder];
  if (became) {
    // The field editor exists now and did not before, which is the only moment
    // the spell-checking flags can be put on it. See RnPeerSetTextChecking.
    if ([self.currentEditor isKindOfClass:NSTextView.class]) {
      RnApplyTextChecking((NSTextView *)self.currentEditor, _rnSpellCheck, _rnAutoCorrect);
      RnApplyEditorColours(
          (NSTextView *)self.currentEditor, _rnSelectionColour, _rnCaretColour);
    }
    [_rnOwner rnFieldDidBecomeFirstResponder:_rnTag];
  }
  return became;
}
@end

@interface RnAppKitTextView : NSTextView
@property(nonatomic, assign) NSInteger rnTag;
@property(nonatomic, strong, nullable) NSColor *rnSelectionColour;
@property(nonatomic, strong, nullable) NSColor *rnCaretColour;
@property(nonatomic, weak) id<RnAppKitTextPeerOwner> rnOwner;
@property(nonatomic, strong, nullable) NSAttributedString *rnPlaceholder;
@property(nonatomic, assign) RnTextChecking rnSpellCheck;
@property(nonatomic, assign) RnTextChecking rnAutoCorrect;
// `contextMenuHidden`. A flag rather than a torn-down menu: AppKit builds the
// menu on demand, through -menuForEvent:, so refusing there is the one place
// that covers a right-click and a Control-click both.
@property(nonatomic, assign) BOOL rnContextMenuHidden;
@end

@implementation RnAppKitTextView
- (NSMenu *)menuForEvent:(NSEvent *)event {
  // `contextMenuHidden`: nil is how a view says it has no context menu, and
  // AppKit then offers none rather than falling back to a default.
  if (_rnContextMenuHidden) {
    return nil;
  }
  return [super menuForEvent:event];
}

- (BOOL)becomeFirstResponder {
  const BOOL became = [super becomeFirstResponder];
  if (became) {
    [_rnOwner rnFieldDidBecomeFirstResponder:_rnTag];
  }
  return became;
}

// An NSTextView has no placeholder, so it is drawn. Over the top of whatever
// the view drew, which is nothing when the field is empty, and only when it
// is empty -- the one case a placeholder means anything.
- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];
  if (_rnPlaceholder == nil || self.string.length > 0) {
    return;
  }
  // The same origin the first line of real text would take, so the placeholder
  // does not shift when the user starts typing.
  [_rnPlaceholder drawAtPoint:NSMakePoint(self.textContainerInset.width +
                                              self.textContainer.lineFragmentPadding,
                                          self.textContainerInset.height)];
}

// Going from empty to one character, and back, changes whether the placeholder
// should be there at all -- and AppKit has no reason to know that.
- (void)didChangeText {
  [super didChangeText];
  self.needsDisplay = YES;
}
@end

NSView *RnPeerNew(BOOL multiline, BOOL secure, BOOL scrollEnabled, NSInteger tag, id owner,
                  id delegate) {
  if (multiline) {
    RnAppKitTextView *view = [[RnAppKitTextView alloc] initWithFrame:NSZeroRect];
    view.rnTag = tag;
    view.rnOwner = owner;
    view.delegate = (id<NSTextViewDelegate>)delegate;

    // The RnAppKitView behind it draws the background, border and corner
    // radius from React Native's style props, so the peer paints none of its
    // own -- the same reason the single-line field is unbezeled.
    view.drawsBackground = NO;
    view.focusRingType = NSFocusRingTypeNone;
    // No insets of its own: the view already places the peer inside the
    // content inset Yoga resolved.
    view.textContainerInset = NSZeroSize;
    view.textContainer.lineFragmentPadding = 0;
    // Wrap rather than run off the side. The measured box is already tall
    // enough for the wrapped text, because the shadow node measures a
    // multiline input against the real constraints.
    view.textContainer.widthTracksTextView = YES;
    view.horizontallyResizable = NO;
    view.verticallyResizable = YES;
    // Plain text: React Native styles the whole field through props, and rich
    // text would let a paste bring its own font in.
    view.richText = NO;
    view.allowsUndo = YES;

    if (!scrollEnabled) {
      // `scrollEnabled: false`: the text is clipped to the field rather than
      // scrolled to, which is what the prop asks for and what the other two
      // hosts do with it -- an `EDIT` without `ES_AUTOVSCROLL` on Windows, and
      // no scrolled window on GTK.
      //
      // Clipped by refusing to grow, and this is the part that was wrong when
      // the scroller went in beside it. A bare NSTextView with
      // `verticallyResizable` resizes itself to fit its text, and nothing above
      // it clips: React Native's `overflow` defaults to `visible`, so the
      // RnAppKitView does not. The field drew three lines of text out of a
      // one-line box and down the page behind it -- which is what every
      // multiline field on this host did before today, and what looking at the
      // running app found in a minute after three test suites had not.
      //
      // So the container tracks the view in both directions and the view stops
      // resizing: text past the bottom is never laid out, and so never drawn.
      view.verticallyResizable = NO;
      view.textContainer.widthTracksTextView = YES;
      view.textContainer.heightTracksTextView = YES;
      return view;
    }

    // Scrolling, which is the default. A scroll view with the text view as its
    // document view is how AppKit scrolls text, and not a wrapper over
    // something that would otherwise work: the clipping, the scroller, the
    // wheel and the elastic edges are the scroll view's, and
    // `scrollRangeToVisible:` -- which is what an NSTextView calls when the
    // caret moves -- walks up to one. Without it the caret can leave the box
    // and the user cannot follow it, which is what this platform did until now.
    //
    // Creation-time, like `multiline` and like Windows' style bit, so a field
    // that changes the prop has its peer rebuilt. The alternative is a scroll
    // view that is told not to scroll, which means overriding the wheel, the
    // scrollers *and* `scrollRangeToVisible:` to undo what it is for.
    NSScrollView *scroller = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    // The RnAppKitView behind it draws the background, border and corner
    // radius from React Native's style props, so the scroller paints none of
    // its own -- the same reason the text view does not.
    scroller.drawsBackground = NO;
    scroller.borderType = NSNoBorder;
    // Vertical only: the text wraps, so there is nothing to scroll sideways
    // to, and `horizontallyResizable` is NO below for the same reason.
    scroller.hasVerticalScroller = YES;
    scroller.hasHorizontalScroller = NO;
    // Hidden until it is needed, because a scroller standing in a styled field
    // is furniture React Native did not ask for -- and the overlay style is
    // what every Mac app with a trackpad shows anyway.
    scroller.autohidesScrollers = YES;
    scroller.scrollerStyle = NSScrollerStyleOverlay;

    // The document view's width follows the clip view's, and its height
    // follows its text. This is upstream AppKit's own recipe for a text view in
    // a scroller, and each line matters: without the autoresizing mask the text
    // view keeps the width it was made with (zero), and without the container
    // tracking it the text lays out against a width the field does not have.
    view.autoresizingMask = NSViewWidthSizable;
    view.minSize = NSMakeSize(0, 0);
    view.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    view.textContainer.containerSize = NSMakeSize(0, FLT_MAX);
    view.textContainer.widthTracksTextView = YES;

    scroller.documentView = view;
    return scroller;
  }

  NSTextField *field;
  if (secure) {
    RnAppKitSecureTextField *secureField =
        [[RnAppKitSecureTextField alloc] initWithFrame:NSZeroRect];
    secureField.rnTag = tag;
    secureField.rnOwner = owner;
    field = secureField;
  } else {
    RnAppKitTextField *plain = [[RnAppKitTextField alloc] initWithFrame:NSZeroRect];
    plain.rnTag = tag;
    plain.rnOwner = owner;
    field = plain;
  }

  // One line, and clipped rather than wrapped. An NSTextField wraps by default
  // once its text outruns its width, which is not what a single-line
  // <TextInput> does anywhere else -- it scrolls horizontally. Invisible until
  // a field holds more than it can show, which is why it survived this long.
  field.usesSingleLineMode = YES;
  field.lineBreakMode = NSLineBreakByClipping;
  field.cell.scrollable = YES;
  field.cell.wraps = NO;

  field.bordered = NO;
  field.bezeled = NO;
  field.drawsBackground = NO;
  field.focusRingType = NSFocusRingTypeNone;
  field.delegate = (id<NSTextFieldDelegate>)delegate;
  // Return has to reach doCommandBySelector: rather than being swallowed as a
  // "do nothing" action, which is what an NSTextField with no target does.
  field.target = nil;
  field.action = nullptr;
  return field;
}

// The text view inside a multiline peer, or nil for a single-line one.
//
// A multiline peer is not always the text view any more. A field that scrolls
// -- `scrollEnabled`, which is React Native's default -- is an NSScrollView
// with the text view as its document view, because that is how AppKit scrolls
// text: the scroll view owns the clipping, the scrollers and the wheel, and
// `scrollRangeToVisible:` moves the caret into view through it. A field that
// asked not to scroll is the bare text view it always was, which clips.
//
// Everything in this file that used to cast the peer goes through here, so the
// arrangement is one function's business rather than twenty-eight call sites'.
static RnAppKitTextView *_Nullable RnPeerTextView(NSView *_Nullable peer) {
  if (peer == nil) {
    return nil;
  }
  if ([peer isKindOfClass:RnAppKitTextView.class]) {
    return (RnAppKitTextView *)peer;
  }
  if ([peer isKindOfClass:[NSScrollView class]]) {
    NSView *document = ((NSScrollView *)peer).documentView;
    if ([document isKindOfClass:RnAppKitTextView.class]) {
      return (RnAppKitTextView *)document;
    }
  }
  return nil;
}

BOOL RnPeerIsMultiline(NSView *peer) {
  return RnPeerTextView(peer) != nil;
}

NSView *RnPeerEditorView(NSView *peer) {
  if (RnAppKitTextView *view = RnPeerTextView(peer)) {
    return view;
  }
  return peer;
}

NSString *RnPeerText(NSView *peer) {
  if (peer == nil) {
    return @"";
  }
  if (RnPeerIsMultiline(peer)) {
    NSString *text = RnPeerTextView(peer).string;
    return text != nil ? text : @"";
  }
  NSString *value = ((NSTextField *)peer).stringValue;
  return value != nil ? value : @"";
}

void RnPeerSetText(NSView *peer, NSString *text) {
  if (peer == nil) {
    return;
  }
  NSString *value = text != nil ? text : @"";
  if (RnPeerIsMultiline(peer)) {
    RnPeerTextView(peer).string = value;
    return;
  }
  ((NSTextField *)peer).stringValue = value;
}

NSRange RnPeerSelection(NSView *peer) {
  if (peer == nil) {
    return NSMakeRange(0, 0);
  }
  if (RnPeerIsMultiline(peer)) {
    // An NSTextView is its own editor, so the selection is always available --
    // which is the one place multiline is simpler than single line here.
    return RnPeerTextView(peer).selectedRange;
  }
  NSTextField *field = (NSTextField *)peer;
  if (NSText *editor = field.currentEditor) {
    return editor.selectedRange;
  }
  return NSMakeRange(field.stringValue.length, 0);
}

void RnPeerSetSelection(NSView *peer, NSRange range) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    RnAppKitTextView *view = RnPeerTextView(peer);
    const NSUInteger length = view.string.length;
    const NSUInteger location = MIN(range.location, length);
    [view setSelectedRange:NSMakeRange(location, MIN(range.length, length - location))];
    return;
  }
  if (NSText *editor = ((NSTextField *)peer).currentEditor) {
    editor.selectedRange = range;
  }
}

void RnPeerSetTextChecking(NSView *peer,
                           RnTextChecking spellCheck,
                           RnTextChecking autoCorrect) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    // An NSTextView is its own editor, so this is the simple case -- the
    // request is still remembered, because AppKit may decline it and a test
    // needs to know what was asked.
    RnPeerTextView(peer).rnSpellCheck = spellCheck;
    RnPeerTextView(peer).rnAutoCorrect = autoCorrect;
    RnApplyTextChecking(RnPeerTextView(peer), spellCheck, autoCorrect);
    return;
  }
  // Remembered first, because a field that is not focused has no editor to put
  // them on and `becomeFirstResponder` is what applies them then.
  if ([peer isKindOfClass:RnAppKitTextField.class]) {
    ((RnAppKitTextField *)peer).rnSpellCheck = spellCheck;
    ((RnAppKitTextField *)peer).rnAutoCorrect = autoCorrect;
  } else if ([peer isKindOfClass:RnAppKitSecureTextField.class]) {
    ((RnAppKitSecureTextField *)peer).rnSpellCheck = spellCheck;
    ((RnAppKitSecureTextField *)peer).rnAutoCorrect = autoCorrect;
  }
  if ([((NSTextField *)peer).currentEditor isKindOfClass:NSTextView.class]) {
    RnApplyTextChecking(
        (NSTextView *)((NSTextField *)peer).currentEditor, spellCheck, autoCorrect);
  }
}

void RnPeerSetEditorColours(NSView *peer, NSColor *selection, NSColor *caret) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    // Its own editor, so the colours could go straight on -- they are still
    // remembered, so that reading them back answers what the app asked for
    // rather than what AppKit currently holds.
    RnPeerTextView(peer).rnSelectionColour = selection;
    RnPeerTextView(peer).rnCaretColour = caret;
    RnApplyEditorColours(RnPeerTextView(peer), selection, caret);
    return;
  }
  // Remembered first, because a field with no focus has no field editor to put
  // them on and `becomeFirstResponder` is what applies them then.
  if ([peer isKindOfClass:RnAppKitTextField.class]) {
    ((RnAppKitTextField *)peer).rnSelectionColour = selection;
    ((RnAppKitTextField *)peer).rnCaretColour = caret;
  } else if ([peer isKindOfClass:RnAppKitSecureTextField.class]) {
    ((RnAppKitSecureTextField *)peer).rnSelectionColour = selection;
    ((RnAppKitSecureTextField *)peer).rnCaretColour = caret;
  }
  if ([((NSTextField *)peer).currentEditor isKindOfClass:NSTextView.class]) {
    RnApplyEditorColours((NSTextView *)((NSTextField *)peer).currentEditor, selection, caret);
  }
}

NSColor *RnPeerSelectionColour(NSView *peer) {
  if (RnPeerTextView(peer) != nil) {
    return RnPeerTextView(peer).rnSelectionColour;
  }
  if ([peer isKindOfClass:RnAppKitTextField.class]) {
    return ((RnAppKitTextField *)peer).rnSelectionColour;
  }
  if ([peer isKindOfClass:RnAppKitSecureTextField.class]) {
    return ((RnAppKitSecureTextField *)peer).rnSelectionColour;
  }
  return nil;
}

NSColor *RnPeerCaretColour(NSView *peer) {
  if (RnPeerTextView(peer) != nil) {
    return RnPeerTextView(peer).rnCaretColour;
  }
  if ([peer isKindOfClass:RnAppKitTextField.class]) {
    return ((RnAppKitTextField *)peer).rnCaretColour;
  }
  if ([peer isKindOfClass:RnAppKitSecureTextField.class]) {
    return ((RnAppKitSecureTextField *)peer).rnCaretColour;
  }
  return nil;
}

// The remembered request, from whichever peer this is. A multiline view has no
// need to remember -- it is its own editor -- so its own flags are read back.
RnTextChecking RnPeerSpellCheck(NSView *peer) {
  if (peer == nil) {
    return RnTextCheckingUnset;
  }
  if ([peer isKindOfClass:RnAppKitTextField.class]) {
    return ((RnAppKitTextField *)peer).rnSpellCheck;
  }
  if ([peer isKindOfClass:RnAppKitSecureTextField.class]) {
    return ((RnAppKitSecureTextField *)peer).rnSpellCheck;
  }
  if (RnPeerTextView(peer) != nil) {
    return RnPeerTextView(peer).rnSpellCheck;
  }
  return RnTextCheckingUnset;
}

RnTextChecking RnPeerAutoCorrect(NSView *peer) {
  if (peer == nil) {
    return RnTextCheckingUnset;
  }
  if ([peer isKindOfClass:RnAppKitTextField.class]) {
    return ((RnAppKitTextField *)peer).rnAutoCorrect;
  }
  if ([peer isKindOfClass:RnAppKitSecureTextField.class]) {
    return ((RnAppKitSecureTextField *)peer).rnAutoCorrect;
  }
  if (RnPeerTextView(peer) != nil) {
    return RnPeerTextView(peer).rnAutoCorrect;
  }
  return RnTextCheckingUnset;
}

void RnPeerSetContextMenuHidden(NSView *peer, BOOL hidden) {
  if (peer == nil) {
    return;
  }
  // Every peer class this file makes carries the flag, and they are the only
  // classes that reach here: a secure field is not an NSTextField subclass of
  // this one's, so the three are named rather than tested for a common base.
  if (RnPeerTextView(peer) != nil) {
    RnPeerTextView(peer).rnContextMenuHidden = hidden;
    return;
  }
  if ([peer isKindOfClass:RnAppKitSecureTextField.class]) {
    ((RnAppKitSecureTextField *)peer).rnContextMenuHidden = hidden;
    return;
  }
  if ([peer isKindOfClass:RnAppKitTextField.class]) {
    ((RnAppKitTextField *)peer).rnContextMenuHidden = hidden;
  }
}

BOOL RnPeerContextMenuHidden(NSView *peer) {
  if (RnPeerTextView(peer) != nil) {
    return RnPeerTextView(peer).rnContextMenuHidden;
  }
  if ([peer isKindOfClass:RnAppKitSecureTextField.class]) {
    return ((RnAppKitSecureTextField *)peer).rnContextMenuHidden;
  }
  if ([peer isKindOfClass:RnAppKitTextField.class]) {
    return ((RnAppKitTextField *)peer).rnContextMenuHidden;
  }
  return NO;
}

void RnPeerSetEditable(NSView *peer, BOOL editable) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    RnAppKitTextView *view = RnPeerTextView(peer);
    view.editable = editable;
    view.selectable = YES;
    return;
  }
  NSTextField *field = (NSTextField *)peer;
  field.editable = editable;
  field.selectable = YES;
}

void RnPeerSetTextStyle(NSView *peer, NSFont *font, NSColor *colour, NSTextAlignment alignment) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    RnAppKitTextView *view = RnPeerTextView(peer);
    if (font != nil) {
      view.font = font;
    }
    if (colour != nil) {
      view.textColor = colour;
    }
    view.alignment = alignment;
    // What typing continues in. Without it the font and colour apply to what
    // is there and the next character reverts to the system default.
    NSMutableDictionary *typing = [NSMutableDictionary dictionary];
    if (font != nil) {
      typing[NSFontAttributeName] = font;
    }
    if (colour != nil) {
      typing[NSForegroundColorAttributeName] = colour;
    }
    view.typingAttributes = typing;
    return;
  }
  NSTextField *field = (NSTextField *)peer;
  if (font != nil) {
    field.font = font;
  }
  if (colour != nil) {
    field.textColor = colour;
  }
  field.alignment = alignment;
}

void RnPeerSetPlaceholder(NSView *peer, NSAttributedString *placeholder) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    RnAppKitTextView *view = RnPeerTextView(peer);
    view.rnPlaceholder = placeholder;
    view.needsDisplay = YES;
    return;
  }
  ((NSTextField *)peer).placeholderAttributedString = placeholder;
}

NSAttributedString *RnPeerPlaceholder(NSView *peer) {
  if (peer == nil) {
    return nil;
  }
  if (RnPeerIsMultiline(peer)) {
    return RnPeerTextView(peer).rnPlaceholder;
  }
  return ((NSTextField *)peer).placeholderAttributedString;
}

// A formatter that refuses anything longer than the limit. NSTextField has no
// maximum length of its own, and a formatter is what AppKit offers instead --
// it is consulted on every edit, which is exactly the hook needed.
@interface RnAppKitLengthFormatter : NSFormatter
@property(nonatomic, assign) NSInteger maxLength;
@end

@implementation RnAppKitLengthFormatter
- (NSString *)stringForObjectValue:(id)object {
  return [object isKindOfClass:[NSString class]] ? object : nil;
}

- (BOOL)getObjectValue:(out id *)object
             forString:(NSString *)string
      errorDescription:(out NSString **)error {
  if (object != nullptr) {
    *object = string;
  }
  return YES;
}

- (BOOL)isPartialStringValid:(NSString *)partial
            newEditingString:(NSString **)newString
            errorDescription:(NSString **)error {
  if (_maxLength <= 0 || (NSInteger)partial.length <= _maxLength) {
    return YES;
  }
  // NO with no replacement: the edit is refused and what was there stays.
  if (newString != nullptr) {
    *newString = nil;
  }
  if (error != nullptr) {
    *error = nil;
  }
  return NO;
}
@end

void RnPeerSetMaxLength(NSView *peer, NSInteger maxLength) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    // Stored, and read back by the delegate through RnPeerAllowsChange. An
    // NSTextView has no formatter.
    objc_setAssociatedObject(RnPeerTextView(peer), @selector(RnPeerSetMaxLength), @(maxLength),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return;
  }

  NSTextField *field = (NSTextField *)peer;
  if (maxLength <= 0) {
    field.formatter = nil;
    return;
  }
  RnAppKitLengthFormatter *formatter = [[RnAppKitLengthFormatter alloc] init];
  formatter.maxLength = maxLength;
  field.formatter = formatter;
}

BOOL RnPeerAllowsChange(NSView *peer, NSRange range, NSString *replacement) {
  if (peer == nil || !RnPeerIsMultiline(peer)) {
    return YES;
  }
  NSNumber *limit = objc_getAssociatedObject(RnPeerTextView(peer), @selector(RnPeerSetMaxLength));
  const NSInteger maxLength = limit != nil ? limit.integerValue : 0;
  if (maxLength <= 0) {
    return YES;
  }
  const NSInteger current = (NSInteger)RnPeerTextView(peer).string.length;
  const NSInteger after = current - (NSInteger)range.length + (NSInteger)replacement.length;
  return after <= maxLength;
}

NSResponder *RnPeerEditor(NSView *peer) {
  if (peer == nil) {
    return nil;
  }
  // An NSTextView is the editor; an NSTextField borrows one from the window
  // and only while it has focus.
  return RnPeerIsMultiline(peer) ? (NSResponder *)RnPeerTextView(peer)
                                 : ((NSTextField *)peer).currentEditor;
}
