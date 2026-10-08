# macOS

Part of the [backlog](../BACKLOG.md). Not scheduled.

Six of this file's entries were struck on 2026-09-18 after being checked
against the code rather than remembered. Five of them had been done for days.
If an entry here is about to be picked up, run the thing it describes first.

**Open (6):**

1. ~~Justified text~~, which worked all along
2. Fonts loaded at runtime are untested
3. No `keyboardType`, `autoCapitalize`, `autoCorrect` or `spellCheck`
4. Nothing tested against a real screen reader
5. No accessibility subroles
6. ~~`accessibilityValue`, `accessibilityLiveRegion` and `accessibilityLabelledBy`~~
7. No animated images
8. No gesture cancellation from the platform

- ~~**Hit testing ignores `transform`.**~~ Fixed, the way Windows already did
  it: `RnAppKitView` gains `rnLocalToParent`, the frame's translation with the
  transform composed in about the centre, and the hit test inverts it on the
  way down instead of subtracting the frame's origin. The two were the same
  thing for every untransformed view, which is why subtracting passed every
  test there was.

  Public for the same reason Win32's `localToParent` is: hit testing inverts
  what placement produces, and deriving it twice is how the two come to
  disagree.

  A transform with no 2D inverse is the case worth naming. `scale: 0` is legal
  and draws nothing, so it is skipped and the press reaches what is behind it;
  a perspective transform is not affine, and answers with the translation
  alone, which is what every transform got before this.

  Three tests, and each was run against the old code first to check it failed:
  a translation, which moves the drawing and not the frame; a quarter turn,
  which cannot be got right by adjusting an offset because the corners leave
  the frame and the centre does not move; and the degenerate scale.
- ~~**No accessibility.**~~ Done: roles, names, hints, states and hiding, with
  nine tests in `test_appkit_accessibility.mm`. What is left of it is listed
  separately below: subroles, `accessibilityValue`, and nothing having been
  tried against a real screen reader, which is a different claim from the one
  this entry made.
- ~~**Justified text.**~~ **It works, and has all along.** Checked 2026-10-08 by
  drawing it, after this file's own instruction to run the thing an entry
  describes before picking it up -- which is exactly what nobody had done here.

  The entry said Core Text ignores justification for lines drawn individually,
  which is true and is not what `RnTextLayout` does: `linesForWidth:` takes its
  lines out of a `CTFrame`, and Core Text justifies the lines in a frame itself.
  The implementation the entry described -- `CTLineCreateWithAttributedString`
  per line -- is the one it does not have.

  Measured in a 200 point box: the first line of a justified paragraph reaches
  column 199 where the same text drawn flush left stops at 186, and the last line
  stops at 125, Core Text leaving it alone as typography and Pango both do.
  `text_justified_lines_reach_both_edges` and
  `text_the_last_justified_line_is_not_stretched` pin both halves, and GTK has the
  first of them too now, over the pixel helper that landed the same day --
  sabotaging `pango_layout_set_justify` to FALSE fails it, which is the one-flag
  bug that test exists for.

  **What was nearly committed instead**, and is worth recording: a
  `CTLineCreateJustifiedLine` per line, exactly as the entry proposed. It passed
  its own test, because the lines were already justified and stretching them again
  to the same width changes nothing. Sabotaging it is what showed the feature
  working without it -- the test passed with the new code disabled, which is the
  signal that the code was not what made it pass.
- **Fonts loaded at runtime are untested.** `resolveFontFamily` is wired into
  the Core Text font lookup, and `expo-font` on macOS has never been run end to
  end.
- ~~**No `maxLength` on macOS.**~~ Done, on both peers:
  `textinput_max_length_is_enforced_on_both_peers`.
- ~~**No multiline `<TextInput>`**~~ Done on both: the NSTextView and
  GtkTextView peers, with the text and the selection round-tripping through
  each, `textinput_multiline_builds_a_text_view` and its two siblings.
- **No `keyboardType`, `autoCapitalize`, `autoCorrect` or `spellCheck`** on
  macOS, all of which AppKit has some form of.
- **Nothing tested against a real screen reader**, on either platform.
  VoiceOver and Orca are both a manual step nobody has taken; the unit tests
  assert the properties were set and cannot assert the result is usable.
- ~~**`accessibilityValue`, `accessibilityLiveRegion` and
  `accessibilityLabelledBy`.**~~ All three done, the first earlier and the other
  two on 2026-10-08; see [accessibility.md](accessibility.md), which has the
  detail and the shared registries both of the latter two needed. Left here as a
  struck entry rather than deleted, because this file's own note at the top is
  about entries that go stale and get read as though they were still true.

- **No accessibility subroles.** A search field should be a text field with
  `NSAccessibilitySearchFieldSubrole`; reporting only the role loses the "this
  searches" part.
- **No animated images.** The first frame of a GIF is drawn as a still, on both
  desktops.
- ~~**No scrollbars.**~~ Done, and on all three, the claim that the GTK side
  got them from its widget theme was never true: neither host drew one. See the
  ScrollView section for the shape. AppKit's own are `NSScroller`, which comes
  with `NSScrollView` and so was never available here, so the indicator is drawn
  from `core/ScrollIndicator.h` like the other two.
- ~~**No scroll momentum or elasticity.**~~ Done: AppKit reports the phases the
  system's own deceleration goes through, which is what lets
  `onMomentumScrollBegin` and `onMomentumScrollEnd` be answered honestly here.
  See the ScrollView section for what is left, which is Windows.
- ~~**Nothing is reachable by Tab**~~ Done: Tab visits focusable views in tree
  order and wraps, and what counts as focusable is what `accessible` marks,
  `focus_tab_visits_focusable_views_in_tree_order_and_wraps`.
- **No gesture cancellation from the platform.** `dispatchTouchCancel` exists
  and nothing calls it: AppKit has no equivalent of GTK's gesture `cancel`, and
  the case it covers (a press interrupted by the window losing focus) has no
  handler yet.
- ~~Within `<View>`: per-corner radii, borders, transform, z-index and
  pointer-events are unmapped.~~ All five are mapped, and have been since
  2026-09-13; see "The borders macOS was never drawing". The entry outlived
  the gap by five days, which is the second time this file has claimed
  something missing that was done; the title bar was the first.

  Checked by comparing rather than by reading: `scripts/compare_hosts.sh focus
  BasaltFocus` has GTK and AppKit agreeing on `radii=`, `borderw=` and
  `borderc=`, and `index BasaltDemo` on `transform=`.
