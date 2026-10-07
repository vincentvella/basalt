# Text

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (8):**

1. Inline views (<Text><View/></Text>) measure as zero-sized attachments
2. No baseline, so alignItems: 'baseline' is wrong for text
3. numberOfLines with ellipsizeMode: 'clip' does not truncate
4. Ignored: adjustsFontSizeToFit, textBreakStrategy, hyphenation, textShadow*, te
5. One PangoLayout is rebuilt per Paragraph per mutation, including layout-only u
6. All measurement serialises on one mutex; see docs/DECISIONS.md
7. Text is not selectable and reports nothing to AT-SPI
8. The mutex covering Pango is not held while text is drawn

- Inline views (`<Text><View/></Text>`) measure as zero-sized attachments.
  Doing it properly means `PangoAttrShape` placeholders sized from the child's
  own measurement, and returning their rects from `measure`.
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
