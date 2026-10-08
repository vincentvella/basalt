# Accessibility

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (4):**

1. Not tested against a real screen reader
2. Accessible actions are unimplemented: IMountingManager declares accessibleClic
3. accessibilityRole cannot change after mount on GTK; see docs/DECISIONS.md
4. accessibilityLiveRegion, accessibilityLabelledBy and accessibilityActions are ignored

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
