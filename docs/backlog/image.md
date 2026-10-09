# Image

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (5):**

1. overlayColor, fadeDuration and progressiveRenderingEnabled are ignored; blurRadius is done
2. Assets are never fetched over the network, so a dev server's assets do not wor
3. Nothing caches a downloaded asset, which is right for a local file and will no
4. onProgress and onPartialLoad are never emitted
5. ~~`ImageProps` and `<Image>`'s own style names have no rows on the support page~~
6. `defaultSource` and `loadingIndicatorSource` draw no placeholder

- ~~Nothing evicts the texture cache.~~ Done on all three, in
  `core/ImageCache.h`. Each host kept decoded images in an `unordered_map`
  nothing removed from, so a long list of remote images, scrolled, held every
  one ever seen until the process exited.

  Least recently *used*, not least recently added, which is the distinction the
  case needs: scrolling a list back up should not re-decode the rows about to
  be on screen again. A hit counts as a use, so it moves an entry to the front.

  The policy is in core and the pixels are not, because the three hosts hold a
  `GdkTexture`, a `CGImage` and an `RnWin32Image` and none of those can be
  named there. Core answers with the URIs to release and never sees one. Each
  host measures its own bytes: GDK four per pixel, Core Graphics the row
  stride times the height, since rows are padded and width times four would
  undercount.

  An image larger than the whole budget is kept rather than refused: it is on
  screen, and evicting it would decode it again on the next frame, forever.
- ~~`resizeMode: 'repeat'` falls back to `center`.~~ Done on all three, and it
  turned out each toolkit has a primitive for it rather than needing one
  built: `gtk_snapshot_push_repeat`, `CGContextDrawTiledImage`, and a Direct2D
  bitmap brush in wrap mode.

  Tiled from the top left, so whole tiles start at the origin and the partial
  one is at the far edge, which is what CSS `repeat` does, and why the
  centring it used to fall back to was a different picture rather than a
  rougher one.

  Windows uses nearest-neighbour sampling for the brush deliberately: the
  default is linear, which blends the last column of one tile into the first
  of the next and leaves a seam on every boundary, visible on exactly the
  small sharp images people tile.

  `fit=repeat` is in the tree dump, so `compare_hosts.sh` sees it, and
  `test_appkit_image.mm` asserts the ink reaches all four corners, which it
  does not when centred, the check having been run against `center` first to
  be sure it discriminates.
- ~~**`tintColor` is ignored.**~~ Done on all three, and on expo-image's
  `tintColor` too. The image becomes a stencil and the colour is what is drawn,
  which is what the prop means: recolour the silhouette rather than blend with
  the pixels. Each toolkit spells that differently: a GskMaskNode in alpha
  mode on GTK, `CGContextClipToMask` on AppKit, `FillOpacityMask` on Direct2D,
  which needs aliased antialiasing and refuses the call without it.

  The tree dump reports `tint=#rrggbbaa`, in one format on all three, which is
  what makes a paint property assertable on hosts that have no rendering
  assertions: cross-host parity now compares the tint the way it compares the
  fit.

