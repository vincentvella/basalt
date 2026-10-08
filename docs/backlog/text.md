# Text

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (7):**

1. ~~Inline views (<Text><View/></Text>) measure as zero-sized attachments~~
2. No baseline, so alignItems: 'baseline' is wrong for text
3. numberOfLines with ellipsizeMode: 'clip' does not truncate
4. Ignored: adjustsFontSizeToFit, textBreakStrategy, hyphenation, textShadow*, te
5. One PangoLayout is rebuilt per Paragraph per mutation, including layout-only u
6. All measurement serialises on one mutex; see docs/DECISIONS.md
7. Text is not selectable and reports nothing to AT-SPI
8. The mutex covering Pango is not held while text is drawn

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
- `numberOfLines` with `ellipsizeMode: 'clip'` does not truncate. Pango only
  honours a line limit when ellipsizing, so clip needs a clip node in the widget.
- Ignored: `adjustsFontSizeToFit`, `textBreakStrategy`, hyphenation,
  `textShadow*`, `textTransform`, `fontVariant`, `fontVariationSettings`.
- One PangoLayout is rebuilt per Paragraph per mutation, including
  layout-only updates that did not change the text.
- All measurement serialises on one mutex; see `docs/DECISIONS.md`.

- **The mutex covering Pango is not held while text is drawn.** Found
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

  Not observed failing. What a draw actually touches inside the shared context,
  and whether Pango's own internal locking already covers it, is the thing to
  establish before deciding whether this wants the lock on the draw side or a
  context per thread. The honest summary is that the invariant the file states
  is not the invariant the code keeps.
- Text is not selectable and reports nothing to AT-SPI.
