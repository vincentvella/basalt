# Text

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (4):**

1. ~~Inline views (<Text><View/></Text>) measure as zero-sized attachments~~
2. No baseline, so alignItems: 'baseline' is wrong for text, and the plumbing is
   upstream's
3. ~~numberOfLines with ellipsizeMode: 'clip' does not truncate~~
4. Ignored: adjustsFontSizeToFit, textBreakStrategy, hyphenation,
   fontVariationSettings (~~textTransform~~, ~~textShadow*~~,
   ~~the font-scaling three~~, ~~the decoration pair~~, ~~fontVariant~~ and
   ~~a fragment's opacity~~ are done on GTK and AppKit)
5. One PangoLayout is rebuilt per Paragraph per mutation, including layout-only u
6. ~~All measurement serialises on one mutex; see docs/DECISIONS.md~~
7. Text is not selectable and reports nothing to AT-SPI
8. ~~The mutex covering Pango is not held while text is drawn~~

- ~~**Inline views (`<Text><View/></Text>`) measure as zero-sized attachments.**~~
  Done 2026-10-07 on GTK and AppKit. **Windows is the host left**, and that is
  recorded here rather than being implied by "both hosts": see the end of this
  entry.

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

  **Windows, and how it was noticed.** `DirectWriteLayoutManager` reports one
  zero-sized attachment per fragment, which keeps the count `ParagraphShadowNode`
  iterates over right and positions nothing, and `DirectWriteLayout` skips
  attachment fragments when it builds the runs. Both carried a comment saying
  this was "the same gap both other desktops have", which stopped being true the
  day the two were fixed.

  The scenario was added for those two and runs on every host, so it had been
  failing Windows CI on main for a day: twelve of thirteen in that shard, with
  the attachment measuring 0x0 against the 48x24 it asked for. Skipped on Windows
  now, by name and with a reason, which is what the suite does elsewhere for a
  gap one host has.

  What it would take: `IDWriteTextLayout::SetInlineObject` over the fragment's
  range, with an `IDWriteInlineObject` whose `GetMetrics` answers the width,
  height and baseline React Native measured, and `HitTestTextPosition` to read
  the box back afterwards. The same shape as the other two, which is the useful
  part of having done them first.
- **No baseline, so `alignItems: 'baseline'` is wrong for text, and the plumbing
  is not ours to add.** Measured 2026-10-08 rather than reasoned about, and the
  entry used to make it sound like local work.

  What a run does. A row with `alignItems: 'baseline'` holding a 40pt box, a 32pt
  `<Text>` and a 12pt one lays out as

      view tag=2  frame=(20,20 40x40)   bg=#ff0000ff
      view tag=6  frame=(60,60 46x33)   text="Big"
      view tag=10 frame=(106,60 29x13)  text="small"

  with both paragraphs at y=60, which is the box's *bottom*. React Native says why
  itself, three times over, once per measure pass:

      W ParagraphShadowNode.cpp:294] Baseline alignment is not supported by the
      current platform

  Yoga uses a node's height as its baseline when it has no baseline function, so
  the box's baseline is 40 and the text's is 0 -- the top of each paragraph is
  aligned with the bottom of the box, which is as wrong as it can be while still
  being deterministic.

  **The gate is a compile-time check against a header this project does not
  own.** `ParagraphShadowNode::baseline` asks
  `TextLayoutManagerExtended::supportsLineMeasurement()`, which is a `requires`
  expression on the platform's `TextLayoutManager` type: it is true when that
  class declares `measureLines(AttributedStringBox, ParagraphAttributes, Size)`.
  Android's and iOS's do. The cxx platform's declares `measure` and nothing else,
  so the answer is false for every ReactCxxPlatform app before a host writes a
  line of code.

  This host replaces the *definition* of that class -- see the top of
  `PangoTextLayoutManager.cpp` -- and deliberately leaves the header alone, which
  is what keeps the arrangement maintainable. A subclass does not help: the
  concept is checked on the static type. So the fix is one declaration and one
  stub upstream, recorded in [upstream.md](upstream.md), and after it lands this
  side is small: `pango_layout_get_baseline` and `CTLineGetTypographicBounds`
  already have the number.

  The same gate takes `onTextLayout` with it, which is the other half worth
  knowing: a `<Text onTextLayout>` gets "onTextLayout is not supported by the
  current platform" from the same file for the same reason, so an app cannot
  measure its own lines either.
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

  The clip node is covered as of the same day, which it was not when this was
  first written. `a_clipped_paragraph_paints_through_a_clip_node` in
  test_viewprops.cpp allocates an RnView, calls its snapshot vfunc directly and
  counts the clip nodes in the render tree it produces, against the same
  paragraph with no limit. Two things made that possible without a window: an
  allocated widget snapshots to a real node where an unallocated one snapshots
  to nothing, and the vfunc can be called directly where
  `gtk_widget_snapshot_child` wants a realized parent. It counts clip nodes
  anywhere in the tree rather than asserting their position, since where GSK
  nests one is its business.

  Still not covered: there is no e2e case, `e2e/text.tsx` using the default
  mode. And a clipped paragraph's measured *width* still comes from every line
  including the hidden ones, which shows only on text with an explicit newline
  whose longest line is below the cut.
