// What a screen reader is told.
//
// The union of tests/test_accessibility.cpp on GTK and
// tests/test_appkit_accessibility.mm on AppKit. What differs is how the answer
// is obtained: those two set a property on a widget and read it back, while
// UI Automation is a pull interface, so these build the provider and ask it the
// same questions a screen reader would -- by property id, through
// GetPropertyValue.
//
// Nothing here needs a window. UIA's tree navigation does, because a fragment
// root is an HWND, and that half lands with the host; the property half is
// answerable now and is most of what the other two suites check.

#include "TestHarness.h"

#include "RnWin32Accessible.h"
#include "RnWin32View.h"
// For narrow(): UIA answers in BSTRs and these assertions are written in UTF-8,
// like the rest of the project.
#include "Win32Strings.h"

#include <windows.h>

// WIN32_LEAN_AND_MEAN keeps combaseapi.h out of windows.h, and that is where the
// `interface` macro lives -- without it UIAutomationCore.h fails on its own
// first typedef, which reads as a broken SDK rather than a missing include.
#include <objbase.h>

#include <uiautomation.h>
#include <wrl/client.h>

#include <memory>
#include <sstream>
#include <string>

using Microsoft::WRL::ComPtr;
using basalt::win32::RnAccessibleFlag;
using basalt::win32::RnAccessibleInfo;
using basalt::win32::RnWin32View;

namespace {

ComPtr<IRawElementProviderSimple> providerFor(const RnAccessibleInfo &info) {
  ComPtr<IRawElementProviderSimple> provider;
  provider.Attach(basalt::win32::createAccessibleProvider(info));
  return provider;
}

// The control type a provider reports, or -1 if it has none.
long controlTypeOf(const ComPtr<IRawElementProviderSimple> &provider) {
  if (!provider) {
    return -1;
  }
  VARIANT value;
  VariantInit(&value);
  if (FAILED(provider->GetPropertyValue(UIA_ControlTypePropertyId, &value))) {
    return -1;
  }
  const long result = value.vt == VT_I4 ? value.lVal : -1;
  VariantClear(&value);
  return result;
}

// A string property, or "" when the provider declined to answer.
std::string stringProperty(const ComPtr<IRawElementProviderSimple> &provider, PROPERTYID id) {
  if (!provider) {
    return {};
  }
  VARIANT value;
  VariantInit(&value);
  if (FAILED(provider->GetPropertyValue(id, &value))) {
    return {};
  }
  std::string result;
  if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
    const std::wstring wide(value.bstrVal, SysStringLen(value.bstrVal));
    result = basalt::win32::narrow(wide);
  }
  VariantClear(&value);
  return result;
}

// A VARIANT's type tag, so a test can say "this property was left unset" --
// which is a different claim from "this property is false".
VARTYPE propertyType(const ComPtr<IRawElementProviderSimple> &provider, PROPERTYID id) {
  if (!provider) {
    return VT_EMPTY;
  }
  VARIANT value;
  VariantInit(&value);
  if (FAILED(provider->GetPropertyValue(id, &value))) {
    return VT_EMPTY;
  }
  const VARTYPE type = value.vt;
  VariantClear(&value);
  return type;
}

long longProperty(const ComPtr<IRawElementProviderSimple> &provider, PROPERTYID id) {
  VARIANT value;
  VariantInit(&value);
  provider->GetPropertyValue(id, &value);
  const long result = value.vt == VT_I4 ? value.lVal : -1;
  VariantClear(&value);
  return result;
}

bool boolProperty(const ComPtr<IRawElementProviderSimple> &provider, PROPERTYID id) {
  VARIANT value;
  VariantInit(&value);
  provider->GetPropertyValue(id, &value);
  const bool result = value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE;
  VariantClear(&value);
  return result;
}

RnAccessibleInfo withRole(const std::string &role) {
  RnAccessibleInfo info;
  info.role = role;
  return info;
}

} // namespace

TEST(accessibility_maps_react_native_roles_onto_uia) {
  EXPECT_EQ(controlTypeOf(providerFor(withRole("button"))),
            static_cast<long>(UIA_ButtonControlTypeId));
  EXPECT_EQ(controlTypeOf(providerFor(withRole("link"))),
            static_cast<long>(UIA_HyperlinkControlTypeId));
  EXPECT_EQ(controlTypeOf(providerFor(withRole("image"))),
            static_cast<long>(UIA_ImageControlTypeId));
  EXPECT_EQ(controlTypeOf(providerFor(withRole("text"))),
            static_cast<long>(UIA_TextControlTypeId));
  EXPECT_EQ(controlTypeOf(providerFor(withRole("checkbox"))),
            static_cast<long>(UIA_CheckBoxControlTypeId));
  EXPECT_EQ(controlTypeOf(providerFor(withRole("adjustable"))),
            static_cast<long>(UIA_SliderControlTypeId));
  EXPECT_EQ(controlTypeOf(providerFor(withRole("tablist"))),
            static_cast<long>(UIA_TabControlTypeId));
}

TEST(accessibility_falls_back_to_a_group) {
  // A role UIA has no control type for is a group, not a guess. A wrong control
  // type is worse than a vague one: it makes a view announce itself as
  // something it is not.
  EXPECT_EQ(controlTypeOf(providerFor(withRole("summary"))),
            static_cast<long>(UIA_GroupControlTypeId));
  EXPECT_EQ(controlTypeOf(providerFor(withRole("somethingnobodyhasheardof"))),
            static_cast<long>(UIA_GroupControlTypeId));

  // And a switch is a checkbox, because UIA has no switch. Named here rather
  // than left to be rediscovered, since it is a deliberate approximation.
  EXPECT_EQ(controlTypeOf(providerFor(withRole("switch"))),
            static_cast<long>(UIA_CheckBoxControlTypeId));
}

