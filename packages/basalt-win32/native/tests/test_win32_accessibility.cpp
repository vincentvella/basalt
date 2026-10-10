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
#include "RnWin32TextLayout.h"
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
using basalt::win32::RnTextStyle;
using basalt::win32::RnWin32TextLayout;
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

// `testID`, which UIA calls the automation id: the property every Windows test
// runner looks a control up by, and the same idea as AppKit's
// `accessibilityIdentifier` and GTK's accessible id.
TEST(accessibility_a_test_id_becomes_the_automation_id) {
  RnAccessibleInfo info = withRole("button");
  info.label = "Save";
  info.testId = "save-button";

  auto provider = providerFor(info);
  EXPECT(provider != nullptr);
  EXPECT_EQ(stringProperty(provider, UIA_AutomationIdPropertyId), std::string("save-button"));
  // Not the name: a runner searching by automation id and a screen reader
  // reading the label are different questions, and `testID` answers only one.
  EXPECT_EQ(stringProperty(provider, UIA_NamePropertyId), std::string("Save"));

  auto view = std::make_unique<RnWin32View>(4);
  view->setFrame(0, 0, 100, 40);
  view->setAccessibleInfo(info);
  EXPECT_EQ(view->describeTree(),
            std::string("view tag=4 frame=(0,0 100x40) role=button testid=save-button\n"));
}

// And a view with no testID leaves the property unset rather than empty: a
// runner searching for "" would otherwise match the first control it saw.
TEST(accessibility_no_test_id_leaves_the_automation_id_unset) {
  auto provider = providerFor(withRole("button"));
  EXPECT(provider != nullptr);
  EXPECT_EQ(propertyType(provider, UIA_AutomationIdPropertyId), VT_EMPTY);
}

// `accessibilityViewIsModal`, which UIA calls a dialog: a client that honours
// `IsDialog` stops offering what is behind the element.
TEST(accessibility_a_modal_view_is_a_dialog) {
  RnAccessibleInfo info = withRole("button");
  info.label = "Save";
  info.modal = true;

  auto provider = providerFor(info);
  EXPECT(provider != nullptr);
  EXPECT_EQ(propertyType(provider, UIA_IsDialogPropertyId), VT_BOOL);
  EXPECT(boolProperty(provider, UIA_IsDialogPropertyId));

  auto view = std::make_unique<RnWin32View>(5);
  view->setFrame(0, 0, 100, 40);
  view->setAccessibleInfo(info);
  EXPECT_EQ(view->describeTree(),
            std::string("view tag=5 frame=(0,0 100x40) role=button modal\n"));
}

// Unset rather than false: "this is not a dialog" is a claim, and the other two
// hosts stay quiet about it too.
TEST(accessibility_a_plain_view_claims_no_dialog) {
  auto provider = providerFor(withRole("button"));
  EXPECT(provider != nullptr);
  EXPECT_EQ(propertyType(provider, UIA_IsDialogPropertyId), VT_EMPTY);
}

// ---------------------------------------------------------------------------
// `accessibilityLabelledBy`, which is the one accessibility prop that is a
// relation between two views rather than a value on one.
//
// A relation rather than a copied string: the field's name lives on the caption
// beside it, so a caption that changes its text does not leave a stale copy
// behind. GTK sets an AT-SPI LABELLED_BY relation, macOS an
// `accessibilityTitleUIElement`, and UIA answers `UIA_LabeledByPropertyId` with
// an element -- which is why these go through a view rather than through a bare
// `RnAccessibleInfo`: the view is what knows the other view.
// ---------------------------------------------------------------------------

