# macOS

Part of the [backlog](../BACKLOG.md). Not scheduled.

Six of this file's entries were struck on 2026-09-18 after being checked
against the code rather than remembered. Five of them had been done for days.
If an entry here is about to be picked up, run the thing it describes first.

**Open (3):**

1. ~~Justified text~~, which worked all along
2. ~~Fonts loaded at runtime are untested~~
3. ~~`keyboardType`, `autoCapitalize`, `autoCorrect` and `spellCheck`~~: two
   done on both hosts, two recorded here as having no macOS call at all
4. Nothing tested against a real screen reader
5. ~~No accessibility subroles~~
6. ~~`accessibilityValue`, `accessibilityLiveRegion` and `accessibilityLabelledBy`~~
7. No animated images
8. ~~No gesture cancellation from the platform~~, and the half of it that
   was already handled
9. Only the first `dropShadow()` in a filter list is drawn

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
- ~~**Fonts loaded at runtime are untested.**~~ Tested on both hosts, and run
  end to end on macOS, 2026-10-09. Four tests each in
  `tests/test_appkit_fonts.mm` and `tests/test_fonts.cpp`, plus `e2e/fonts.tsx`
  and a scenario.

  **The case a loader exists for is a family the machine does not have**, and
  three quarters of a font test can pass without ever meeting it. Registering a
  system font under a second, app-chosen name proves the mapping and nothing
  underneath it, because every layer already knew that family. So
  `tests/RenamedFontFile.h` makes a font the machine lacks out of one it has: it
  copies a real file and replaces one byte of the family name wherever the name
  table spells it, in ASCII and in the UTF-16BE of a Windows-platform name
  record, so nothing in the file moves and no checksum has to be recomputed.
  Shipping a fixture font was the alternative, and it would have had to be
  renamed anyway to be sure the machine lacked it.

  What each test pins, and the sabotage that showed it discriminates:

  | Test | Fails when |
  | --- | --- |
  | the mapping | `resolveFontFamily` returns its argument |
  | the paragraph is laid out in it | the same |
  | a measurement from before stops counting | the cache is not emptied on a new font generation |
  | a family the system lacks becomes usable | `CTFontManagerRegisterFontsForURL` is never called |

  The third is the one a real app depends on and the easiest to lose: every
  loader resolves after the first render, so a paragraph measured in the
  fallback has to be measured again, and `fontGeneration` is the only thing that
  makes that detectable. The AppKit sabotage of the registration call fails the
  fourth test alone, which is the shape a good test row has.

  **The end-to-end half found a real instrument bug rather than a platform
  one.** `expo-font` on a non-web platform is one call, and the host's half of
  it only exists in a build with `-DBASALT_EXPO_MODULES_CORE`, so the scenario
  skips by name without one and the hosts' own suites carry the proof. Run for
  real against Expo 57: the first version of `e2e/fonts.tsx` reported both
  paragraphs at 852 points wide, because a `<Text>` in a column stretches and
  its frame is then the box it was given rather than the width it measured.
  `alignSelf: 'flex-start'` is what makes the frame the measurement. With that,
  the loaded font measures differently and forgetting the mapping puts both back
  to one number, which is the scenario's own sabotage.

  **GTK on macOS cannot see a runtime font at all**, which is worth writing down
  because the GTK suite runs next to the AppKit one here. Homebrew's Pango
  answers `pango_cairo_font_map_get_default` with a
  `PangoCairoCoreTextFontMap`, and that map never consults fontconfig, so
  `FcConfigAppFontAddFile` succeeds and the family stays invisible. The test
  asserts the mapping and stops, saying why. It also means the Pango-context
  reload in `PangoTextLayout.cpp` is caught by nothing on a Mac: sabotaging it
  leaves all 580 tests passing here, and Linux is where that one is proved.
- ~~**No `maxLength` on macOS.**~~ Done, on both peers:
  `textinput_max_length_is_enforced_on_both_peers`.
- ~~**No multiline `<TextInput>`**~~ Done on both: the NSTextView and
  GtkTextView peers, with the text and the selection round-tripping through
  each, `textinput_multiline_builds_a_text_view` and its two siblings.
