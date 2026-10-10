# Accessibility

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (5):**

1. Not tested against a real screen reader
2. Accessible actions are unimplemented: IMountingManager declares accessibleClic
3. accessibilityRole cannot change after mount on GTK; see docs/DECISIONS.md
4. accessibilityActions is ignored (~~accessibilityLabelledBy~~ and
   ~~accessibilityLiveRegion~~ are done on all three hosts)
5. Eleven more AccessibilityProps fields that no host reads, counted rather
   than guessed

- Not tested against a real screen reader. GTK's assertions say the properties
  are set; Orca on the Linux box is the check that matters.
- Accessible actions are unimplemented: `IMountingManager` declares
  `accessibleClickAction`, `setAccessibilityFocusedView`,
  `accessibleScrollInDirection` and `accessibleSetText`, and all are no-ops, so
  the interface can be read but not driven.

  **Nothing drives them either**, which is worth knowing before anyone spends a
  day on it: checked 2026-10-07, no caller for any of the four exists anywhere in
  `ReactCommon` or `ReactCxxPlatform`. The seam is declared with empty default
  bodies and the platform never invokes it, so a host that implemented all four
  would have nothing calling them and no way to show they worked beyond a unit
  test of its own making. Worth doing when something upstream reaches for them,
  or alongside the screen-reader testing above, and not before.
- `accessibilityRole` cannot change after mount **on GTK**, and that is a
  platform limitation rather than missing work: `accessible-role` is
  construct-only, so the role a widget is created with is the role it dies with.
  See `docs/DECISIONS.md`.

  **AppKit has no such limit** and already followed a change, its mounting
  manager applying the role on every props update. There is now a test pinning
  that, because nothing would have noticed a change to apply it at creation only.

  Mitigated on GTK as of 2026-10-07, as far as GTK allows. When the role a view
  asks for stops matching the role it was built with, the role *description* is
  updated instead, and that is the string a screen reader reads out, so the
  announcement follows even though the role does not. What still does not follow
  is anything inferred from the role itself, such as which navigation commands
  apply. An unchanged role leaves the description unset, because a description
  identical to the role would be noise on every visit.

  Compared against the widget rather than against remembered state, since GTK
  already knows what it was built as and a second copy of that could disagree.
