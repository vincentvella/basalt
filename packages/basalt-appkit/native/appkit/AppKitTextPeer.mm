#import "AppKitTextPeer.h"

#import <objc/runtime.h>

// Both peers report gaining focus the same way, and neither superclass does it
// for us. See the protocol's header comment for why only the gaining half.
@interface RnAppKitTextField : NSTextField
@property(nonatomic, assign) NSInteger rnTag;
@property(nonatomic, weak) id<RnAppKitTextPeerOwner> rnOwner;
@end

@implementation RnAppKitTextField
- (BOOL)becomeFirstResponder {
  const BOOL became = [super becomeFirstResponder];
  if (became) {
    [_rnOwner rnFieldDidBecomeFirstResponder:_rnTag];
  }
  return became;
}
@end

@interface RnAppKitSecureTextField : NSSecureTextField
@property(nonatomic, assign) NSInteger rnTag;
@property(nonatomic, weak) id<RnAppKitTextPeerOwner> rnOwner;
@end

@implementation RnAppKitSecureTextField
- (BOOL)becomeFirstResponder {
  const BOOL became = [super becomeFirstResponder];
  if (became) {
    [_rnOwner rnFieldDidBecomeFirstResponder:_rnTag];
  }
  return became;
}
@end

@interface RnAppKitTextView : NSTextView
@property(nonatomic, assign) NSInteger rnTag;
@property(nonatomic, weak) id<RnAppKitTextPeerOwner> rnOwner;
@property(nonatomic, strong, nullable) NSAttributedString *rnPlaceholder;
@end

@implementation RnAppKitTextView
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

NSView *RnPeerNew(BOOL multiline, BOOL secure, NSInteger tag, id owner, id delegate) {
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
    return view;
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

BOOL RnPeerIsMultiline(NSView *peer) {
  return peer != nil && [peer isKindOfClass:[NSTextView class]];
}

NSString *RnPeerText(NSView *peer) {
  if (peer == nil) {
    return @"";
  }
  if (RnPeerIsMultiline(peer)) {
    NSString *text = ((NSTextView *)peer).string;
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
    ((NSTextView *)peer).string = value;
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
    return ((NSTextView *)peer).selectedRange;
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
    NSTextView *view = (NSTextView *)peer;
    const NSUInteger length = view.string.length;
    const NSUInteger location = MIN(range.location, length);
    [view setSelectedRange:NSMakeRange(location, MIN(range.length, length - location))];
    return;
  }
  if (NSText *editor = ((NSTextField *)peer).currentEditor) {
    editor.selectedRange = range;
  }
}

void RnPeerSetEditable(NSView *peer, BOOL editable) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    NSTextView *view = (NSTextView *)peer;
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
    NSTextView *view = (NSTextView *)peer;
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
    RnAppKitTextView *view = (RnAppKitTextView *)peer;
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
    return ((RnAppKitTextView *)peer).rnPlaceholder;
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
    objc_setAssociatedObject(peer, @selector(RnPeerSetMaxLength), @(maxLength),
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
  NSNumber *limit = objc_getAssociatedObject(peer, @selector(RnPeerSetMaxLength));
  const NSInteger maxLength = limit != nil ? limit.integerValue : 0;
  if (maxLength <= 0) {
    return YES;
  }
  const NSInteger current = (NSInteger)((NSTextView *)peer).string.length;
  const NSInteger after = current - (NSInteger)range.length + (NSInteger)replacement.length;
  return after <= maxLength;
}

NSResponder *RnPeerEditor(NSView *peer) {
  if (peer == nil) {
    return nil;
  }
  // An NSTextView is the editor; an NSTextField borrows one from the window
  // and only while it has focus.
  return RnPeerIsMultiline(peer) ? (NSResponder *)peer : ((NSTextField *)peer).currentEditor;
}
