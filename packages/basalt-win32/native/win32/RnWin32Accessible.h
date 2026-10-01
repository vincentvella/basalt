// What a screen reader is told about a view.
//
// This is the one place Windows is structurally different from the other two
// desktops rather than differently spelled. GTK and AppKit both take
// accessibility as *properties on a view*: `gtk_accessible_update_property`,
// `setAccessibilityRole:`. UI Automation inverts that -- it asks a *provider*
// object questions, and the provider answers by property id. So where the other
// two mounting managers push, this one has to be ready to be pulled.
//
// The consequence worth knowing: a GTK role is construct-only, which is why
// `docs/DECISIONS.md` records that `accessibilityRole` cannot change after
// mount there. Nothing here has that constraint, because nothing is baked into
// a widget class -- the provider reads the current value each time it is asked.
// This platform can therefore do something GTK cannot, and the mounting manager
// should not copy GTK's restriction when it arrives.
//
// What is here is the property half, which is all of it that can exist before
// there is an HWND: UIA's tree navigation needs a fragment root, and a fragment
// root is a window. `IRawElementProviderFragment` and the `Toggle`,
// `SelectionItem` and `ExpandCollapse` *patterns* therefore land with the host.
// The states those patterns expose are answerable now, because UIA mirrors each
// of them as a plain property id, and that is what these answer.

#pragma once

#include <string>

struct IRawElementProviderSimple;

namespace basalt::win32 {

// Each state is a tri-state on purpose. Leaving `checked` unset is not the same
// as setting it false: a view that never mentions being checked is not an
// unchecked checkbox, and a screen reader should not read it as one. Both other
// platforms make the same distinction.
enum class RnAccessibleFlag {
  Unset,
  False,
  True,
};

struct RnAccessibleState {
  RnAccessibleFlag disabled = RnAccessibleFlag::Unset;
  RnAccessibleFlag checked = RnAccessibleFlag::Unset;
  RnAccessibleFlag selected = RnAccessibleFlag::Unset;
  RnAccessibleFlag expanded = RnAccessibleFlag::Unset;
  RnAccessibleFlag busy = RnAccessibleFlag::Unset;
};

// Everything a view carries for assistive technology. Held by the view as plain
// data; the provider below reads it.
struct RnAccessibleInfo {
  // React Native's own role string -- "button", "image", "text" -- not a UIA
  // control type. The mapping is a platform decision and belongs beside the
  // platform, and the string is what `describeTree` reports, so the three
  // hosts' trees compare without one of them speaking UIA's vocabulary and
  // another speaking GTK's.
  std::string role;
  std::string label;
  std::string hint;
  RnAccessibleState state;
  // accessible={false} and accessibilityElementsHidden.
  bool hidden = false;
};

// The UIA control type for a React Native role name, or
// UIA_GroupControlTypeId for one this platform does not recognise.
//
// A fallback rather than a guess: a wrong control type is worse than a vague
// one, because it makes a view announce itself as something it is not. Both
// other platforms fall back the same way.
long controlTypeForRole(const std::string &role);

// True for "none" and "presentation", which mean "do not report this at all".
bool roleIsPresentational(const std::string &role);

// True when this view should appear to a screen reader. A plain <View> with no
// role, no label and no hint is scaffolding, and a tree full of untyped groups
// is worse for a screen reader user than a tree without them.
bool isAccessibilityElement(const RnAccessibleInfo &info);

// A UI Automation provider over one view's RnAccessibleInfo, for the property
// half of the interface. Returns null when the view is not an accessibility
// element. The caller owns a reference and must Release it.
//
// The provider copies the info rather than pointing at the view: UIA can ask
// its questions from another thread and long after the view is gone, and a
// dangling read there is a crash inside somebody's screen reader.
IRawElementProviderSimple *createAccessibleProvider(const RnAccessibleInfo &info);

} // namespace basalt::win32