- `accessibilityLiveRegion`, `accessibilityLabelledBy` and
  `accessibilityActions` are ignored. **`accessibilityValue` is done**, on both
  hosts, 2026-10-07.

  Its four parts are independent optionals in React Native, a range and a
  position in it plus a text form a screen reader prefers over the number, and
  that is the whole difficulty: an absent part has to stay absent. A view that
  never said what its range is must not be announced as sitting at the bottom of
  one, so each part that is unset is reset rather than given a zero. GTK has a
  property per part; AppKit has `accessibilityMinValue`, `accessibilityMaxValue`
  and one `accessibilityValue` carrying either the number or the text.

  **AppKit shares that one property with the checked state**, which it expresses
  as an element's value the way a checkbox does. An explicit `accessibilityValue`
  wins, the app having said it outright, and the order of application is what
  decides that. There is a test pinning it, because the ordering is the whole of
  the guarantee.

  `accessibilityActions` has its own entry above.

  **`accessibilityLiveRegion` is done on GTK and AppKit, 2026-10-08.** It has no
  property to map onto, which is what made it look harder than the rest: GTK
  announces at a moment through `gtk_accessible_announce`, AppKit posts an
  `NSAccessibilityAnnouncementRequested` notification, and neither is a state a
  view carries. So honouring the prop is change detection, and `core/LiveRegions.h`
  is where it lives, beside the label registry it is the sibling of.

  Two rules are the whole of it, and both are about not being deafening. **The
  first text is not news**: mounting a status line should not read it out, so the
  text a region is first seen with is remembered silently. Getting that wrong
  makes every screen with a status message announce itself on arrival, which is
  why the sabotage check is exactly that change -- it fails three tests per host.
  **The same text twice is not news either**, a view re-rendering for every reason
  under the sun and React Native re-sending identical props on each mutation.
  Clearing the text announces nothing, an empty announcement being silence with a
  beat of interruption in front of it.

  The text is the region's own, collected after the transaction rather than when
  the props arrive, because a `<Text>` inside it may be updated by a later
  mutation in the same batch: `rn_view_collect_text` and `-rnCollectedText` walk
  the subtree and join each paragraph with a space, and an `accessibilityLabel`
  stands in for it where the app set one, an icon-only status having no paragraph.
  Both hosts prefer the label in the same order, decided in the mounting manager
  because GTK offers no way to read an accessible property back.

  **What is observable, and what is not.** Nothing in an automated run is
  connected to AT-SPI or running VoiceOver, so both hosts record the last text
  they announced and log it. The record is what the unit tests read; the log is
  what the end-to-end scenario reads, and it asserts all three rules against a
  status line that goes from "Saving" to "Saved" a second after mount. The
  announcement is deliberately not in the tree dump: an announcement is an event
  and the dump is state.

  Seven tests in core for the rules, four per host for the wiring, one per host for
  the text collection, and the scenario. On GTK the announcement itself needs 4.14;
  below that the text is still recorded, so an older GTK reports what it would have
  said rather than looking as though the prop did nothing.

  **Windows, 2026-10-10, and the call is not the one the entry predicted.** UIA
  has no "say this" among its properties and events but one:
  `UiaRaiseNotificationEvent` carries a string, where `UIA_SystemAlertEventId`
  says only that something happened and `UIA_LiveSettingPropertyId` describes an
  element rather than announcing anything. The kind is
  `NotificationKind_ActionCompleted` and the processing hint is where the two
  politeness levels land -- `ImportantAll` against `All` -- with the view's tag
  as the activity id so two regions do not cancel each other out. The text is
  collected by the same subtree walk, and logged as well as raised for the same
  reason the other two log it.

  **`accessibilityLabelledBy` is done on GTK and AppKit, 2026-10-08**, and the
  reason it was open was wrong: the entry said nothing here can resolve a
  `nativeID` to a view, which was true, and treated that as the obstacle. The
  obstacle is *when*.

  Both hosts already store a view's `nativeID` -- drag and drop marks its targets
  that way -- but nothing could look one up, because the registry in
  `core/MountingWalk.h` is keyed by tag and the drag code walks *up* from a hit
  test. So `core/LabelRegistry.h` is the missing half, shared, and it holds both
  sides of the relation rather than resolving on sight. Fabric mounts in tree
  order, so a field labelled by the caption after it is mounted before its label
  exists: resolving as the props arrive finds nothing and stays wrong for the
  life of the screen. The registry is asked after each transaction instead, and
  answers only the relations whose resolution *changed* -- applying an unchanged
  one again would tell assistive technology that something happened when nothing
  did. A label that is unmounted empties the relation, because a reference left
  behind would have a screen reader read a view that is no longer on screen.

  GTK gets a real AT-SPI relation, `GTK_ACCESSIBLE_RELATION_LABELLED_BY`, which
  is a list of references rather than a copied string, so a caption that changes
  its text does not leave a stale copy behind. **AppKit takes one**:
  `accessibilityTitleUIElement` is a single element where the prop is a list, so
  the first is used and the dump still reports every tag that resolved, which
  keeps the two hosts' trees identical and the difference in what each platform
  does with the answer.

  **Windows, 2026-10-10**, takes one for the same reason:
  `UIA_LabeledByPropertyId` is a single element. What is particular to that host
  is *when* the element is made. Its provider copies a view's info rather than
  pointing at the view -- UIA asks from another thread and after the tree has
  moved on -- so the relation is held as views and the snapshot is taken when a
  provider is made, which is when a client asks. The answer is therefore as
  current as the question.

  Ten tests in core for the ordering, five on GTK through real transactions using
  GTK's own relation assertions, four on AppKit, and a scenario that reads the
  relation out of e2e/a11y.tsx -- where the field is deliberately written before
  its caption. Sabotaged by moving the resolution to where the props arrive,
  which is the obvious implementation: four of the five GTK tests and two of the
  four AppKit ones fail.

  One thing found on the way, in GTK rather than here:
  `gtk_accessible_list_new_from_array` is unusable, its guard reading
  `accessibles == NULL || n_accessibles == 0` so that every non-empty array is
  refused with a Gtk-CRITICAL and a NULL return. Measured on 4.22.4 and recorded
  in [upstream.md](upstream.md); `new_from_list` has the right guard.
- ~~No keyboard focus model, so nothing is reachable by Tab.~~ Done on both:
  Tab visits focusable views in tree order and wraps, Shift-Tab goes back, and
  what counts as focusable is what `accessible` marks, six tests on GTK
  (`focus_*`) and two on AppKit. A view that stops being accessible leaves the
  tab order, which is the part that had to be got right rather than added.

