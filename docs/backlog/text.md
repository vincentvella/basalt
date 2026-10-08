# Text

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (4):**

1. ~~Inline views (<Text><View/></Text>) measure as zero-sized attachments~~
2. No baseline, so alignItems: 'baseline' is wrong for text
3. ~~numberOfLines with ellipsizeMode: 'clip' does not truncate~~
4. Ignored: adjustsFontSizeToFit, textBreakStrategy, hyphenation, textShadow*, te
5. One PangoLayout is rebuilt per Paragraph per mutation, including layout-only u
6. ~~All measurement serialises on one mutex; see docs/DECISIONS.md~~
7. Text is not selectable and reports nothing to AT-SPI
8. ~~The mutex covering Pango is not held while text is drawn~~

- ~~**Inline views (`<Text><View/></Text>`) measure as zero-sized attachments.**~~
  Done 2026-10-07, on both hosts.

  React Native hands a text engine one fragment holding U+FFFC together with the
  size it measured for the view, and wants back a box reserved in the line and
  the frame that box ended up in. `ParagraphShadowNode::layout` puts that frame
  straight onto the child, so getting it wrong is not subtle: neither host
  reserved anything, so the character took whatever width the font gives a
  missing glyph, and every attachment came back at the origin with no size. A
  view inside a sentence rendered as a dot in the corner.

  Pango reserves it with `pango_attr_shape_new` over the fragment's byte range,
  anchored from `-height` to 0 so a tall view sits on the line rather than
  hanging below it, and `attachmentFrames` reads the positions back with
  `pango_layout_index_to_pos`, normalising the negative width Pango reports for a
  right-to-left run. Core Text reserves it with a `CTRunDelegate` answering
  width, ascent and descent, and `RnTextLayout frameForCharacterIndex:width:`
  reads them back, taking the *run's* own typographic bounds rather than the
  line's so a short view on a line of tall text reports its own height.

  Two things that were easy to get wrong and are worth keeping. The size reported
  back is React Native's, not the one the engine echoes, so a rounding difference
  in the reservation cannot move a view a fraction of a point off the box its own
  layout used. And on Core Text the index is UTF-16 while fragment strings are
  UTF-8 bytes, so the offset is accumulated in UTF-16 units: byte offsets are
  right for ASCII and wrong the moment an emoji precedes the view.

  Covered by thirteen unit tests across the two engines and by "an inline view
  inside a Text is given a box and told where it is", which asserts the sizes
  exactly, those coming from React Native, and the positions only against the
  paragraph's own left edge, those coming from the font. Each was checked to fail
  with the reservation removed and again with the position query removed.

  `isClipped` is still always false on GTK. It means a view fell outside the
  paragraph after `numberOfLines` cut it, which this host does not enforce yet;
  Core Text reports it, having a truncation to ask about.
- No baseline, so `alignItems: 'baseline'` is wrong for text.
  `pango_layout_get_baseline` is the value; plumbing it needs
  `TextLayoutManagerExtended`.
- ~~**`numberOfLines` with `ellipsizeMode: 'clip'` does not truncate.**~~ Done
  2026-10-07, on GTK. **AppKit already did it** and needed no change: it maps
  Clip to `truncates:NO` and applies the line limit independently of whether it
  is ellipsizing, so the measured box, the inline-view frames and the drawn ink
  all already stopped at line N. It got tests to hold that.

  Pango is the one that needed work, because a line limit is expressed as a
  negative height and acted on only while ellipsizing, so for clip the surplus
  lines stayed in the layout. `rn_pango_clip_height` now derives the cut from
  `pango_layout_iter_get_line_yrange`, the bottom edge of the last line kept
  rather than N times one line's height, so a paragraph with mixed font sizes is
  cut on a line edge. Both readers use it: `textLayoutSize` reports a box only as
  tall as the visible lines, and `rn_view_snapshot` pushes a clip node so the
  hidden lines are not painted.

  **The trap, which cost the first attempt.** Pango's default layout height is
  **-1**, not 0, measured rather than read from documentation. And -1 is exactly
  what a `numberOfLines={1}` layout carries, so the two cannot be told apart from
  the layout alone. The first version read every ordinary paragraph as a one-line
  one and cut the whole screen to its first line. Three pre-existing tests caught
  it. `buildTextLayout` now says "no limit" by setting the height to zero.

  The same shape, worth knowing: **React Native's `ellipsizeMode` defaults to
  `Clip`**, the first name in its enum, so every ordinary paragraph arrives
  asking to be clipped and only the line-limit guard stops it. A comment in that
  file used to say the default was Tail, wrong in the direction that matters.

  Not covered: the clip node itself, because no GTK test walks a render-node
  tree, so sabotaging the `gtk_snapshot_push_clip` call is caught by nothing.
  No e2e case either, `e2e/text.tsx` using the default mode. And a clipped
  paragraph's measured *width* still comes from every line including the hidden
  ones, which shows only on text with an explicit newline whose longest line is
  below the cut.