- `overlayColor`, `fadeDuration` and `progressiveRenderingEnabled` are ignored.
  **`blurRadius` is done on GTK and AppKit**, 2026-10-07 and 2026-10-08.

  GSK has a blur node, so the prop is a `gtk_snapshot_push_blur` around the
  image, inside the clip and the tiling: a blurred `cover` image is still cut to
  its box and a blurred `repeat` blurs each tile alike. Around the tint as well,
  so the blur applies to what is drawn rather than to the silhouette it is
  masked from. The image alone and not the view, which is what the prop means,
  since a blurred photograph behind sharp text is the usual reason to ask.

  Worth noting for the three still open: a blur node made this *directly
  observable*, so the tests walk the render tree and count blur nodes rather than
  asserting a radius was stored. A negative radius is no blur rather than a
  crash, which nothing stops an app sending and GSK would otherwise take.

  AppKit, the next day, and the entry was wrong about why it would be hard. It
  said the image path is a layer, so the change would be `layer.filters` with a
  CIFilter and would not be reachable from the tests. The image is not a layer:
  it is drawn in `drawRect:` with Core Graphics, which has no blur of its own, so
  the blur is a Core Image round trip that produces a blurred CGImage to draw in
  place of the original. The tests reach it the way the other image tests do, by
  drawing into a bitmap and measuring the ink.

  Three things had to be decided, and each was measured rather than chosen:

  **Which space to blur in.** The source's pixels or the view's coordinates, and
  they are the same only for an image drawn at its natural size. Blurring in the
  source's would make one prop almost invisible on a photograph and overwhelming
  on an icon, and would not agree with GSK, which blurs in widget coordinates. So
  the image is scaled to its destination first. A test stretches a 40-pixel image
  to 200 and measures the ramp: in the wrong space it is magnified along with the
  picture and comes out five times too wide, which is what the sabotage check
  reported.

  **How a radius becomes a sigma.** `gtk_snapshot_push_blur` documents neither,
  so it was measured: a black-to-white edge rendered through a blur node and
  downloaded as a texture comes out at sigma = 0.47 * radius under the GL
  renderer, and 0.55 under the cairo one, which approximates with three boxes.
  React Native's own iOS path convolves three boxes of
  `floor((radius * scale * 3 * sqrt(2 * pi) / 4 + 0.5) / 2) | 1`, whose variance
  works out to the same 0.47. Half the radius is what both agree on.

  **Which colour space.** Core Image converts to linear light and blurs there by
  default; GSK blurs the encoded pixels and iOS runs `vImageBoxConvolve` over
  8-bit sRGB bytes. Measured on the same edge, the managed blur spreads about 1.2
  times as far, so colour management is off for this one filter. That was the
  difference between a plausible blur and the same blur as the other two.

  **And the bug the GTK half shipped with**, found by writing this one: the
  radius was assigned inside the `if` that read `tintColor`, so a blurred image
  that was not also tinted came out sharp. Every unit test passed, because each
  one pushes the blur onto the widget by hand and none of them went through the
  props. Both hosts now print `blur=` in the tree dump, there is a unit test per
  host that the dump says it, and an end-to-end scenario mounts a blurred image
  with no tint and asserts on it. The two hosts' image lines are byte-identical.
- ~~A `require()`d image drew nothing.~~ It laid out at the right size and had
  no pixels, and the reason was neither the loader nor the mounting manager:
  Metro's `build` command has no `--assets-dest`, so the files were never copied
  next to the bundle. `scripts/copy_assets.js` reads the asset descriptors back
  out of the bundle Metro just wrote and copies each one to where
  `AssetSourceResolver.scaledAssetURLNearBundle` will look for it, including
  that rule's own escaping, where each `../` becomes a single `_`. The demo's
  images never showed this because they are `{uri: ...}` rather than requires.
- Assets are never fetched over the network, so a dev server's assets do not
  work; `downloadAsync` rejects saying so. The host has an http client already.
  And nothing is cached: `downloadAsync` returns the file where it lies, which
  is right for a local asset and will not be right for a remote one.
- Nothing caches a downloaded asset, which is right for a local file and will
  not be for a remote one.
- `onProgress` and `onPartialLoad` are never emitted.
- ~~`IImageLoader` itself is still unimplemented, so `Image.getSize` and
  `Image.prefetch` do nothing.~~ Done on all three. Each host's image loader
  now *is* an `IImageLoader`, so a size asked for something already on screen
  is answered from the same cache that is holding its pixels.

  The reason it was unimplemented is upstream and worth knowing before anyone
  looks for the seam: `ReactCxxTurboModuleProvider` constructs
  `ImageLoaderModule(jsInvoker_)` with the default empty `weak_ptr`, and
  nothing in `ReactInstanceConfig` can supply one. So there is no hook to fill
  in; the module has to be built by the host instead, which works because a
  host's own providers are consulted before the built-in ones. See
  `docs/backlog/upstream.md`.

