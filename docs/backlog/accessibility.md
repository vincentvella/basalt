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
- `accessibilityLiveRegion`, `accessibilityLabelledBy`, `accessibilityValue`
  and `accessibilityActions` are ignored.
- ~~No keyboard focus model, so nothing is reachable by Tab.~~ Done on both:
  Tab visits focusable views in tree order and wraps, Shift-Tab goes back, and
  what counts as focusable is what `accessible` marks, six tests on GTK
  (`focus_*`) and two on AppKit. A view that stops being accessible leaves the
  tab order, which is the part that had to be got right rather than added.