- **Eleven more fields that no host reads, counted 2026-10-09.** Walking
  `AccessibilityProps` field by field, which is what `scripts/scrape_props.py`
  now does for the support page: what the three hosts read is `accessible`,
  `accessibilityState`, `accessibilityLabel`, `accessibilityRole`,
  `accessibilityHint`, `accessibilityElementsHidden` and
  `importantForAccessibility`, plus `accessibilityValue`,
  `accessibilityLabelledBy` and `accessibilityLiveRegion` on GTK and AppKit, and
  `testId` and `accessibilityViewIsModal` on all three, and `accessibilityOrder`
  on GTK and AppKit. Everything else in the struct is ignored.

  **Which splits three ways rather than being one gap.** Six are iOS's own with
  no desktop equivalent -- `accessibilityTraits`, `accessibilityLargeContentTitle`,
  `accessibilityShowsLargeContentViewer`, `accessibilityIgnoresInvertColors`,
  `onAccessibilityMagicTap` and `onAccessibilityEscape`. Four are the action
  family, which is entry 2: `accessibilityActions`, `onAccessibilityAction`,
  `onAccessibilityTap` and `accessibilityRespondsToUserInteraction`.

  What is left of that list is `accessibilityLanguage`.

  **`accessibilityOrder` came off it on 2026-10-09**, and it is the one prop
  here that needed a *lookup* rather than a value: a parent lists its children
  by `nativeID`, and an id can name a view that has not mounted yet.
  `core/LabelRegistry.h` already solved that for `accessibilityLabelledBy`, so
  it now holds both relations against one `nativeID` index -- two classes would
  have meant two answers to "which view is called that", and they would
  disagree the first time a view changed its id.

  The two hosts say it differently, and that is the toolkits rather than a
  choice:

  - GTK sets `GTK_ACCESSIBLE_RELATION_FLOW_TO` on the view that asked, which is
    ARIA's `aria-flowto`: "from here, read these next". The other reading of the
    same relation is a chain between the children, `c1` flows to `c2` and `c2`
    to `c3`. One call on one view was taken instead, because it is resettable in
    one call and needs no record of which children were in the last order; a
    chain would need both.
  - AppKit replaces `accessibilityChildren`, which is what VoiceOver walks in
    place of the view hierarchy. GTK has no equivalent override, its accessible
    tree following the widget tree, which is why the two are not the same call.

  Windows is recorded rather than done, and the support page says `ignored`
  rather than `not yet`: UIA has no reading-order property at all, the tree order
  being the order, so there is nothing to set. Reordering the view tree itself
  would move what is painted.

  **`accessibilityLanguage` was written here as "one attribute on each
  platform", and that is wrong.** Checked against the three SDKs on 2026-10-09:

  - **GTK has no accessible language property.** `GtkAccessibleProperty` has
    nineteen values -- label, description, placeholder, level, sort, the three
    value ones, modal, and the rest -- and none of them is a language. The
    nearest thing is `pango_attr_language_new`, which tells the *shaper* what
    language the text is in and says nothing to AT-SPI.
  - **AppKit has two, and the one that fits needs macOS 26.**
    `NSAccessibilityLanguageAttribute` is "a BCP-47 language code for the whole
    object" and is `API_AVAILABLE(macos(26.0))`.
    `NSAccessibilityLanguageTextAttribute` has been there since 10.13 but is an
    *attributed string* key: it marks a segment of text, so it reaches a
    `<Text>` and not a view.
  - **Win32 has an exact property and wants a different vocabulary.**
    `UIA_CulturePropertyId` is an LCID rather than a BCP-47 tag, so it needs
    `LocaleNameToLCID` on the way out.

  So the prop is one call on Windows, a text attribute on macOS with a view-wide
  version arriving in 26, and nothing at all on GTK. Worth doing in that order,
  and worth saying plainly that a `<View accessibilityLanguage="fr">` cannot be
  honoured on Linux by any call that exists today.

  **`accessibilityViewIsModal` came off this list on 2026-10-09**, and it is
  the first prop here that needed a line of *JavaScript* rather than three of
  C++. All three toolkits have the flag -- `GTK_ACCESSIBLE_PROPERTY_MODAL`,
  AppKit's `accessibilityModal`, UIA's `IsDialog` -- and ReactCommon parses the
  prop with no condition on the platform, but only `BaseViewConfig.ios.js`
  declares it. A view config built from Android's therefore never sends it, so
  the prop was always false whatever the hosts did with it.

  `packages/basalt-core/src/overrides/BaseViewConfig.ts` declares it now, beside
  the three pointer events that are there for the same kind of reason. The other
  prop in that position is `borderCurve`, and backlog/correctness.md already says
  what this one proves: the view-config line is what makes such a prop arrive at
  all. It stays unimplemented there for a different reason, which that entry
  gives -- AppKit has `kCACornerCurveContinuous` and GSK has no squircle, so the
  two desktops would draw different corners from the same stylesheet.

  **`testId` came off this list on 2026-10-09**, and it is the one with a
  version in it. Each platform has an exact equivalent -- UIA's automation id,
  AppKit's `accessibilityIdentifier` -- except GTK, where the only hook is
  `GtkAccessibleIface::get_accessible_id`, a vfunc that arrived in **4.22**.
  There is no setter: a widget answers for its own id, which `RnView` now does
  by re-implementing `GtkAccessible`.

  Both of the obvious alternatives were measured and neither works:
  `gtk_widget_set_name` does not feed the accessible id, and a widget built in
  code has no buildable id to fall back on. So on GTK before 4.22 -- which
  includes Ubuntu 24.04, and therefore CI -- the prop is carried and shown in
  the tree dump and the toolkit has nowhere to put it. That is why the support
  page says `partial` for Linux and why the unit assertion is the one with the
  version guard, the end-to-end scenario asserting what every host carries.

  **And one wrong claim, found the same day.** The support page said
  `accessibilityValue` worked on Windows. It does not: nothing in the Win32 host
  reads it. The page's own check now catches that class of mistake, because every
  row names the prop its column claims.
