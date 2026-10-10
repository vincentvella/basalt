#include "RnWin32Accessible.h"

#include "Win32Strings.h"

#include <windows.h>

// WIN32_LEAN_AND_MEAN keeps combaseapi.h out of windows.h, and that is where the
// `interface` macro lives -- without it UIAutomationCore.h fails on its own
// first typedef, which reads as a broken SDK rather than a missing include.
#include <objbase.h>

#include <uiautomation.h>

#include <optional>

#include <atomic>
#include <unordered_map>

namespace basalt::win32 {
namespace {

// React Native's role vocabulary, mapped onto UIA control types. The names are
// exactly the ones `GtkMountingManager.cpp` accepts, in the same order, because
// a role this platform silently ignores and GTK honours is a difference no test
// comparing two trees would catch -- both print React Native's own string.
//
// Three of them are approximations UIA forces, and they are worth naming rather
// than leaving to be rediscovered. UIA has no switch, so a switch is a checkbox
// with a toggle state, which is what Narrator reads sensibly. It has no heading
// control type either -- headings are a *property*, UIA_HeadingLevelPropertyId,
// on a text element -- so "header" is text. And "alert" is not a control type
// at all in UIA; it is an event, so the nearest honest resting place is a group
// until there is a live-region implementation to raise it from.
const std::unordered_map<std::string, long> &roleTable() {
  static const std::unordered_map<std::string, long> kRoles = {
      {"button", UIA_ButtonControlTypeId},
      {"togglebutton", UIA_ButtonControlTypeId},
      {"link", UIA_HyperlinkControlTypeId},
      {"search", UIA_EditControlTypeId},
      {"image", UIA_ImageControlTypeId},
      {"imagebutton", UIA_ButtonControlTypeId},
      {"text", UIA_TextControlTypeId},
      {"header", UIA_TextControlTypeId},
      {"adjustable", UIA_SliderControlTypeId},
      {"alert", UIA_GroupControlTypeId},
      {"checkbox", UIA_CheckBoxControlTypeId},
      {"combobox", UIA_ComboBoxControlTypeId},
      {"menu", UIA_MenuControlTypeId},
      {"menubar", UIA_MenuBarControlTypeId},
      {"menuitem", UIA_MenuItemControlTypeId},
      {"progressbar", UIA_ProgressBarControlTypeId},
      {"radio", UIA_RadioButtonControlTypeId},
      {"radiogroup", UIA_GroupControlTypeId},
      {"scrollbar", UIA_ScrollBarControlTypeId},
      {"spinbutton", UIA_SpinnerControlTypeId},
      {"switch", UIA_CheckBoxControlTypeId},
      {"tab", UIA_TabItemControlTypeId},
      {"tablist", UIA_TabControlTypeId},
      {"list", UIA_ListControlTypeId},
      {"grid", UIA_DataGridControlTypeId},
      {"toolbar", UIA_ToolBarControlTypeId},
      {"tooltip", UIA_ToolTipControlTypeId},
  };
  return kRoles;
}

BSTR toBstr(const std::string &text) {
  const std::wstring wide = widen(text);
  return SysAllocStringLen(wide.c_str(), static_cast<UINT>(wide.size()));
}

// The property half of a UIA provider.
//
// ProviderOptions_ServerSideProvider because this lives in the process that
// owns the UI, which is the only arrangement that makes sense for a host that
// draws its own views.
class AccessibleProvider final : public IRawElementProviderSimple {
 public:
  explicit AccessibleProvider(const RnAccessibleInfo &info) : info_(info) {}

  // --- IUnknown ---

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --references_;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
    if (object == nullptr) {
      return E_INVALIDARG;
    }
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) {
      *object = static_cast<IRawElementProviderSimple *>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  // --- IRawElementProviderSimple ---

  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *options) override {
    if (options == nullptr) {
      return E_INVALIDARG;
    }
    *options = ProviderOptions_ServerSideProvider;
    return S_OK;
  }

