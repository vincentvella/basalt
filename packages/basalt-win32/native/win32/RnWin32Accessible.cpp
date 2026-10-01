#include "RnWin32Accessible.h"

#include "Win32Strings.h"

#include <windows.h>

// WIN32_LEAN_AND_MEAN keeps combaseapi.h out of windows.h, and that is where the
// `interface` macro lives -- without it UIAutomationCore.h fails on its own
// first typedef, which reads as a broken SDK rather than a missing include.
#include <objbase.h>

#include <uiautomation.h>

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
  return !info.role.empty() || !info.label.empty() || !info.hint.empty();
}

IRawElementProviderSimple *createAccessibleProvider(const RnAccessibleInfo &info) {
  if (!isAccessibilityElement(info)) {
    return nullptr;
  }
  return new AccessibleProvider(info);
}

} // namespace basalt::win32
