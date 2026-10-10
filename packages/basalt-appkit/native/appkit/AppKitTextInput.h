// <TextInput> on AppKit.
//
// The native half is React Native's own *iOS* TextInput: its shadow node,
// props, state and event emitter are pure C++ and measure through a
// TextLayoutManager, which on this platform is the Core Text one. Android's
// variant includes fbjni and calls into a Java FabricUIManager, so it is
// unusable here; see docs/DECISIONS.md. The component name is therefore
// "TextInput", which is what src/overrides/TextInput.js asks for -- and the
// same name the GTK side answers to.
//
// The editing itself is a real NSTextField rather than a caret drawn on a
// paragraph. That brings input methods, selection, the clipboard, and every
// key binding a Mac user expects, none of which is worth reimplementing.
//
// The awkward part of a text field is not typing, it is that React Native's
// <TextInput> is a controlled component: JavaScript owns the value, and the
// widget must not fight it. Every change is reported to JavaScript, which
// re-renders and sends the text back down as a prop -- so applying that prop
// must not itself look like the user typing, or the two chase each other.
// `applying` is what breaks that loop, exactly as on GTK.

#pragma once

#import "RnAppKitView.h"

#include <react/renderer/components/iostextinput/TextInputShadowNode.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowView.h>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace basalt {

class AppKitTextInputManager {
 public:
  using EmitterLookup = std::function<facebook::react::EventEmitter::Shared(facebook::react::Tag)>;

  explicit AppKitTextInputManager(EmitterLookup lookup);
  ~AppKitTextInputManager();

  AppKitTextInputManager(const AppKitTextInputManager &) = delete;
  AppKitTextInputManager &operator=(const AppKitTextInputManager &) = delete;

  // Called for every mutation touching a TextInput. Creates the field the first
  // time, then applies props.
  void update(RnAppKitView *view, const facebook::react::ShadowView &shadowView);

  void remove(facebook::react::Tag tag);

  // Focuses whatever `autoFocus` asked for, once the transaction is on screen.
  //
  // It cannot be done where the prop is read. Props are applied by
  // MountingWalk::create, immediately after `createView` and before any Insert,
  // so the view has no superview and `view.window` is nil -- and a
  // `makeFirstResponder:` to nil is a silent no-op. Worse, the flag that guarded
  // it is `try_emplace`'s "first time this tag was seen", which is false by the
  // time the Insert arrives, so it never ran twice either. Deferred to the end of
  // the transaction, which is where resolveRefreshControls already lives for the
  // same reason: Fabric inserts a subtree bottom-up, so even "just after
  // insertChild" is too early.
  void flushAutoFocus();

  // focus, blur, and setTextAndSelection. Returns false for anything else.
  bool dispatchCommand(facebook::react::Tag tag,
                       const std::string &name,
                       const folly::dynamic &args);

  // Called by the Objective-C delegate. Public because the trampoline has to
  // reach them; not part of anything a caller would use.
  void handleChanged(facebook::react::Tag tag);
  void handleSubmit(facebook::react::Tag tag);
  void handleFocus(facebook::react::Tag tag);
  void handleBlur(facebook::react::Tag tag);

  // The field editor whose selection moved. An NSTextField has no selection of
  // its own -- editing is done by a shared NSTextView on loan from the window
  // -- so this arrives as a notification about that view, and the tag it
  // belongs to has to be found by asking which field is currently using it.
  void handleSelectionChanged(void *editor);

  // A key on its way to whichever field has focus. Returns true when the key
  // belonged to one of ours, which is only used to decide whether to bother.
  bool handleKeyDown(void *event);

 private:
  // Set when a field is created with `autoFocus`, cleared by flushAutoFocus.
  // A list rather than one tag because nothing stops two fields asking; the last
  // one to be focused wins, which is what AppKit would do anyway.
  std::vector<facebook::react::Tag> pendingAutoFocus_;

  struct Entry {
    // The selection as JavaScript last saw it, so one movement is one event.
    facebook::react::AttributedString::Range lastReportedSelection{0, 0};

    // The last `selection` prop seen, for the same reason lastPropText exists.
    std::optional<facebook::react::Selection> lastPropSelection{};
    RnAppKitView *view{nil};
    // An NSTextField or an NSTextView; see AppKitTextPeer.h.
    NSView *field{nil};
    facebook::react::Tag tag{0};

    // React Native counts events so it can ignore a prop update that is older
    // than what the user has since typed. Every emitted metric carries it.
    int eventCount{0};

    // True while a prop is being pushed into the field, so the change it
    // provokes is not reported back as the user typing.
    bool applying{false};

    bool secure{false};
    // A multiline field is an NSTextView rather than an NSTextField, so a
    // change here rebuilds the peer exactly as `secure` does.
    bool multiline{false};

    // `clearTextOnFocus` and `selectTextOnFocus`: what happens when the field
    // takes focus. Kept here because focus arrives long after the props do, and
    // because the second has to be known in `flushAutoFocus` as well -- a field
    // that asked to select its text must not have the selection collapsed
    // behind it.
    bool clearTextOnFocus{false};
    bool selectTextOnFocus{false};
    std::string lastReportedText;

    // The last `text` prop actually seen, and whether one has been seen at all.
    //
    // This is what tells a *controlled* field from an uncontrolled one, which
    // props alone cannot: React Native's TextInput.js sends
    // `text={value ?? defaultValue}`, and an uncontrolled field with no default
    // sends undefined, which arrives here as the empty string -- exactly what a
    // controlled field that JavaScript has cleared sends. Applying it whenever
    // it differs from the field therefore wipes an uncontrolled field the
    // moment anything else in the tree re-renders, because React Native
    // re-sends `mostRecentEventCount` on every change and that alone produces
    // an Update mutation.
    //
    // So the prop is applied when it *changes*, not when it differs from the
    // field. A controlled field's value changes as the user types; an
    // uncontrolled one's never does. Found on Windows.
    std::string lastPropText;
    bool sawProps{false};
  };

  Entry *entryFor(facebook::react::Tag tag);

  // Fills in the parts of Metrics every event carries.
  facebook::react::TextInputEventEmitter::Metrics metricsFor(const Entry &entry) const;

  std::shared_ptr<const facebook::react::TextInputEventEmitter> emitterFor(
      facebook::react::Tag tag) const;

  // Builds the field, or rebuilds it when secureTextEntry changed: a secure
  // field is a different class on AppKit, not a property.
  void makeField(Entry &entry, bool secure, bool multiline);

  EmitterLookup lookup_;
  std::unordered_map<facebook::react::Tag, Entry> entries_;
  // The delegate every field points at. One for all of them: it dispatches by
  // tag, so there is nothing per-field to keep in step.
  id delegate_;
};

} // namespace basalt
