#import "AppKitTextInput.h"

#import "AppKitTextPeer.h"

#import "CoreTextLayout.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>

#include <algorithm>
#include <cmath>
#include <string_view>

// The bridge between AppKit's delegate protocol and the C++ manager, for the
// same reason the touch dispatcher and the scroll manager have one.
@interface RnAppKitTextInputDelegate
    : NSObject <NSTextFieldDelegate, NSTextViewDelegate, RnAppKitTextPeerOwner>
@property(nonatomic, assign) basalt::AppKitTextInputManager *manager;
@property(nonatomic, strong) id keyMonitor;
@end

@implementation RnAppKitTextInputDelegate

static NSInteger RnTagOf(id object) {
  if ([object respondsToSelector:@selector(rnTag)]) {
    return [object rnTag];
  }
  return 0;
}

- (instancetype)init {
  self = [super init];
  if (self != nil) {
    // Object nil, because the field editor is on loan from the window and a
    // different one can arrive with the next focus -- there is nothing stable
    // to observe. The handler filters by which field is using it.
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(rnSelectionDidChange:)
                                                 name:NSTextViewDidChangeSelectionNotification
                                               object:nil];
    [self rnInstallKeyMonitor];
  }
  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  if (_keyMonitor != nil) {
    [NSEvent removeMonitor:_keyMonitor];
    _keyMonitor = nil;
  }
}

- (void)rnInstallKeyMonitor {
  // A local monitor rather than a delegate method, because AppKit has no hook
  // for an ordinary character reaching a field. `control:textView:
  // doCommandBySelector:` sees only the named commands -- newline, delete,
  // the arrows -- and the other route is supplying a custom field editor
  // through the *window's* delegate, which this manager does not own.
  //
  // A monitor sees the key on its way to the responder chain, so onKeyPress
  // fires before the edit and therefore before onChange, which is the order
  // React Native promises.
  __weak RnAppKitTextInputDelegate *weakSelf = self;
  _keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                      handler:^NSEvent *(NSEvent *event) {
    RnAppKitTextInputDelegate *strongSelf = weakSelf;
    if (strongSelf != nil && strongSelf.manager != nullptr) {
      strongSelf.manager->handleKeyDown((__bridge void *)event);
    }
    // Always passed on: this observes, it does not consume.
    return event;
  }];
}

- (void)rnSelectionDidChange:(NSNotification *)notification {
  if (_manager != nullptr) {
    _manager->handleSelectionChanged((__bridge void *)notification.object);
  }
}

- (void)controlTextDidChange:(NSNotification *)notification {
  if (_manager != nullptr) {
    _manager->handleChanged(static_cast<facebook::react::Tag>(RnTagOf(notification.object)));
  }
}

- (void)controlTextDidEndEditing:(NSNotification *)notification {
  if (_manager != nullptr) {
    _manager->handleBlur(static_cast<facebook::react::Tag>(RnTagOf(notification.object)));
  }
}

// The multiline halves of the two above. An NSTextView is not an NSControl, so
// it posts NSText's notifications through NSTextViewDelegate rather than
// NSControl's -- different names, same two moments.
// maxLength for a multiline peer, which has no formatter to hold it.
- (BOOL)textView:(NSTextView *)textView
    shouldChangeTextInRange:(NSRange)range
          replacementString:(NSString *)replacement {
  return RnPeerAllowsChange(textView, range, replacement != nil ? replacement : @"");
}

- (void)textDidChange:(NSNotification *)notification {
  if (_manager != nullptr) {
    _manager->handleChanged(static_cast<facebook::react::Tag>(RnTagOf(notification.object)));
  }
}

- (void)textDidEndEditing:(NSNotification *)notification {
  if (_manager != nullptr) {
    _manager->handleBlur(static_cast<facebook::react::Tag>(RnTagOf(notification.object)));
  }
}

- (void)rnFieldDidBecomeFirstResponder:(NSInteger)tag {
  if (_manager != nullptr) {
    _manager->handleFocus(static_cast<facebook::react::Tag>(tag));
  }
}