  // The control patterns -- Toggle, SelectionItem, ExpandCollapse, Invoke --
  // land with the host, because a pattern that reports a state but cannot be
  // driven is worse than none: it tells a screen reader the control can be
  // operated and then does nothing. The *states* are answered as properties
  // below, which is read-only and honest.
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown **provider) override {
    if (provider == nullptr) {
      return E_INVALIDARG;
    }
    *provider = nullptr;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT *value) override {
    if (value == nullptr) {
      return E_INVALIDARG;
    }
    VariantInit(value);

    switch (property) {
      case UIA_ControlTypePropertyId:
        value->vt = VT_I4;
        value->lVal = controlTypeForRole(info_.role);
        return S_OK;

      case UIA_NamePropertyId:
        if (!info_.label.empty()) {
          value->vt = VT_BSTR;
          value->bstrVal = toBstr(info_.label);
        }
        return S_OK;

      case UIA_AutomationIdPropertyId:
        // React Native's `testID`. UIA's automation id is the property every
        // Windows test runner looks a control up by, and it is the same idea as
        // AppKit's `accessibilityIdentifier` and GTK's accessible id.
        if (!info_.testId.empty()) {
          value->vt = VT_BSTR;
          value->bstrVal = toBstr(info_.testId);
        }
        return S_OK;

      case UIA_IsDialogPropertyId:
        // React Native's `accessibilityViewIsModal`. Left unset rather than
        // VARIANT_FALSE when the app did not ask: "this is not a dialog" is a
        // claim, and the other two hosts say nothing in that case either.
        if (info_.modal) {
          value->vt = VT_BOOL;
          value->boolVal = VARIANT_TRUE;
        }
        return S_OK;

      case UIA_HelpTextPropertyId:
        // React Native's `accessibilityHint` is "what happens if you do this",
        // which is what UIA calls help text. AppKit puts it in the
        // accessibility description and GTK in the description property; all
        // three are the same idea under three names.
        if (!info_.hint.empty()) {
          value->vt = VT_BSTR;
          value->bstrVal = toBstr(info_.hint);
        }
        return S_OK;

      case UIA_IsEnabledPropertyId:
        // Unset means enabled, which is UIA's default and React Native's.
        value->vt = VT_BOOL;
        value->boolVal =
            info_.state.disabled == RnAccessibleFlag::True ? VARIANT_FALSE : VARIANT_TRUE;
        return S_OK;

      case UIA_ToggleToggleStatePropertyId:
        // The property mirror of the Toggle pattern, so the state is reportable
        // before the pattern exists. Left empty when unset, because
        // ToggleState_Off is a claim that this is an unchecked checkbox.
        if (info_.state.checked != RnAccessibleFlag::Unset) {
          value->vt = VT_I4;
          value->lVal = info_.state.checked == RnAccessibleFlag::True ? ToggleState_On
                                                                     : ToggleState_Off;
        }
        return S_OK;

      case UIA_SelectionItemIsSelectedPropertyId:
        if (info_.state.selected != RnAccessibleFlag::Unset) {
          value->vt = VT_BOOL;
          value->boolVal =
              info_.state.selected == RnAccessibleFlag::True ? VARIANT_TRUE : VARIANT_FALSE;
        }
        return S_OK;

      case UIA_ExpandCollapseExpandCollapseStatePropertyId:
        if (info_.state.expanded != RnAccessibleFlag::Unset) {
          value->vt = VT_I4;
          value->lVal = info_.state.expanded == RnAccessibleFlag::True
                            ? ExpandCollapseState_Expanded
                            : ExpandCollapseState_Collapsed;
        }
        return S_OK;

      case UIA_RangeValueMinimumPropertyId:
      case UIA_RangeValueMaximumPropertyId:
      case UIA_RangeValueValuePropertyId: {
        // `accessibilityValue`'s range and position, as the property mirrors of
        // UIA's RangeValue pattern -- so they are answerable before the pattern
        // exists, the same arrangement the toggle and selection states use.
        //
        // Each part is left unset when the app did not give it: VT_EMPTY is
        // "no opinion", where VT_R8 zero is the claim that a slider sits at the
        // bottom of a range nobody mentioned. GTK resets its property and
        // AppKit leaves its attribute nil for the same reason.
        const std::optional<int> &part = property == UIA_RangeValueMinimumPropertyId
            ? info_.value.min
            : (property == UIA_RangeValueMaximumPropertyId ? info_.value.max : info_.value.now);
        if (part.has_value()) {
          value->vt = VT_R8;
          value->dblVal = static_cast<double>(*part);
        }
        return S_OK;
      }

      case UIA_RangeValueIsReadOnlyPropertyId:
        // Read-only whenever there is a range at all: nothing here lets a client
        // change the value. What would is `IRangeValueProvider::SetValue`, which
        // is a pattern rather than a property and lands with the fragment root,
        // and React Native expresses "you may change this" as an accessibility
        // *action*, which no host implements yet.
        if (info_.value.min.has_value() || info_.value.max.has_value()
            || info_.value.now.has_value()) {
          value->vt = VT_BOOL;
          value->boolVal = VARIANT_TRUE;
        }
        return S_OK;

      case UIA_ValueValuePropertyId:
        // The text form, which a screen reader reads in place of the number:
        // "half past three" rather than 1530. UIA keeps it in the Value pattern
        // rather than RangeValue, so it is its own property and independent of
        // the range, exactly as React Native has it.
        if (info_.value.text.has_value() && !info_.value.text->empty()) {
          value->vt = VT_BSTR;
          value->bstrVal = toBstr(*info_.value.text);
        }
        return S_OK;

      case UIA_LabeledByPropertyId:
        // `accessibilityLabelledBy`: a relation rather than a copied string, so
        // a caption that changes its text does not leave a stale name behind.
        // The element is a provider over the labelling view's own info, which
        // is what a client follows to read it.
        if (info_.labelledBy != nullptr) {
          if (IRawElementProviderSimple *label = createAccessibleProvider(*info_.labelledBy)) {
            value->vt = VT_UNKNOWN;
            // The VARIANT owns the reference: `VariantClear` releases it, and
            // every UIA client clears what it is handed.
            value->punkVal = label;
          }
        }
        return S_OK;

      case UIA_IsOffscreenPropertyId:
        value->vt = VT_BOOL;
        value->boolVal = info_.hidden ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;

      case UIA_IsControlElementPropertyId:
      case UIA_IsContentElementPropertyId:
        value->vt = VT_BOOL;
        value->boolVal = VARIANT_TRUE;
        return S_OK;

      default:
        break;
    }

    // VT_EMPTY, which is UIA's "I have no opinion, use the default". Returning
    // an error here instead makes a screen reader treat the whole element as
    // broken rather than as quiet.
    return S_OK;
  }