- **`keyboardType` and `autoCapitalize`, and the premise of this entry was
  wrong.** It said these four were missing "on macOS, all of which AppKit has
  some form of". Checked on 2026-10-09: **neither host read any of the four**,
  and AppKit does not have some form of all of them. What each toolkit has,
  measured against the two SDKs rather than remembered:

  | Prop | GTK | AppKit |
  | --- | --- | --- |
  | `spellCheck` | `GTK_INPUT_HINT_SPELLCHECK` and its negative | `NSTextView.continuousSpellCheckingEnabled` |
  | `autoCorrect` | no hint exists | `automaticSpellingCorrectionEnabled` |
  | `autoCapitalize` | `GTK_INPUT_HINT_UPPERCASE_CHARS`, `_WORDS`, `_SENTENCES`, `LOWERCASE` | nothing per field |
  | `keyboardType` | `gtk_entry_set_input_purpose` | nothing; there is no software keyboard |

  `NSSpellChecker.automaticCapitalizationEnabled` looks like the answer for the
  third and is not: it is a **class, read-only** property, which is the person's
  system-wide setting rather than something a field can ask for.

  **`spellCheck` and `autoCorrect` are done, 2026-10-09**, through
  `core/TextChecking.h` -- three states, because unset is not false: a field
  that says nothing wants whatever the platform does, and resolving that to
  `false` would opt every ordinary field out of spell checking without the app
  asking.

  Two things that only measuring found:

  - **A single-line field has neither property.** They belong to NSTextView, and
    an NSTextField *borrows* one as its field editor while it is focused. So the
    peer remembers the request and applies it again in `becomeFirstResponder`,
    which is the first moment there is an editor to apply it to.
  - **macOS may refuse.** `NSAllowContinuousSpellChecking` is a user-wide
    setting, and with it off `setContinuousSpellCheckingEnabled:YES` is ignored
    -- on a bare NSTextView, with and without a window. It is 0 on the machine
    this was written on, which is why the suite asserts the *request*
    unconditionally and AppKit's own answer only where the machine allows it. A
    test that asserted the latter everywhere would pass on CI and fail on a
    developer's Mac, which is the shape of flake this project has already spent
    a day on.

  **`autoCapitalize` and `keyboardType` are done too, 2026-10-09, and on this
  host that means *reported and not acted on*.** GTK turns the first into three
  more input hints -- capitalise characters, words or sentences -- and the
  second into an input purpose, which is what an input method reads and what
  brings up a number pad on a Linux tablet. macOS has neither: no per-field
  automatic capitalisation, and no software keyboard whose layout a purpose
  could choose.

  So both hosts print what the app asked for in the tree dump, in the same
  words, and this file is where the difference is written down rather than
  being a missing line in a diff. Two details worth keeping:

  - **React Native's default for capitalisation is `sentences`**, as on iOS, and
    the prop is a plain enum with no "did not say" -- so every field asks for
    sentence capitalisation unless it says otherwise, and GTK sets the hint. A
    test pins that, because it is the kind of behaviour a later reader takes
    for a bug.
  - **`none` is the absence of the three hints, not `GTK_INPUT_HINT_LOWERCASE`.**
    That hint asks the input method to lowercase what the person typed, which
    no app asked for.

  The mapping from fourteen keyboard types to eleven purposes is in
  `GtkTextInput.cpp` with its reasoning: `number-pad` is `DIGITS` where
  `numeric` and `decimal-pad` are `NUMBER`, which allows a separator and a
  sign; `ascii-capable`, `numbers-and-punctuation`, `twitter`, `web-search` and
  `visible-password` describe keyboards rather than kinds of text and are
  ordinary `FREE_FORM`.
- **Nothing tested against a real screen reader**, on either platform.
  VoiceOver and Orca are both a manual step nobody has taken; the unit tests
  assert the properties were set and cannot assert the result is usable.
- ~~**`accessibilityValue`, `accessibilityLiveRegion` and
  `accessibilityLabelledBy`.**~~ All three done, the first earlier and the other
  two on 2026-10-08; see [accessibility.md](accessibility.md), which has the
  detail and the shared registries both of the latter two needed. Left here as a
  struck entry rather than deleted, because this file's own note at the top is
  about entries that go stale and get read as though they were still true.