// Return. React Native calls this submitEditing, and follows it with
// endEditing on platforms where the field also gives up focus; an NSTextField
// keeps focus on Return, so only the submit is reported here -- the same
// asymmetry the GTK side has with GtkText's `activate`.
- (BOOL)control:(NSControl *)control
               textView:(NSTextView *)textView
    doCommandBySelector:(SEL)selector {
  (void)textView;
  if (selector == @selector(insertNewline:) && _manager != nullptr) {
    _manager->handleSubmit(static_cast<facebook::react::Tag>(RnTagOf(control)));
    return YES;
  }
  return NO;
}

@end

namespace basalt {

using facebook::react::AttributedString;
using facebook::react::ShadowView;
using facebook::react::Tag;
using facebook::react::TextInputEventEmitter;
using facebook::react::TextInputProps;

AppKitTextInputManager::AppKitTextInputManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {
  RnAppKitTextInputDelegate *delegate = [[RnAppKitTextInputDelegate alloc] init];
  delegate.manager = this;
  delegate_ = delegate;
}

AppKitTextInputManager::~AppKitTextInputManager() {
  ((RnAppKitTextInputDelegate *)delegate_).manager = nullptr;
  delegate_ = nil;
}

AppKitTextInputManager::Entry *AppKitTextInputManager::entryFor(Tag tag) {
  const auto it = entries_.find(tag);
  return it == entries_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

void AppKitTextInputManager::makeField(Entry &entry, bool secure, bool multiline) {
  // The contents come across. Nothing asked for the field to be cleared --
  // React changed one prop -- and the controlled loop will not put the text
  // back, because from its side `text` did not change. The GTK side carries it
  // the same way for the same reason.
  NSString *existing = RnPeerText(entry.field);
  [entry.field removeFromSuperview];

  NSView *peer = RnPeerNew(multiline ? YES : NO, secure ? YES : NO, entry.tag,
                           (id)delegate_, (id)delegate_);
  RnPeerSetText(peer, existing);

  entry.field = peer;
  entry.secure = secure;
  entry.multiline = multiline;
  entry.view.rnEditable = peer;
  [entry.view addSubview:peer];
}

void AppKitTextInputManager::update(RnAppKitView *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;

  const auto props = std::dynamic_pointer_cast<const TextInputProps>(shadowView.props);
  const bool secure = props != nullptr && props->traits.secureTextEntry;

  const bool multiline = props != nullptr && props->multiline;
  if (inserted || entry.field == nil || entry.secure != secure || entry.multiline != multiline) {
    // A secure field is a different class on AppKit, not a property, so
    // toggling secureTextEntry means building a new one. The text comes across;
    // focus does not, which is the honest limit of doing it this way.
    makeField(entry, secure, multiline);
  }

  // Yoga has resolved border and padding into the content inset; the field is
  // placed inside it, so `paddingHorizontal` on a field means what it means on
  // a <View>.
  const auto &frame = shadowView.layoutMetrics.frame;
  const auto &insets = shadowView.layoutMetrics.contentInsets;
  const CGFloat innerWidth = (CGFloat)frame.size.width - insets.left - insets.right;
  const CGFloat innerHeight = (CGFloat)frame.size.height - insets.top - insets.bottom;
  entry.field.frame = NSMakeRect(insets.left,
                                 insets.top,
                                 std::max<CGFloat>(0, innerWidth),
                                 std::max<CGFloat>(0, innerHeight));

  if (props == nullptr) {
    return;
  }

  // Controlled component: JavaScript owns the value. Three things have to be
  // true at once, and each fails differently.
  //
  // Setting it must not look like typing, or the change we report provokes a
  // re-render that sets it again and the two chase each other -- `applying`.
  //
  // A prop older than what the user has since typed must not be applied at all,
  // or a fast typist watches characters reorder themselves. That is what
  // React Native counts events for, and dropping such a value *without
  // recording it* is deliberate: the next render, once JavaScript has caught
  // up, applies it.
  //
  // And it is applied when the prop *changes*, not when it differs from the
  // field, which is the only thing that tells a controlled field from an
  // uncontrolled one. See the header.
  const bool stale = props->mostRecentEventCount < entry.eventCount;
  const bool changed = !entry.sawProps || props->text != entry.lastPropText;
  if (changed && !stale) {
    entry.lastPropText = props->text;
    entry.sawProps = true;

    const char *currentUtf8 = RnPeerText(entry.field).UTF8String;
    const std::string current = currentUtf8 != nullptr ? currentUtf8 : "";
    if (props->text != current) {
      entry.applying = true;
      NSString *incoming = [NSString stringWithUTF8String:props->text.c_str()];
      RnPeerSetText(entry.field, incoming != nil ? incoming : @"");
      entry.applying = false;
      entry.lastReportedText = props->text;
    }
  }

  // The field renders in AppKit's own font and colour, which has nothing to do
  // with the `style` this component was given -- on a dark field that is dark
  // text on dark. buildTextAttributes is what the style becomes, and it was
  // written for the text layer with this as its eventual second caller.
  //
  // fontSizeMultiplier is 1: nothing on this platform scales text for
  // accessibility settings yet, and passing 0 would multiply the size away.
  NSDictionary<NSAttributedStringKey, id> *attributes =
      buildTextAttributes(props->getEffectiveTextAttributes(1.0F));
  NSParagraphStyle *paragraph = attributes[NSParagraphStyleAttributeName];
  RnPeerSetTextStyle(entry.field, attributes[NSFontAttributeName],
                     attributes[NSForegroundColorAttributeName],
                     paragraph != nil ? paragraph.alignment : NSTextAlignmentNatural);

  // A controlled *selection*, under the same staleness rule the text is under:
  // JavaScript that has not yet seen the last keystroke must not drag the caret
  // back to where it thought it was. Applied on change rather than on
  // difference, for the reason `text` is -- an uncontrolled field sends no
  // selection at all, and re-asserting one every render would fight the user's
  // own arrow keys.
  //
  // Only while the field has a field editor: with no focus there is nothing
  // holding a selection, and AppKit will make one the moment focus arrives.
  if (props->selection.has_value() && !stale) {
    const auto &selection = *props->selection;
    const bool selectionChanged = !entry.lastPropSelection.has_value() ||
        entry.lastPropSelection->start != selection.start ||
        entry.lastPropSelection->end != selection.end;
    if (selectionChanged) {
      entry.lastPropSelection = selection;
      // Only when something is holding a selection. A single-line field with no
      // focus has no field editor and nowhere to put one; a multiline peer
      // always can, because it is its own editor.
      if (RnPeerEditor(entry.field) != nil) {
        entry.applying = true;
        RnPeerSetSelection(entry.field,
                           NSMakeRange((NSUInteger)selection.start,
                                       (NSUInteger)MAX(0, selection.end - selection.start)));
        entry.applying = false;
        entry.lastReportedSelection = facebook::react::AttributedString::Range{
            selection.start, selection.end - selection.start};
      }
    }
  }

  if (props->placeholder.empty()) {
    RnPeerSetPlaceholder(entry.field, nil);
  } else {
    NSString *placeholder = [NSString stringWithUTF8String:props->placeholder.c_str()];
    if (placeholder == nil) {
      placeholder = @"";
    }
    // Styled like the text, so a placeholder in a 20pt field is not 13pt.
    NSMutableDictionary *placeholderAttributes = [attributes mutableCopy];
    placeholderAttributes[NSForegroundColorAttributeName] = NSColor.placeholderTextColor;
    RnPeerSetPlaceholder(entry.field,
                         [[NSAttributedString alloc] initWithString:placeholder
                                                         attributes:placeholderAttributes]);
  }

  // `editable` is the prop; `readOnly` is the newer spelling of its inverse,
  // and React Native honours both.
  RnPeerSetEditable(entry.field, props->traits.editable && !props->readOnly);

  // Zero means no limit, and so does the absurd default React Native uses when
  // the prop is absent.
  RnPeerSetMaxLength(entry.field,
                     (props->maxLength > 0 && props->maxLength < 1000000)
                         ? (NSInteger)props->maxLength
                         : 0);

  if (inserted && props->autoFocus) {
    [entry.view.window makeFirstResponder:entry.field];
  }
}

void AppKitTextInputManager::remove(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  // The delegate dispatches by tag, so dropping the entry is what stops events
  // reaching a dead view; taking the field out of the hierarchy is what stops
  // it being drawn.
  [it->second.field removeFromSuperview];
  // Both peers carry a delegate, and neither is an NSView property -- so it is
  // cleared through the class that has it.
  if (RnPeerIsMultiline(it->second.field)) {
    ((NSTextView *)it->second.field).delegate = nil;
  } else {
    ((NSTextField *)it->second.field).delegate = nil;
  }
  entries_.erase(it);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

std::shared_ptr<const TextInputEventEmitter> AppKitTextInputManager::emitterFor(Tag tag) const {
  return std::dynamic_pointer_cast<const TextInputEventEmitter>(lookup_(tag));
}

TextInputEventEmitter::Metrics AppKitTextInputManager::metricsFor(const Entry &entry) const {
  TextInputEventEmitter::Metrics metrics{};
  const char *utf8 = RnPeerText(entry.field).UTF8String;
  metrics.text = utf8 != nullptr ? utf8 : "";
  metrics.eventCount = entry.eventCount;
  metrics.target = entry.tag;

  // The whole selection, from the field editor -- an NSTextField has none of
  // its own, because the editing is done by a shared NSTextView on loan from
  // the window while the field has focus. With no editor there is no selection
  // to report, and the end of the text is where a caret would appear.
  int location = static_cast<int>(metrics.text.size());
  int length = 0;
  if (entry.field != nil) {
    if (RnPeerEditor(entry.field) != nil) {
      const NSRange selected = RnPeerSelection(entry.field);
      location = static_cast<int>(selected.location);
      length = static_cast<int>(selected.length);
    }
  }
  metrics.selectionRange = AttributedString::Range{location, length};

  // The scroll-shaped fields exist because iOS's text view is a scroll view.
  // Nothing here scrolls yet, so they describe a viewport the size of the
  // field, which is true and keeps JavaScript's arithmetic sane.
  const auto width = static_cast<facebook::react::Float>(
      entry.view != nil ? entry.view.bounds.size.width : 0);
  const auto height = static_cast<facebook::react::Float>(
      entry.view != nil ? entry.view.bounds.size.height : 0);
  metrics.containerSize = {.width = width, .height = height};
  metrics.contentSize = metrics.containerSize;
  metrics.layoutMeasurement = metrics.containerSize;
  metrics.zoomScale = 1.0F;

  return metrics;
}

void AppKitTextInputManager::handleChanged(Tag tag) {
  Entry *entry = entryFor(tag);
  if (entry == nullptr || entry->applying) {
    // This is a prop being applied, not the user typing.
    return;
  }

  const char *utf8 = RnPeerText(entry->field).UTF8String;
  const std::string value = utf8 != nullptr ? utf8 : "";
  if (value == entry->lastReportedText) {
    return;
  }
  entry->lastReportedText = value;
  entry->eventCount++;

  if (const auto emitter = emitterFor(tag)) {
    emitter->onChange(metricsFor(*entry));
  }
}

void AppKitTextInputManager::handleSubmit(Tag tag) {
  Entry *entry = entryFor(tag);
  if (entry == nullptr) {
    return;
  }
  if (const auto emitter = emitterFor(tag)) {
    emitter->onSubmitEditing(metricsFor(*entry));
  }
}

void AppKitTextInputManager::handleFocus(Tag tag) {
  Entry *entry = entryFor(tag);
  if (entry == nullptr) {
    return;
  }
  if (const auto emitter = emitterFor(tag)) {
    emitter->onFocus(metricsFor(*entry));
  }
}

bool AppKitTextInputManager::handleKeyDown(void *event) {
  if (event == nullptr) {
    return false;
  }
  NSEvent *keyEvent = (__bridge NSEvent *)event;

  // Only when the key is going to a field of ours. The monitor sees every key
  // down in the process, including those meant for anything else on screen.
  for (auto &[tag, entry] : entries_) {
    NSResponder *editor = RnPeerEditor(entry.field);
    if (editor == nil || entry.field.window.firstResponder != editor) {
      continue;
    }

    // React Native's contract: 'Enter' and 'Backspace' by name, the typed
    // character otherwise -- including ' ' for space. Keys that produce no
    // character send nothing, which is what iOS does.
    std::string key;
    switch (keyEvent.keyCode) {
      case 36:  // Return
      case 76:  // Enter, on the keypad
        key = "Enter";
        break;
      case 51:  // Delete, which is Backspace everywhere but on Apple keycaps
        key = "Backspace";
        break;
      default: {
        NSString *characters = keyEvent.characters;
        if (characters.length == 0) {
          return false;
        }
        const unichar first = [characters characterAtIndex:0];
        // Function keys and the arrows live in the Unicode private use area,
        // and control characters are not typing either.
        if (first >= 0xF700 || (first < 0x20 && first != 0x09)) {
          return false;
        }
        key = characters.UTF8String != nullptr ? characters.UTF8String : "";
        break;
      }
    }
    if (key.empty()) {
      return false;
    }

    if (auto emitter = emitterFor(tag)) {
      TextInputEventEmitter::KeyPressMetrics metrics{};
      metrics.text = key;
      metrics.eventCount = entry.eventCount;
      emitter->onKeyPress(metrics);
    }
    return true;
  }
  return false;
}

void AppKitTextInputManager::handleSelectionChanged(void *editor) {
  if (editor == nullptr) {
    return;
  }
  NSText *text = (__bridge NSText *)editor;

  for (auto &[tag, entry] : entries_) {
    if (entry.field == nil || RnPeerEditor(entry.field) != (NSResponder *)text) {
      continue;
    }
    // Not while a prop is being pushed in: applying `text` moves the caret, and
    // reporting that as the user selecting something would make a controlled
    // field fight its own render.
    if (entry.applying) {
      return;
    }

    const auto metrics = metricsFor(entry);
    if (metrics.selectionRange.location == entry.lastReportedSelection.location &&
        metrics.selectionRange.length == entry.lastReportedSelection.length) {
      return;
    }
    entry.lastReportedSelection = metrics.selectionRange;

    if (auto emitter = emitterFor(tag)) {
      emitter->onSelectionChange(metrics);
    }
    return;
  }
}

void AppKitTextInputManager::handleBlur(Tag tag) {
  Entry *entry = entryFor(tag);
  if (entry == nullptr) {
    return;
  }
  if (const auto emitter = emitterFor(tag)) {
    emitter->onBlur(metricsFor(*entry));
    emitter->onEndEditing(metricsFor(*entry));
  }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool AppKitTextInputManager::dispatchCommand(Tag tag,
                                             const std::string &name,
                                             const folly::dynamic &args) {
  Entry *entry = entryFor(tag);
  if (entry == nullptr || entry->field == nil) {
    return false;
  }

  if (name == "focus") {
    [entry->view.window makeFirstResponder:entry->field];
    return true;
  }

  if (name == "blur") {
    // Handing focus back to the window is the closest thing AppKit has to
    // "unfocus this": focus moves, it is not dropped. It ends the edit, so the
    // delegate reports a blur and JavaScript still sees onBlur.
    NSWindow *window = entry->view.window;
    if (window != nil && window.firstResponder != window) {
      [window makeFirstResponder:window];
    }
    return true;
  }

  if (name == "setTextAndSelection") {
    // [eventCount, text, start, end]. An eventCount older than what the user
    // has since typed means this command is stale and must be dropped, which is
    // the whole reason React Native counts them.
    if (args.isArray() && args.size() >= 2) {
      const int eventCount = static_cast<int>(args[0].asInt());
      if (eventCount < entry->eventCount) {
        return true;
      }
      entry->applying = true;
      const auto text = args[1].isString() ? args[1].asString() : std::string{};
      NSString *incoming = [NSString stringWithUTF8String:text.c_str()];
      RnPeerSetText(entry->field, incoming != nil ? incoming : @"");
      if (args.size() >= 4 && args[2].isInt() && args[3].isInt()) {
        const NSInteger start = static_cast<NSInteger>(args[2].asInt());
        const NSInteger end = static_cast<NSInteger>(args[3].asInt());
        if (RnPeerEditor(entry->field) != nil) {
          RnPeerSetSelection(entry->field,
                             NSMakeRange((NSUInteger)start, (NSUInteger)MAX(0, end - start)));
        }
      }
      entry->applying = false;
      entry->lastReportedText = text;
    }
    return true;
  }

  return false;
}

} // namespace basalt