- ~~**`ImageProps` and `<Image>`'s own style names have no rows on the support
  page.**~~ Done 2026-10-09, with the same machinery the layout props got the
  day before: `ImageProps` is scraped like the other structs, and
  `____ImageStyle_InternalCore` joined the style types, so `resizeMode`,
  `objectFit`, `tintColor` and `overlayColor` are accounted for rather than
  silently outside the page's claim. `objectFit` is listed as another spelling
  of `resizeMode`, which is what `Image.js` converts it into before ReactCommon
  sees anything.

  `Libraries/Image/ImageProps.js` had to join the prop-type files too, or every
  field but two read as an internal: the page would have said that nothing an
  app writes reaches `blurRadius`.

  **Two things the rows found.** The hand-written Image section they replace
  still said no host decodes a second frame, which stopped being true earlier
  the same day; and six fields had never been looked at, of which four are
  Android's (`resizeMethod`, `resizeMultiplier`, with `overlayColor` and
  `fadeDuration` already in entry 1), one is iOS's (`capInsets`), and two are
  entry 6 below.

  What the entry said when it was open, which is still the shape of the
  `<TextInput>` half: The page scrapes React Native's prop structs, and as of 2026-10-09 it
  scrapes four of them plus Yoga's: `BaseViewProps`, `AccessibilityProps`,
  `TextAttributes`, `ParagraphAttributes`. `ImageProps` is not among them, so
  thirteen props an app writes on an `<Image>` are answered for by a
  hand-written "Image" section of five rows instead, which is the arrangement
  the scraped tables replaced everywhere else.

  Two halves, and neither is hard.

  `ImageProps` has a `#pragma mark - Props` region of the same shape as the four
  already read, so adding it to `STRUCTS` in `scripts/scrape_props.py` is one
  entry. What it needs is a judgement per field, and some of them are not
  obvious: `sources` and `resizeMode` are done on all three, `blurRadius` on two,
  `overlayColor`, `fadeDuration` and `progressiveRenderingEnabled` are entry 1
  here, and `defaultSource`, `loadingIndicatorSource`, `capInsets`,
  `resizeMethod`, `resizeMultiplier` and `internal_analyticTag` have never been
  looked at. Reading what each host does rather than assuming is the work.

  The other half is the style names. `____ImageStyle_InternalCore` adds
  `resizeMode`, `objectFit`, `tintColor` and `overlayColor` to what an
  `<Image style={{...}}>` takes, and `STYLE_TYPES` reads five types that do not
  include it. Adding it makes the scrape report those four as unaccounted for,
  which is the check working: they are accounted for by `ImageProps`, so the two
  halves want doing together.

  Then the hand-written section can go, and the page's claim can stop having an
  exception in it.

- **`defaultSource` and `loadingIndicatorSource` draw no placeholder.** Both are
  read by nobody on any host, which the support page says as of 2026-10-09 and
  nothing said before: an `<Image>` whose source is still loading draws its
  background and nothing else.

  The work is small and the decision is not. `defaultSource` is an image to
  draw until the real one arrives, which means the loader has to answer twice
  for one view: once with the placeholder, synchronously if it is a bundled
  asset, and again when the fetch lands. Each host's `applyImage` already has
  the shape of that, since it re-requests a cached URI on every mutation, so
  what is missing is a second URI per view and the rule for when to stop using
  it. `loadingIndicatorSource` is the Android spelling of the same idea with a
  spinner rather than an image, and a desktop has no spinner to draw unless one
  is drawn by hand; `core/ScrollIndicator.h` is the precedent for that if it is
  ever wanted.

  Worth doing after the network cache in entry 3: a placeholder is most visible
  on a remote image, and a cache changes how often one is seen at all.