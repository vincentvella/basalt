# Accessibility

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (4):**

1. Not tested against a real screen reader
2. Accessible actions are unimplemented: IMountingManager declares accessibleClic
3. accessibilityRole cannot change after mount on GTK; see docs/DECISIONS.md
4. accessibilityActions is ignored (~~accessibilityLabelledBy~~ and
   ~~accessibilityLiveRegion~~ are done on GTK and AppKit)

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