- ~~**No accessibility subroles.**~~ Done 2026-10-09. macOS says some things
  with a role and the rest with a *subrole*, and four of React Native's roles
  are in the second group:

  | Role | AppKit role | AppKit subrole |
  | --- | --- | --- |
  | `search` | text field | `NSAccessibilitySearchFieldSubrole` |
  | `switch` | check box | `NSAccessibilitySwitchSubrole` |
  | `togglebutton` | check box | `NSAccessibilityToggleSubrole` |
  | `tab` | radio button | `NSAccessibilityTabButtonSubrole` |

  Each role alone loses the part that says what the thing is: a switch and a
  toggle button are both "check box", and a tab is "radio button".

  **GTK needed nothing**, which is why this was a macOS entry and worth
  confirming rather than assuming: `GTK_ACCESSIBLE_ROLE_SEARCH_BOX`, `_SWITCH`
  and `_TAB` are roles of their own there, and the mounting manager already
  mapped all three. So this closes a real difference between the two hosts
  rather than adding a macOS flourish.

  Nothing else gets a subrole. One that is not AppKit's own tells VoiceOver
  less than none at all, and a view without one is read by its role, which is
  already the nearest thing available.

  Four tests, including the two that matter for a prop that can change after
  mount: a role taken away takes its subrole, and a role replaced replaces it.
  AppKit allows that where GTK does not, a `GtkAccessible`'s role being
  construct-only, which `docs/DECISIONS.md` records.
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
- ~~**No gesture cancellation from the platform.**~~ Done, and **half of this
  entry's premise was wrong.** It said `dispatchTouchCancel` exists and nothing
  calls it. Something did, and had for as long as drag-out has been here:
  `AppKitTouchDispatcher.mm:48` cancels the press when
  `beginDraggingSessionWithItems:` takes the gesture, because AppKit then runs
  its own loop until the drop and the touch that started the drag can never
  end. The case with no handler was the other one the entry named, a press
  interrupted by the window losing focus.

  The notification is `NSWindowDidResignKeyNotification`, and the dispatcher
  observes it itself rather than taking it from the app delegate.
  `main_appkit.mm` is not linked into the test suite, so a delegate method
  there would have put the part worth testing out of reach of any test: that
  the notification reaches a press in flight. It registers with `object:nil`
  and filters on delivery against `surfaceRoot_.window`, since another window
  losing focus is not this surface's press being interrupted.

  `dispatchTouchCancel()` already does nothing when nothing is down, so the
  observer needs no state of its own; the destructor removes it.

  Two tests, each with the sabotage that proves it discriminates.
  `a_press_is_cancelled_when_the_window_stops_being_key` fails when the cancel
  call is dropped, and `a_press_survives_another_windows_focus_loss` fails when
  the window filter is. Both drive the real path, a real `NSWindow`,
  `rnMouseDownAt:` with no release, and the notification posted for real, so
  neither is asserting on a helper of its own.

  **GTK needed nothing**, which is the part of the entry that held up:
  `GtkGestureClick` has a `cancel` signal, wired at
  `GtkTouchDispatcher.cpp:65` since the gestures landed. AppKit having no
  equivalent is why this was a macOS entry.
- ~~Within `<View>`: per-corner radii, borders, transform, z-index and
  pointer-events are unmapped.~~ All five are mapped, and have been since
  2026-09-13; see "The borders macOS was never drawing". The entry outlived
  the gap by five days, which is the second time this file has claimed
  something missing that was done; the title bar was the first.

  Checked by comparing rather than by reading: `scripts/compare_hosts.sh focus
  BasaltFocus` has GTK and AppKit agreeing on `radii=`, `borderw=` and
  `borderc=`, and `index BasaltDemo` on `transform=`.

- **Only the first `dropShadow()` in a filter list is drawn.** `filter:
  'drop-shadow(...) drop-shadow(...)'` is legal CSS and the GTK host draws both,
  GSK's shadow node taking an array. This host draws the first and logs once,
  because the mechanism is the layer's own shadow and a `CALayer` has one.

  **Three platforms, three behaviours, measured 2026-10-09 rather than
  assumed:**

  | | With `drop-shadow(a) drop-shadow(b)` |
  | --- | --- |
  | GTK | draws both, GSK's shadow node taking an array |
  | basalt on macOS | draws `a`, the first |
  | React Native on iOS | draws `b`, the **last** |

  iOS's is not a decision either: `RCTViewComponentView.mm` loops over the
  filter list and calls `[_swiftUIWrapper updateDropShadow:...]` for each one,
  and that is a setter, so the last call wins. So the reference platform an app
  author would compare against keeps a different one from this host.

  **Why the wrapper-layer idea in this entry is a worse trade than it sounds.**
  A `CALayer` casts its shadow from its *rendered subtree*, which is what makes
  a shadow follow the content's alpha, so an extra shadow needs a layer whose
  subtree is this view's content. A superlayer would do it and is not available:
  an NSView's layer has the parent *view*'s layer as its superlayer and AppKit
  owns that relationship. A sibling layer needs the content rendered into it,
  which means snapshotting a live view hierarchy on every mutation and keeping
  the snapshot in step with children, text, images and size.

  There is a cheap case, a view whose alpha *is* its border box, where an
  extra shadow could reuse the box-shadow layers that already take a path. It is
  wrong for exactly the views the filter exists for: text, a transparent image,
  children with gaps between them. Conditioning on "has no children and
  no text and no image" is a lot of behaviour that changes with the content, for
  a prop that is rare.

  So this stays recorded. What *is* worth doing if it is ever picked up is
  deciding between CSS's answer (draw them all, which GTK already does) and
  iOS's (keep the last), rather than keeping a third answer that matches
  neither. backlog/correctness.md has the rest of the function.