  // Null because these views are not native Windows controls: there is no
  // HWND-based provider underneath to defer to. The host's fragment root is
  // where the one real HWND enters the picture.
  HRESULT STDMETHODCALLTYPE
  get_HostRawElementProvider(IRawElementProviderSimple **provider) override {
    if (provider == nullptr) {
      return E_INVALIDARG;
    }
    *provider = nullptr;
    return S_OK;
  }

 private:
  ~AccessibleProvider() = default;

  RnAccessibleInfo info_;
  std::atomic<ULONG> references_{1};
};

} // namespace

long controlTypeForRole(const std::string &role) {
  const auto &table = roleTable();
  const auto it = table.find(role);
  return it == table.end() ? UIA_GroupControlTypeId : it->second;
}

bool roleIsPresentational(const std::string &role) {
  return role == "none" || role == "presentation";
}

bool isAccessibilityElement(const RnAccessibleInfo &info) {
  if (info.hidden || roleIsPresentational(info.role)) {
    return false;
  }
  // A value counts, beside a role, a label and a hint: a view that says what
  // its position in a range is means to be reported, and dropping it because it
  // named no role would be the quiet kind of gap.
  return !info.role.empty() || !info.label.empty() || !info.hint.empty() || !info.value.empty();
}

IRawElementProviderSimple *createAccessibleProvider(const RnAccessibleInfo &info) {
  if (!isAccessibilityElement(info)) {
    return nullptr;
  }
  return new AccessibleProvider(info);
}

} // namespace basalt::win32
