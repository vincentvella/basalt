# Accessibility

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (4):**

1. Not tested against a real screen reader
2. Accessible actions are unimplemented: IMountingManager declares accessibleClic
3. accessibilityRole cannot change after mount; see docs/DECISIONS.md
4. accessibilityLiveRegion, accessibilityLabelledBy, accessibilityValue and acces

- Not tested against a real screen reader. GTK's assertions say the properties
  are set; Orca on the Linux box is the check that matters.
- Accessible actions are unimplemented: `IMountingManager` declares
  `accessibleClickAction`, `setAccessibilityFocusedView`,
  `accessibleScrollInDirection` and `accessibleSetText`, and all are no-ops, so
  the interface can be read but not driven.
- `accessibilityRole` cannot change after mount; see `docs/DECISIONS.md`.
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

  The two still open are open for different reasons, neither of them effort.
  `accessibilityLiveRegion` has no property to map onto: GTK models it as
  `gtk_accessible_announce` at a moment rather than as a state of a view, so
  honouring it means noticing that a view's content changed and announcing the
  new text, which is change detection rather than prop plumbing.
  `accessibilityLabelledBy` names another view by its `nativeID`, and nothing
  here can resolve a `nativeID` to a view: the registry is keyed by tag.
  `accessibilityActions` has its own entry above.
- ~~No keyboard focus model, so nothing is reachable by Tab.~~ Done on both:
  Tab visits focusable views in tree order and wraps, Shift-Tab goes back, and
  what counts as focusable is what `accessible` marks, six tests on GTK
  (`focus_*`) and two on AppKit. A view that stops being accessible leaves the
  tab order, which is the part that had to be got right rather than added.