- Ignored, and now counted: walking `TextAttributes` field by field on
  2026-10-09 -- which is what `scripts/scrape_props.py` does for the support page
  -- the fields no host reads are `fontVariationSettings`, `layoutDirection`,
  and the `accessibilityRole` and `role` that ride along on a fragment. `lineBreakMode` is read on AppKit only. Beside those,
  `adjustsFontSizeToFit`, `textBreakStrategy` and hyphenation are
  `ParagraphAttributes` and equally ignored.

  Four of them are iOS's own with no desktop equivalent, and are not gaps so
  much as vocabulary: `dynamicTypeRamp` and `lineBreakStrategy` name iOS
  behaviours, and `isHighlighted` and `isPressable` are the internals of iOS's
  pressable text. `textEffects` is newer than the pin.

  What is left of that list is `fontVariationSettings`, which is newer than the
  pin.

  **`baseWritingDirection` came off it on 2026-10-09**, and it is a smaller
  thing than "RTL support", which this entry used to conflate it with: the prop
  sets one paragraph's direction, where RTL as a whole means mirroring a layout
  and is `layoutDirection`'s question.

  Both engines take the direction directly -- a Pango context's base direction,
  an `NSParagraphStyle`'s `baseWritingDirection`, which is the same call
  upstream's iOS half makes -- and **both of them left the line against the left
  edge anyway**, which is the part worth writing down:

  - Pango flips a natural alignment for a right-to-left line only while
    `auto_dir` is on, and an explicit direction is exactly what turns that off.
  - Core Text resolves a natural alignment inside a *frame*, and `RnTextLayout`
    draws its own lines so that it can honour `numberOfLines`, so the frame
    never gets the chance.

  So each host needed the same decision in a different place: a natural
  alignment follows the writing direction. Found by a pixel test on each side
  rather than by reading, twice: the direction reached the engine, the glyphs
  came out in the right order inside the line, and the line sat on the wrong
  edge. An assertion about the attribute alone would have passed.

  The dump reports the direction as `writing-dir=`, from the first fragment
  that asks, which carries the same limit as the text shadow: one direction per
  paragraph, a nested `<Text>` with its own being recorded rather than drawn.

  None of this was written down before the support page needed a status per
  attribute, which is the argument for having one.

  **`fontVariant` and a fragment's `opacity` came off this list on 2026-10-09**,
  into `core/FontVariants.h` and `core/TextColors.h`. Two notes:

  - The twenty-five flags are resolved to OpenType tags in core, which serves
    GTK and would serve Win32 unchanged. AppKit is the host that needs a
    translation: Core Text takes Apple's older AAT pairs, a feature type and a
    selector inside it, so the flags are walked in core and each host spells
    them its own way. Upstream's iOS half does the same in `RCTFontFeatures`,
    which is where the AppKit table was copied from.
  - Whether a variant *changes* anything depends on the font. The system font
    has small caps and tabular figures and has neither oldstyle figures nor a
    twentieth stylistic set, and `[NSFont fontWithDescriptor:]` silently drops
    features the resolved font does not have. So the AppKit suite asserts the
    mapping rather than what a font kept of it, and separately that one feature
    the system font does have survives onto the font. The first version of that
    test asked the font and failed on three of eight variants, which is a
    failure that looks like a wrong mapping and is not.

  `TextColors.h` took two rules that were spelled twice and one that was spelled
  nowhere: React Native's default foreground is opaque black rather than the
  platform's label colour, a background nobody set is nothing rather than
  transparent black, and `opacity` multiplies both alphas. That last is
  upstream's `RCTEffectiveForegroundColorFromTextAttributes`, including the
  detail that it applies to the default black as well.

  **`textDecorationColor` and `textDecorationStyle` came off this list on
  2026-10-09**, into `core/TextDecorations.h`, and the style is `partial` on
  both hosts rather than done. That is not laziness, it is the two engines:

  | Style | Pango | Core Text |
  | --- | --- | --- |
  | solid | `PANGO_UNDERLINE_SINGLE` | `NSUnderlineStyleSingle` |
  | double | `PANGO_UNDERLINE_DOUBLE` | `NSUnderlineStyleDouble` |
  | dotted | nothing | `...Single \| ...PatternDot` |
  | dashed | nothing | `...Single \| ...PatternDash` |
  | wavy | `PANGO_UNDERLINE_ERROR` | nothing |

  Pango's underline is an enum with no patterns in it and its strikethrough is a
  boolean with no style at all, so dotted and dashed fall back to a single line
  there; Core Text has no wavy, so that falls back here. Each host draws a line
  rather than nothing, which is wrong in a way a reader can see instead of
  wrong in a way that looks like the prop being ignored.

  What would close it is the same work on both: draw the decoration rather than
  ask for it. GTK already has the hook, `rn_view`'s snapshot, and Pango gives
  the position and thickness through `pango_font_metrics_get_underline_position`
  and `..._underline_thickness`; the line itself is a `gtk_snapshot_append_color`
  per dash. AppKit would need an `NSLayoutManager` subclass or the same manual
  pass in `RnTextLayout`, which draws through `CTLineDraw` and would have to ask
  `CTFontGetUnderlinePosition` for the same two numbers.

  There is also no end-to-end scenario for the pair, and that is deliberate. A
  decoration is per *fragment* and the tree dump is per view, so there is
  nothing in a dump to assert on; a dump line could only say what was asked for,
  which on GTK would read `dotted` beside a solid line. What asserts these is a
  pixel test per host, which is the thing a dump cannot do: the GTK one renders
  white text with a red underline and counts red pixels, and the AppKit one does
  the same in reverse, each with the uncoloured render as its negative control.

  **`allowFontScaling` and `maxFontSizeMultiplier` came off this list on
  2026-10-09**, with `fontSizeMultiplier`, into `core/FontScaling.h`. Two things
  are worth keeping from that:

  - `dynamicTypeRamp` is the one left of the group, and it is iOS's by
    construction. Upstream resolves it with `[UIFontMetrics
    metricsForTextStyle:]` and `-scaledValueForValue:`, asking the system what
    size a text style is at the user's setting. macOS's nearest equivalent is
    `[NSFont preferredFontForTextStyle:options:]`, which answers a *font* rather
    than a multiplier and only moves with Accessibility's "Text size"; GTK and
    Win32 have no ramp at all. So it is a mapping from eleven iOS text styles to
    eleven sizes, which is a table somebody has to choose rather than a call to
    make.
  - A text scale changed while the app is running does not reflow text already
    laid out. GTK notices the change, `notify::gtk-xft-dpi` on `GtkSettings`
    being what main_gtk.cpp connects, and new paragraphs come out at the new
    scale; but Fabric is holding measurements taken at the old one. What would
    fix it is telling the shadow tree that every paragraph is dirty, which is a
    re-measuring commit rather than anything in the host.

  **`textTransform` is done on GTK and AppKit, 2026-10-08**, and it is the one of
  that list that is not a drawing question at all: an uppercase label is a
  different string, so the transform has to happen before anything measures it or
  the paragraph wraps in the wrong place. Both hosts apply it where they build
  their layout, which is the one place both measurement and drawing go through.

  **Not shared, deliberately.** Unicode case mapping belongs to the toolkit:
  `g_utf8_strup` knows that ß uppercases to SS and that an accented letter has a
  case at all, and NSString's `uppercaseString` knows the same, where a byte-wise
  `std::toupper` looks right in English and silently leaves both alone. So each
  host uses its own, and the two are asserted against the same cases -- "café"
  and "straße" are in both suites, and sabotaging GTK's to a byte-wise loop gives
  "CAFé" and "STRAßE" while every ASCII case still passes.

  `capitalize` follows React Native's own rule, from `RCTAttributedTextUtils.mm`:
  split on single spaces, and a word whose first character is not a digit is
  capitalised, which lowercases the rest of it -- so "iOS" becomes "Ios". That is
  surprising, it is what the other platforms do, and both suites pin it.

  **The part that was nearly a bug.** Both hosts index into the laid-out string
  to place inline views: GTK by byte offset, AppKit by UTF-16 offset, each
  accumulated fragment by fragment. A transform can change the length -- ß to SS
  -- so the offsets had to be taken from the transformed text as well, which is
  why the helper is shared between each host's layout and its layout manager
  rather than living inside the former.

  Four tests per host over the same cases, and a scenario that reads
  `text="SHOUT QUIETLY"` and `text="Ios And Android"` out of e2e/text.js on both
  while asserting the untransformed strings are *gone* -- which is what says the
  engine laid out the transformed text rather than drawing over the original.
  Windows is skipped by name.
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