TEST(accessibility_leaves_plain_views_out_of_the_tree) {
  // A <View> with no role, no label and no hint is scaffolding. A tree full of
  // untyped groups is worse for a screen reader user than a tree without them.
  RnAccessibleInfo plain;
  EXPECT(!basalt::win32::isAccessibilityElement(plain));
  EXPECT(basalt::win32::createAccessibleProvider(plain) == nullptr);

  // A label alone is enough to make it worth reporting.
  RnAccessibleInfo labelled;
  labelled.label = "Close";
  EXPECT(basalt::win32::isAccessibilityElement(labelled));
}

TEST(accessibility_honours_a_presentational_role) {
  EXPECT(basalt::win32::roleIsPresentational("none"));
  EXPECT(basalt::win32::roleIsPresentational("presentation"));
  EXPECT(!basalt::win32::roleIsPresentational("button"));

  // Explicitly presentational beats having a label: the app said not to report
  // this one.
  RnAccessibleInfo info = withRole("none");
  info.label = "ignored";
  EXPECT(basalt::win32::createAccessibleProvider(info) == nullptr);
}

TEST(accessibility_label_and_hint_reach_uia) {
  RnAccessibleInfo info = withRole("button");
  info.label = "Save";
  info.hint = "Writes the file to disk";

  auto provider = providerFor(info);
  EXPECT(provider != nullptr);
  EXPECT_EQ(stringProperty(provider, UIA_NamePropertyId), std::string("Save"));
  // React Native's hint is "what happens if you do this", which is what UIA
  // calls help text.
  EXPECT_EQ(stringProperty(provider, UIA_HelpTextPropertyId),
            std::string("Writes the file to disk"));
}

TEST(accessibility_states_reach_uia) {
  RnAccessibleInfo info = withRole("checkbox");
  info.state.disabled = RnAccessibleFlag::True;
  info.state.checked = RnAccessibleFlag::True;
  info.state.selected = RnAccessibleFlag::True;
  info.state.expanded = RnAccessibleFlag::False;

  auto provider = providerFor(info);
  EXPECT(provider != nullptr);

  EXPECT(!boolProperty(provider, UIA_IsEnabledPropertyId));
  EXPECT_EQ(longProperty(provider, UIA_ToggleToggleStatePropertyId),
            static_cast<long>(ToggleState_On));
  EXPECT(boolProperty(provider, UIA_SelectionItemIsSelectedPropertyId));
  EXPECT_EQ(longProperty(provider, UIA_ExpandCollapseExpandCollapseStatePropertyId),
            static_cast<long>(ExpandCollapseState_Collapsed));
}

TEST(accessibility_unset_leaves_uia_alone) {
  // The tri-state, stated as a test. A view that never mentions being checked
  // is not an unchecked checkbox, and must not report ToggleState_Off -- which
  // a screen reader would read aloud as "not checked".
  auto provider = providerFor(withRole("button"));
  EXPECT(provider != nullptr);

  EXPECT_EQ(propertyType(provider, UIA_ToggleToggleStatePropertyId), VT_EMPTY);
  EXPECT_EQ(propertyType(provider, UIA_SelectionItemIsSelectedPropertyId), VT_EMPTY);
  EXPECT_EQ(propertyType(provider, UIA_ExpandCollapseExpandCollapseStatePropertyId), VT_EMPTY);

  // IsEnabled is the exception, and deliberately: UIA has no "unknown" for it,
  // and its default is enabled, which is React Native's default too.
  EXPECT_EQ(propertyType(provider, UIA_IsEnabledPropertyId), VT_BOOL);
  EXPECT(boolProperty(provider, UIA_IsEnabledPropertyId));
}

TEST(accessibility_hidden_takes_a_view_out_of_the_tree) {
  RnAccessibleInfo info = withRole("button");
  info.label = "Save";
  info.hidden = true;

  EXPECT(!basalt::win32::isAccessibilityElement(info));
  EXPECT(basalt::win32::createAccessibleProvider(info) == nullptr);
}

TEST(accessibility_role_is_reported_in_the_tree) {
  auto view = std::make_unique<RnWin32View>(3);
  view->setFrame(0, 0, 100, 40);

  RnAccessibleInfo info = withRole("button");
  info.label = "Save";
  view->setAccessibleInfo(info);

  // React Native's role name, not UIA's -- the same string GTK and AppKit
  // print, so the three dumps compare.
  EXPECT_EQ(view->describeTree(), std::string("view tag=3 frame=(0,0 100x40) role=button\n"));
}

TEST(accessibility_role_can_change_after_mount) {
  // GTK cannot do this: a GtkAccessible's role is construct-only, so
  // `docs/DECISIONS.md` records that accessibilityRole is fixed once a widget
  // exists. UI Automation pulls rather than being pushed to, so there is
  // nothing baked in, and the mounting manager should not copy GTK's
  // restriction when it arrives.
  auto view = std::make_unique<RnWin32View>(4);
  view->setAccessibleInfo(withRole("button"));

  ComPtr<IRawElementProviderSimple> asButton;
  asButton.Attach(view->createAccessibleProvider());
  EXPECT_EQ(controlTypeOf(asButton), static_cast<long>(UIA_ButtonControlTypeId));

  view->setAccessibleInfo(withRole("checkbox"));

  ComPtr<IRawElementProviderSimple> asCheckbox;
  asCheckbox.Attach(view->createAccessibleProvider());
  EXPECT_EQ(controlTypeOf(asCheckbox), static_cast<long>(UIA_CheckBoxControlTypeId));
}