TEST(accessibility_labelled_by_answers_with_the_labelling_element) {
  auto field = std::make_unique<RnWin32View>(1);
  auto caption = std::make_unique<RnWin32View>(2);

  // A field with no label of its own, which is the whole point of the relation:
  // its name is the caption's. `role` has to be something, though -- "none" is
  // presentational and has no provider at all, which is the right behaviour and
  // what the first version of this test tripped over.
  RnAccessibleInfo fieldInfo;
  fieldInfo.role = "textbox";
  fieldInfo.testId = "field";
  field->setAccessibleInfo(fieldInfo);

  RnAccessibleInfo captionInfo;
  captionInfo.role = "text";
  captionInfo.label = "Save the document";
  caption->setAccessibleInfo(captionInfo);

  // Nothing yet: a view with no relation answers with no element rather than
  // with itself.
  ComPtr<IRawElementProviderSimple> before;
  before.Attach(field->createAccessibleProvider());
  EXPECT(before != nullptr);
  if (before) {
    EXPECT_EQ(propertyType(before, UIA_LabeledByPropertyId), VT_EMPTY);
  }

  field->setLabelledBy({caption.get()});

  ComPtr<IRawElementProviderSimple> provider;
  provider.Attach(field->createAccessibleProvider());
  EXPECT(provider != nullptr);
  if (!provider) {
    return;
  }

  VARIANT value;
  VariantInit(&value);
  EXPECT(SUCCEEDED(provider->GetPropertyValue(UIA_LabeledByPropertyId, &value)));
  EXPECT_EQ(value.vt, VT_UNKNOWN);
  if (value.vt == VT_UNKNOWN && value.punkVal != nullptr) {
    // The element is a provider, and what it is for is reading the caption's
    // name: a relation pointing at the wrong view is the failure that matters.
    ComPtr<IRawElementProviderSimple> label;
    EXPECT(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(label.GetAddressOf()))));
    EXPECT_EQ(stringProperty(label, UIA_NamePropertyId), std::string("Save the document"));
  }
  VariantClear(&value);
}

// The relation comes apart again, which is what a caption being unmounted means
// and what a stale element would get wrong.
TEST(accessibility_labelled_by_can_be_taken_away) {
  auto field = std::make_unique<RnWin32View>(1);
  auto caption = std::make_unique<RnWin32View>(2);
  RnAccessibleInfo info;
  info.label = "Name";
  field->setAccessibleInfo(info);
  caption->setAccessibleInfo(info);

  field->setLabelledBy({caption.get()});
  field->setLabelledBy({});

  ComPtr<IRawElementProviderSimple> provider;
  provider.Attach(field->createAccessibleProvider());
  EXPECT(provider != nullptr);
  if (provider) {
    EXPECT_EQ(propertyType(provider, UIA_LabeledByPropertyId), VT_EMPTY);
  }
}

// The resolved relation in the dump, by tag, which is what the end-to-end
// scenario reads on all three hosts.
TEST(accessibility_labelled_by_is_reported_in_the_tree) {
  auto field = std::make_unique<RnWin32View>(7);
  auto caption = std::make_unique<RnWin32View>(9);
  EXPECT(field->describeTree().find("labelled-by=") == std::string::npos);

  field->setLabelledBy({caption.get()});
  EXPECT(field->describeTree().find("labelled-by=9") != std::string::npos);
}

// ---------------------------------------------------------------------------
// `accessibilityLiveRegion`: the text a status message says, and saying it.
//
// The change detection is `core/LiveRegions.h`'s and has its own tests there;
// what is this host's is collecting the text and raising the announcement. UIA
// has no "say this" except `UiaRaiseNotificationEvent`, and nothing in a test
// run is listening to it, so what is asserted here is the text -- which is the
// half that was wrong on both other hosts first.
// ---------------------------------------------------------------------------

TEST(accessibility_a_live_region_collects_the_text_of_its_subtree) {
  auto region = std::make_unique<RnWin32View>(1);
  auto first = std::make_unique<RnWin32View>(2);
  auto second = std::make_unique<RnWin32View>(3);
  RnTextStyle style;
  first->setTextLayout(RnWin32TextLayout::create("Saving", style, 0));
  second->setTextLayout(RnWin32TextLayout::create("one of three", style, 0));
  region->insertChild(first.get(), 0);
  region->insertChild(second.get(), 1);

  // In paint order, joined with a space: a status line is a <View> with text
  // inside it, and the view itself carries no string at all. Both other hosts
  // collect it the same way.
  EXPECT_EQ(region->collectText(), std::string("Saving one of three"));

  // A view with nothing in it says nothing, rather than a stray space.
  auto empty = std::make_unique<RnWin32View>(4);
  EXPECT(empty->collectText().empty());

  region->removeChild(first.get());
  region->removeChild(second.get());
}

TEST(accessibility_an_announcement_is_remembered_and_an_empty_one_is_not_made) {
  auto view = std::make_unique<RnWin32View>(1);
  RnAccessibleInfo info;
  info.role = "text";
  info.label = "Saved";
  view->setAccessibleInfo(info);

  EXPECT(view->lastAnnouncement().empty());

  view->announce("Saved", false);
  EXPECT_EQ(view->lastAnnouncement(), std::string("Saved"));

  // Nothing to say is not an announcement: a region whose text went empty must
  // not read out silence, and `UiaRaiseNotificationEvent` with an empty string
  // is a notification a screen reader still reports as one.
  view->announce("", true);
  EXPECT_EQ(view->lastAnnouncement(), std::string("Saved"));

  view->announce("Saved twice", true);
  EXPECT_EQ(view->lastAnnouncement(), std::string("Saved twice"));
}