- ~~**`textShadowColor`, `textShadowOffset` and `textShadowRadius`.**~~ Done on
  GTK and AppKit 2026-10-09. Three props every pre-CSS React Native title sets
  and no host read.

  **One shadow per paragraph, and that is a limit rather than the spec.** They
  are `TextAttributes`, so they arrive per *fragment* -- per `<Text>` inside a
  `<Text>` -- and neither engine here can draw a different shadow per run: GTK
  appends one `PangoLayout` to the snapshot, and AppKit draws lines with
  `CTLineDraw`, which does not honour an `NSShadow` attribute the way AppKit's
  own text drawing does. So `core/TextShadows.h` takes the first fragment that
  asks for one, the same rule `borderStyle` follows for four sides that can only
  have one stroke. A nested `<Text>` with a different shadow draws the outer
  one. Doing it properly means drawing each run separately, which costs the
  single-layout arrangement both hosts are built on.

  **The radius is a standard deviation, which is the number to get wrong.**
  React Native's iOS half puts `textShadowRadius` into
  `NSShadow.shadowBlurRadius`, the `CALayer.shadowRadius` kind of parameter. So
  AppKit hands it to `CGContextSetShadowWithColor` unchanged, and GTK doubles it
  for `GskShadow`, which takes CSS's radius. The same conversion in the same
  direction as a `dropShadow()` filter, and both dumps print what React Native
  parsed so a cross-host diff compares that rather than either platform's own
  idea.

  **The sign is the other one.** AppKit's drawing flips the context back to Core
  Text's y-up orientation before drawing, so a positive offset has to be negated
  there or the shadow lands above the glyphs. GTK gets it for free, GSK counting
  y downwards. A pixel test on each host asserts the shadow is *below* the
  glyphs and not above them, which is what a missed flip looks like.

  Six tests in core on the resolution, a pixel test on each host -- the shadow
  past the glyph's edge, the glyph still on top of it, nothing above it, and the
  shadow gone when the colour is taken away -- and a scenario on both hosts.
  Sabotage: the node, the context shadow, the sign and both conversions each fail
  tests of their own.

  **Windows** needs a second `DrawTextLayout` under the first, offset and in the
  shadow colour, or `CLSID_D2D1Shadow` over the text layer's alpha, which is the
  same effect the box shadow entry names. Its scenario skips by name.