- Ignored: `adjustsFontSizeToFit`, `textBreakStrategy`, hyphenation,
  `textShadow*`, `textTransform`, `fontVariant`, `fontVariationSettings`.
- One PangoLayout is rebuilt per Paragraph per mutation, including
  layout-only updates that did not change the text.
- All measurement serialises on one mutex; see `docs/DECISIONS.md`.

- ~~**The mutex covering Pango is not held while text is drawn.**~~ Fixed
  2026-10-07, by deleting the mutex rather than widening it. Found
  2026-10-07 while reading backtraces for the GTK main-loop stall, and not the
  cause of it: nothing in either of those stacks goes near Pango. It is a
  separate latent race, recorded because looking for one bug found it.

  `PangoTextLayout.cpp:117` says what it is for, that the font map is reached
  from the layout thread and the GTK main thread and "one mutex covers every
  use". It does not. `pangoMutex()` is taken at `PangoTextLayout.cpp:220`, `:276`
  and `:286`, all of them building or measuring, while
  `gtk_snapshot_append_layout` runs under no lock at all, at `RnView.cpp:393`
  and `GtkTextPeer.cpp:59`.

  The two sides share state rather than merely resembling each other: every
  layout is `pango_layout_new(sharedPangoContext())` at `PangoTextLayout.cpp:222`,
  one context for the process, and drawing a layout reaches its context and font
  map to shape and load glyphs. So a draw on the main thread can run against a
  font map that Fabric's layout thread is using under the lock.

  **Established 2026-10-07, and the answer inverts the question.** Pango's own
  documentation, in the version installed here, says the default font map is
  **per-thread**: "since Pango 1.32.6, the default fontmap is per-thread. Each
  thread gets its own default fontmap. In this way, PangoCairo can be used safely
  from multiple threads." Its answer to cross-thread use is isolation, not
  locking, and `libpangocairo` imports the `GPrivate` that implements it.

  `sharedPangoContext()` opts out of that with a function-local static, so one
  context, built from the font map of whichever thread got there first, is used
  from both. **The shared font map this mutex exists to protect is
  self-inflicted.** So the fix is to stop sharing rather than to extend the lock,
  and that also closes entry 6, measurement no longer serialising on a
  process-wide mutex.

  The race is also worse than this entry assumed. The drawn layout has not been
  laid out when it reaches the draw site: `GtkMountingManager` builds it and only
  sets properties, nothing queries its size, so `gtk_snapshot_append_layout` is
  what performs the itemisation, font resolution and shaping, on the main thread,
  unlocked. That is the same work the measuring path deliberately serialises.

  One argument against simply locking the draw, flagged as reasoning rather than
  read from source: GSK rasterises glyphs during `gsk_renderer_render`, after the
  snapshot call returns, still reading shared `PangoFont` objects. If that holds,
  a lock around the append covers shaping and leaves rasterisation exposed, which
  makes it a narrowing rather than a fix. Worth checking against GTK's source,
  which is not on this machine, before acting.

  Not demonstrable by a unit test: the GTK suite is single-threaded and nothing
  in it touches a snapshot. A stress loop would prove the bug when it fired and
  nothing when it did not. ThreadSanitizer would be largely blind, since the
  races live inside Homebrew's prebuilt Pango, which it would not instrument.
  The testable thing is the fix: record the owning thread on each context and
  assert it.

  **What was done.** `threadPangoContext()` gives each thread its own context,
  rebuilt when `fontGeneration` changes so a runtime-registered font is not
  invisible to a thread that measured before it appeared, and `pangoMutex()` is
  gone along with its four lock sites. `GtkMountingManager` now asserts the main
  thread where it hands a layout to a widget, which is the one condition the
  arrangement rests on and which previously held only incidentally.

  Three tests: two threads get different contexts, one thread reuses its own
  rather than building one per layout, and measuring from two threads at once
  does not deadlock. The first fails if the context is shared again. None of them
  can show the race itself, which is the honest limit, and the font-reload path
  is untestable here for want of a font fixture and because `registerFont` is
  inert on macOS anyway, the CoreText map failing the `PANGO_IS_FC_FONT_MAP`
  guard.

  **A second bug, from the same fact.** `FontRegistryFontconfig.cpp:87`
  invalidates `pango_cairo_font_map_get_default()`, whichever thread's map that
  is, which need not be the one behind `sharedPangoContext()`. So a
  runtime-registered font can be invisible to the layouts that matter. On macOS
  it is inert regardless: the backend here is `PangoCoreTextFontMap`, so the
  `PANGO_IS_FC_FONT_MAP` guard is false and an app font never reaches Pango at
  all.
- Text is not selectable and reports nothing to AT-SPI.