// ---------------------------------------------------------------------------
// `accessibilityValue`: a range, a position in it, and a text form.
//
// Four independent optionals, and the whole difficulty is that an absent part
// has to stay absent: a view that never said what its range is must not be
// announced as sitting at the bottom of one. GTK resets a property per part and
// AppKit leaves its attribute nil; UIA has the RangeValue properties and a
// separate Value one for the text, so each is answered or left VT_EMPTY.
// ---------------------------------------------------------------------------

TEST(accessibility_value_answers_the_range_and_the_position) {
  RnAccessibleInfo info;
  info.role = "slider";
  info.value.min = 0;
  info.value.max = 10;
  info.value.now = 7;

  const auto provider = providerFor(info);
  EXPECT(provider != nullptr);
  if (!provider) {
    return;
  }

  const auto number = [&provider](PROPERTYID id) {
    VARIANT value;
    VariantInit(&value);
    if (FAILED(provider->GetPropertyValue(id, &value))) {
      return -1.0;
    }
    const double result = value.vt == VT_R8 ? value.dblVal : -1.0;
    VariantClear(&value);
    return result;
  };

  EXPECT_NEAR(number(UIA_RangeValueMinimumPropertyId), 0.0, 0.001);
  EXPECT_NEAR(number(UIA_RangeValueMaximumPropertyId), 10.0, 0.001);
  EXPECT_NEAR(number(UIA_RangeValueValuePropertyId), 7.0, 0.001);
  // Read-only, because nothing here lets a client change it: the pattern that
  // would is `IRangeValueProvider`, and React Native expresses "you may change
  // this" as an accessibility action.
  EXPECT_EQ(propertyType(provider, UIA_RangeValueIsReadOnlyPropertyId), VT_BOOL);
  // And no text form was given, so none is claimed.
  EXPECT_EQ(propertyType(provider, UIA_ValueValuePropertyId), VT_EMPTY);
}

TEST(accessibility_value_leaves_a_part_nobody_mentioned_unset) {
  RnAccessibleInfo info;
  info.role = "progressbar";
  // Only the position, which is what a progress bar with no declared range
  // looks like. The other two parts must stay absent rather than become zeros.
  info.value.now = 3;

  const auto provider = providerFor(info);
  EXPECT(provider != nullptr);
  if (!provider) {
    return;
  }
  EXPECT_EQ(propertyType(provider, UIA_RangeValueValuePropertyId), VT_R8);
  EXPECT_EQ(propertyType(provider, UIA_RangeValueMinimumPropertyId), VT_EMPTY);
  EXPECT_EQ(propertyType(provider, UIA_RangeValueMaximumPropertyId), VT_EMPTY);

  // A view with no value at all says nothing about any of them, and does not
  // claim to be read-only either.
  RnAccessibleInfo plain;
  plain.role = "button";
  const auto bare = providerFor(plain);
  EXPECT(bare != nullptr);
  if (bare) {
    EXPECT_EQ(propertyType(bare, UIA_RangeValueValuePropertyId), VT_EMPTY);
    EXPECT_EQ(propertyType(bare, UIA_RangeValueIsReadOnlyPropertyId), VT_EMPTY);
  }
}

TEST(accessibility_value_text_is_its_own_property) {
  RnAccessibleInfo info;
  info.role = "adjustable";
  info.value.now = 1530;
  info.value.text = "half past three";

  const auto provider = providerFor(info);
  EXPECT(provider != nullptr);
  if (!provider) {
    return;
  }
  // The text a screen reader reads instead of the number, which UIA keeps in
  // the Value pattern rather than RangeValue -- so both are answered and
  // neither replaces the other.
  EXPECT_EQ(stringProperty(provider, UIA_ValueValuePropertyId),
            std::string("half past three"));
  EXPECT_EQ(propertyType(provider, UIA_RangeValueValuePropertyId), VT_R8);
}

// A value on its own makes a view worth reporting, which is the one change this
// made to what counts as an accessibility element: a <View> that says where it
// is in a range means to be heard, even if it named no role.
TEST(accessibility_a_value_alone_is_enough_to_be_an_element) {
  RnAccessibleInfo info;
  info.value.now = 2;
  info.value.max = 5;
  EXPECT(basalt::win32::isAccessibilityElement(info));

  RnAccessibleInfo scaffolding;
  EXPECT(!basalt::win32::isAccessibilityElement(scaffolding));
}
