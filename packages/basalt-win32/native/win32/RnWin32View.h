// The Windows view layer: a view Fabric's mounting can drive.
//
// Deliberately the same shape as `gtk/RnView.h` and `appkit/RnAppKitView.h` --
// a view owns a tag and an absolute frame, does no layout of its own, and
// places each child at the rect the shadow tree already resolved. Yoga has done
// the layout by the time a mutation arrives, and a second layout system
// underneath it is the thing to avoid, not to add.
//
// Three decisions carry the weight here, and the first is the one that makes
// this platform different from the other two.
//
// **A view is not a window.** On GTK a view is a GtkWidget and on macOS an
// NSView; the obvious Windows translation is a child HWND per view, and it is
// wrong. An HWND is a kernel object with a message queue association, USER32
// caps a process at ten thousand of them by default, and a list of a few
// hundred rows would spend the budget. Worse, none of what the analogy promises
// arrives: an HWND clips rectangularly and cannot be rotated, so `transform`
// and rounded `overflow: hidden` would both have to be reimplemented anyway.
// So the host owns one HWND for the surface, and a view is a plain C++ object
// painted by the recursive walk in `paint`. That is closer to GTK's snapshot
// than to AppKit's layer tree, and it means this file owes the platform
// nothing but Direct2D. The one place a real window is still the right answer
// is `<TextInput>`, where an EDIT peer brings input methods and every Windows
// key binding with it -- the same bargain GTK's GtkText and AppKit's
// NSTextField make.
//
// **No flip.** Win32 and Direct2D both put the origin at the top left, which is
// where React Native puts it. The AppKit side needs `isFlipped` and a test that
// states the coordinate in numbers; here the coordinate systems already agree,
// and the only reason to mention it is that a reader coming from that file will
// go looking for the flip.
//
// **Painting is immediate, not retained.** DirectComposition would give a
// visual per view, which is the closer analogue of CALayer, and it would put
// transform and opacity in the compositor. It would also make the offscreen
// snapshot -- the thing that proves this layer paints correctly at all -- a
// second, separate path. One Direct2D walk renders identically to a window and
// to a WIC bitmap, so the picture the tests compare is made by the same code
// that draws the app.

#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// For RnImageFit and RnAccessibleInfo, both of which a view stores by value.
// Nothing heavy: each forward-declares its own Windows types.
#include "RnWin32Accessible.h"
#include "RnWin32Image.h"

// Direct2D's interfaces are structs, so the paint entry point can be declared
// without dragging <d2d1.h> -- and windows.h behind it -- into every
// translation unit that only wants to build a tree. The tests do exactly that.
struct ID2D1RenderTarget;
struct ID2D1BitmapRenderTarget;

namespace basalt::win32 {

class RnWin32TextLayout;

struct RnRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// Content this view layer cannot draw itself, drawn by whoever can.
//
// One implementor today: the Skia surface behind a `<Canvas>`. It exists
// because of the split this directory is built around -- `basalt_win32_view`
// knows Direct2D and nothing else, and Skia lives a layer up in
// `basalt_win32_mounting`, which is the target that may or may not have been
// built with it. A `sk_sp<SkSurface>` member here would drag Skia's headers
// into the demo, the layout tool and every test that builds a view tree, and
// would make a host without Skia fail to compile rather than fail to draw.
//
// So the view holds an interface and calls it in the paint walk, at the point
// where it would draw an image. What that buys beyond the include: a canvas is
// clipped, transformed, faded and scrolled by exactly the code that does those
// things to everything else, and it appears in the offscreen snapshot the
// tests compare, because the snapshot is the same walk.
class RnWin32Painter {
 public:
  virtual ~RnWin32Painter() = default;

  // Draw into a box `boxWidth` by `boxHeight` at the target's current origin
  // -- the same contract as RnWin32Image::draw, and the same units, which are
  // the view's own points.
  virtual void draw(ID2D1RenderTarget *target, float boxWidth, float boxHeight) = 0;
};

class RnWin32View {
 public:
  explicit RnWin32View(int32_t tag);
  ~RnWin32View();

  RnWin32View(const RnWin32View &) = delete;
  RnWin32View &operator=(const RnWin32View &) = delete;

  // Fabric's tag for this view. Set once, at creation.
  int32_t tag() const { return tag_; }

  // --- Geometry ------------------------------------------------------------

  // The frame the shadow tree resolved, in the parent's coordinates, top-left
  // origin. Applied directly: nothing here recomputes it.
  void setFrame(float x, float y, float width, float height);
  const RnRect &frame() const { return frame_; }

  // Shifts this view's children by (-x, -y), which is how a ScrollView scrolls.
  //
  // An offset applied while painting rather than by moving every child: the
  // frames the mounting manager wrote stay exactly the ones Yoga produced, and
  // hit testing subtracts the same offset, so the two cannot disagree. Both
  // other platforms arrive at the same arrangement by their own route.
  void setScrollOffset(float x, float y);
  float scrollX() const { return scrollX_; }
  float scrollY() const { return scrollY_; }

  // The overlay scrollbars, as core/ScrollIndicator.h decides them: an offset
  // along the track and a length, per axis, with a length of zero meaning "do
  // not draw one". Painted above the children and below everything that is not
  // the app's own -- see `paintScrollIndicators`.
  void setScrollIndicators(float verticalOffset,
                           float verticalLength,
                           float horizontalOffset,
                           float horizontalLength);

  // --- Appearance ----------------------------------------------------------

  // Components are premultiplied-free 0..1, as React Native's colour components
  // arrive. Passing hasColor=false means "no background", which is not the same
  // as transparent black: a view with no background does not paint at all.
  void setBackgroundColor(float red, float green, float blue, float alpha, bool hasColor);
  bool hasBackgroundColor() const { return hasBackgroundColor_; }

  void setOpacity(float opacity);
  float opacity() const { return opacity_; }

  // React Native's transform, as sixteen floats in CSS `matrix3d` order. Null
  // clears it.
  //
  // Only the 2D affine part is drawn -- Direct2D has no perspective and neither
  // does the GTK side today -- and the anchor is the view's centre, which is
  // what React Native means by an untouched `transformOrigin`.
  void setTransform(const float *matrix16);
  bool hasTransform() const { return hasTransform_; }

  // `overflow: hidden`. The background is clipped to the corner radius whether
  // or not this is set; this is only about the children.
  void setClipsChildren(bool clips);
  bool clipsChildren() const { return clipsChildren_; }

  // One circular radius on all four corners: what `borderRadius` alone asks
  // for, and what the paint tests use.
  void setCornerRadius(float radius);
  // Per corner, each with its own horizontal and vertical radius -- what React
  // Native resolves every radius prop to -- in the order describeTree prints
  // them: top-left, top-right, bottom-right, bottom-left, horizontal then
  // vertical. This host drew one circular radius until it took eight.
  void setCornerRadii(const float radii[8]);
  const float *cornerRadii() const { return cornerRadii_; }

  // Per edge, in CSS's order -- top, right, bottom, left -- with `colours` as
  // four RGBA quads in the same order. Drawn inside the view's rounded box and
  // over its content, as GTK and AppKit draw them. An edge with no width or no
  // alpha draws nothing, and a view with no such edge has no border at all:
  // which is also exactly when describeTree prints `borderw=` and `borderc=`,
  // decided the way GTK decides it.
  void setBorders(const float widths[4], const float colours[16]);

  // One `dropShadow()` out of a `filter` list: a shadow of this subtree's
  // *alpha* rather than of its box, which is the whole difference from
  // `boxShadow`. The standard deviation React Native parsed, which is also what
  // `CLSID_D2D1Shadow` takes, so nothing converts.
  struct FilterShadow {
    float dx = 0.0f;
    float dy = 0.0f;
    float standardDeviation = 0.0f;
    float colour[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  };

  // `filter`, resolved by `core/Filters.h` into one colour matrix, one blur, one
  // opacity and the drop shadows in the order they were written. The matrix is
  // row by row with `out = matrix * in + offset`, which is core's shape; this
  // host transposes it for Direct2D, whose matrix multiplies the other way
  // round.
  struct Filters {
    bool hasMatrix = false;
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float offset[4] = {0, 0, 0, 0};
    float blurRadius = 0.0f;
    float opacity = 1.0f;
    std::vector<FilterShadow> shadows;

    bool empty() const {
      return !hasMatrix && blurRadius <= 0.0f && opacity >= 1.0f && shadows.empty();
    }
    // Everything but the opacity, which is folded into the view's own rather
    // than being an effect of its own.
    bool needsEffects() const {
      return hasMatrix || blurRadius > 0.0f || !shadows.empty();
    }
  };

  void setFilters(const Filters &filters);
  const Filters &filters() const { return filters_; }

  // `mixBlendMode`, as CSS's keyword rather than as a Direct2D enum.
  //
  // The keyword is what crosses the seam on all three hosts, for the reason
  // `core/BlendModes.h` gives: React Native's list is CSS's and so is every
  // compositor's, so one place says what a value is called and each view layer
  // answers for its own. Empty, or `normal`, is no blending.
  //
  // A keyword this host cannot blend is still stored and still reported --
  // `describeTree` says what the app asked for -- and the view paints
  // unblended, which is the rule GTK follows for `plus-lighter`. Direct2D has
  // all seventeen, so nothing takes that path today; it is still the behaviour
  // rather than a crash if a future keyword arrives.
  void setBlendMode(const char *name);
  const std::string &blendMode() const { return blendMode_; }
  // Whether this view blends, which is a question its *parent* asks: a blend
  // needs the backdrop, and only the parent can see what is beneath a child.
  bool blends() const { return blends_; }

  // How a border or an outline is drawn: filled, or stroked with a dash
  // pattern. One enum for both because it is one concept and the three values
  // are React Native's own for each of them.
  enum class LineStyle { Solid, Dotted, Dashed };

  // `borderStyle`, which is a property of the whole border rather than of a
  // side: a dashed border is one stroked path around the rounded box, and a
  // stroked path carries one dash pattern. React Native has a slot per side and
  // the first that asks for something other than solid decides the border,
  // which is what both other hosts do.
  void setBorderStyle(LineStyle style);
  LineStyle borderStyle() const { return borderStyle_; }

  // CSS's `outline`: one ring outside the box, one width, one colour, one
  // offset and one style for the whole thing, where a border has four of each.
  // It takes no layout space, so nothing about it touches the frame, and
  // nothing clips it -- see paintOutline.
  void setOutline(float width, float offset, const float colour[4], LineStyle style);
  bool hasBorders() const { return hasBorders_; }

  // zIndex reorders painting and never the child list: Fabric's Insert and
  // Remove carry an index into that list, so it has to stay in mutation order.
  void setZIndex(int zIndex);
  int zIndex() const { return zIndex_; }

  // `pointerEvents`, which decides what a press can land on rather than what is
  // drawn. CSS's four values, and React Native's. Read by `hitTest` and by
  // nothing else.
  //
  // On the view rather than in the mounting manager because hit testing is a
  // pure function of the view tree -- `hitTest` takes no React Native types and
  // has nowhere to look a prop up. The AppKit host arranges it the same way.
  enum class PointerEvents { Auto, None, BoxNone, BoxOnly };
  void setPointerEvents(PointerEvents mode) { pointerEvents_ = mode; }
  PointerEvents pointerEvents() const { return pointerEvents_; }

  // `hitSlop`, which grows the box a press can land on without moving a pixel:
  // a 24pt target can answer for 56pt, which is how a small control is made
  // reachable. Top, right, bottom, left, the order CSS names them and the one
  // the other two hosts store.
  //
  // Read by `hitTest` and by nothing else, and on the view for the reason
  // `pointerEvents` is: hit testing is a pure function of the view tree and has
  // nowhere to look a prop up.
  void setHitSlop(const float insets[4]);
  bool hasHitSlop() const { return hasHitSlop_; }
  const float *hitSlop() const { return hitSlop_; }

  // Whether this view takes keyboard focus, and therefore whether Tab stops on
  // it.
  //
  // React Native has a `focusable` prop and it does not reach this platform:
  // ReactCommon parses it only into Android's and tvOS's HostPlatformViewProps,
  // and the C++ host's is a bare alias of BaseViewProps. What does reach here
  // is `accessible`, which is what <Pressable> sets on everything it renders.
  // See Win32Focus.h.
  void setFocusable(bool focusable) { focusable_ = focusable; }
  bool focusable() const { return focusable_; }

  // Whether to paint a focus ring. A view is not its own window here, so there
  // is no Win32 focus to ask about -- Win32FocusManager owns the answer and
  // sets it, exactly as it owns the Tab order.
  void setShowsFocusRing(bool shows) { showsFocusRing_ = shows; }
  bool showsFocusRing() const { return showsFocusRing_; }

  // `display: none`. A hidden view is neither painted nor hit, and neither are
  // its children. Distinct from `opacity: 0`, which paints nothing and is still
  // there to be pressed.
  void setHidden(bool hidden);
  bool hidden() const { return hidden_; }

  // `backfaceVisibility: 'hidden'`. A view whose transform has turned it away
  // from the viewer is not painted, and nor are its children -- which is what
  // a card's back should do, and what takes it out of hit testing too, since
  // `hitTest` skips a hidden view.
  //
  // Whether it has turned away is `basalt::facesAway`; see core/Backface.h for
  // why the rule is shared rather than left to each toolkit.
  void setHidesBackFace(bool hides);

  // The EDIT control a <TextInput> mounted behind this view, or null.
  //
  // The view neither owns it nor draws it -- `Win32TextInputManager` does both
  // -- and the only thing it is for is `describeTree`: a text field's content
  // lives in its peer rather than in a layout, so without this it would be
  // invisible to every test that reads the tree, and the Windows dump would
  // differ from the other two hosts' for a field that was working perfectly.
  // GTK keeps the same back pointer to its GtkText for the same reason.
  void setEditablePeer(HWND control) { editablePeer_ = control; }
  HWND editablePeer() const { return editablePeer_; }

  // The view's `nativeID`. Kept for the one thing on this platform that reads
  // it: a hidden title bar asks which views are drag regions, and
  // <TitleBar.DragRegion> says so through this prop because it is the one a
  // plain View already carries all the way to the host. See
  // Win32TitleBarLayout.h.
  void setNativeId(std::string nativeId) { nativeId_ = std::move(nativeId); }
  const std::string &nativeId() const { return nativeId_; }

  // --- Content ---------------------------------------------------------------

  // The paragraph this view draws, or null for a view that draws none.
  //
  // Text sits above the background and below any children, which is the order
  // `<Text>` with nested views expects and the order the GTK snapshot uses.
  // Held as a shared_ptr because a paragraph is expensive to build and a
  // mutation that changed only layout must not rebuild one -- the mounting
  // manager will hand the same object back.
  void setTextLayout(std::shared_ptr<RnWin32TextLayout> layout);
  const std::shared_ptr<RnWin32TextLayout> &textLayout() const { return textLayout_; }

  // The decoded pixels of an <Image>, and how they fill this view's frame. Pass
  // null to clear.
  //
  // Shared rather than owned for the same reason as the paragraph: a mutation
  // that changed only layout must not restart a load, or an <Image> flickers
  // whenever its parent resizes.
  void setImage(std::shared_ptr<RnWin32Image> image, RnImageFit fit);

  // `tintColor`: recolours the image keeping its alpha. Separate from the image
  // because the tint arrives from the props and the image from a loader, and
  // either can land first.
  void setImageTint(bool hasTint, const float components[4]);

  // `blurRadius`: blurs the image and not the view, which is what the prop
  // means. Separate from the image for the reason the tint is, and in the
  // view's coordinates rather than the image's pixels, which is the rule the
  // other two hosts measured. Zero or less is no blur.
  void setImageBlur(float radius);
  float imageBlur() const { return imageBlur_; }

  // The frames of an animated image, their delays as the file carried them,
  // and how many times round. Fewer than two frames clears any animation.
  //
  // The view shows frame zero and advances no further by itself: the host's
  // timer calls `advanceImageAnimations` on the tree, which is what keeps a
  // test about frames deterministic. Both other hosts are split the same way.
  //
  // Passing the same frames again is a no-op, so the re-apply on every
  // mutation does not restart the animation; the loader keeps one vector per
  // URI for exactly that.
  void setImageFrames(std::vector<std::shared_ptr<RnWin32Image>> frames,
                      std::vector<unsigned> delaysMs,
                      unsigned loopCount);
  // Moves this view's animation on and shows the frame that lands on,
  // answering with how long until the next one, or zero when nothing more is
  // due.
  double advanceImageAnimation(double milliseconds);
  // The same for this view and everything inside it, which is what the host
  // calls. Answers true while anything is still animating.
  bool advanceImageAnimations(double milliseconds);
  bool hasAnimatedImage() const;
  const std::shared_ptr<RnWin32Image> &image() const { return image_; }
  RnImageFit imageFit() const { return imageFit_; }

  // The painter for this view, or null. Drawn just above where an image would
  // be: a `<Canvas>` carries neither an image nor a paragraph, so the two
  // never meet and the order is a decision rather than a conflict.
  //
  // Shared rather than owned because the thing on the other end outlives a
  // single mutation -- it holds the surface the last frame was rendered into,
  // and rebuilding it per transaction would throw that frame away.
  void setPainter(std::shared_ptr<RnWin32Painter> painter);
  const std::shared_ptr<RnWin32Painter> &painter() const { return painter_; }

  // --- Controls ----------------------------------------------------------------
  //
  // <ActivityIndicator>, <RefreshControl> and <Switch>, which the other two
  // hosts mount a real toolkit widget for and this one draws.
  //
  // Not an oversight and not laziness: Windows has no spinner control at all
  // (the closest is a marquee progress bar, which is a moving stripe), and no
  // switch either -- the Win32 approximation is a checkbox, and WinUI's toggle
  // is not reachable from a plain HWND app. Both would also be child windows,
  // which is the one thing this view layer is built to avoid; see the note at
  // the top about USER32's handle budget. So they are painted, in the same
  // Direct2D walk as everything else, which is also what makes them appear in
  // the offscreen snapshot the tests compare.
  enum class Control { None, Spinner, Switch };

  // Everything the drawing needs, which is core/DesktopControls.h's
  // ControlState with the parts that are not pixels left out. A plain struct
  // rather than that type because this target does not link React Native --
  // the demo and half the tests build a view tree with no Fabric anywhere.
  struct ControlStyle {
    bool on = false;
    bool disabled = false;
    bool large = false;
    bool hidesWhenStopped = true;
    bool hasThumb = false;
    float thumb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool hasTrackOn = false;
    float trackOn[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool hasTrackOff = false;
    float trackOff[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  };

  // `description` is what describeTree prints, written by
  // core/DesktopControls.h so that three hosts cannot describe the same switch
  // differently.
  void setControl(Control kind, const ControlStyle &style, std::string description);
  Control control() const { return control_; }
  const ControlStyle &controlStyle() const { return controlStyle_; }

  // Whether anything in this subtree is a spinner that is turning, and so
  // whether the window has to keep repainting. The host asks the surface root
  // once a frame; see main_win32.cpp.
  bool hasAnimatingSpinner() const;

  // --- React DevTools' overlay -------------------------------------------------
  //
  // The rectangles a `DebuggingOverlay` draws: an inspected element's blue box,
  // or an outline around everything that just re-rendered. Set from
  // Win32MountingManager, which parses them; see core/DebuggingOverlay.h.
  struct Highlight {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool filled = false;
  };

  void setHighlights(std::vector<Highlight> highlights);
  const std::vector<Highlight> &highlights() const { return highlights_; }

  // --- Accessibility ---------------------------------------------------------

  // What a screen reader is told. Unlike GTK, where the role is a
  // construct-only property and so cannot change after mount, every field here
  // is free to change at any time -- UI Automation pulls rather than being
  // pushed to, so the provider simply reads the current value. See
  // RnWin32Accessible.h.
  void setAccessibleInfo(const RnAccessibleInfo &info);
  const RnAccessibleInfo &accessibleInfo() const { return accessible_; }

  // A UIA provider over this view, or null when the view is not an
  // accessibility element. The caller owns a reference and must Release it.
  IRawElementProviderSimple *createAccessibleProvider() const;

  // --- Tree ----------------------------------------------------------------
  //
  // A view does not own its children. The mounting registry owns every view,
  // which is what makes Fabric's "a Remove detaches but must not destroy" rule
  // structural here rather than a reference count that has to be got right: a
  // detached view is simply a view with no parent, still in the registry, ready
  // for the Insert that reparents it. Callers with no registry -- the demo, the
  // tests -- hold their views in a vector of unique_ptr.

  void insertChild(RnWin32View *child, int index);
  void removeChild(RnWin32View *child);
  const std::vector<RnWin32View *> &children() const { return children_; }
  RnWin32View *parent() const { return parent_; }

  // The children in the order they are painted: mutation order, restacked by
  // zIndex where any child has one. Painting walks this forwards and hit
  // testing walks it backwards, and they call the same function so that the
  // view a click lands on is always the view drawn on top.
  //
  // Returns a copy. Painting used to avoid it in the common case; one
  // definition of paint order is worth more than the allocation.
  std::vector<RnWin32View *> childrenInPaintOrder() const;

  // --- Geometry, resolved ----------------------------------------------------

  // This view's local-to-parent transform, as the six numbers of a 2D affine
  // matrix in Direct2D's Matrix3x2F order: the frame's translation, with any
  // `transform` composed in about the view's centre.
  //
  // Public, and the only place that composition is written. `paint` builds its
  // Direct2D matrix from these and `hitTest` inverts them, so the two cannot
  // disagree about where a view is -- which would show up as a rotated button
  // that is clickable where it used to be, and is exactly the bug
  // `docs/DECISIONS.md` records GTK hitting.
  void localToParent(float out[6]) const;

  // A point in `root`'s coordinates, expressed in this view's own.
  //
  // Walks up composing `localToParent` and inverts the chain, which is the
  // same matrix `hitTest` inverts one level at a time on the way down -- so a
  // press and the coordinates it reports cannot disagree about where a view
  // is. Answers false when the chain has no inverse, which `scale: 0` produces
  // and which has no sensible point in it.
  bool pageToLocal(const RnWin32View *root, float pageX, float pageY,
                   float &outX, float &outY) const;

  // --- Painting ------------------------------------------------------------

  // Paints this view and its subtree into `target`, which must be between
  // BeginDraw and EndDraw. The target's transform on entry is the one this
  // view's frame is relative to, and it is restored before returning.
  void paint(ID2D1RenderTarget *target) const;

  // --- Reporting -----------------------------------------------------------

  // One line per view, two spaces of indent per level.
  //
  // The format is a cross-platform contract, not a debug convenience:
  // `scripts/compare_hosts.sh` diffs two hosts' dumps line by line, so the
  // fields and their order match `rn_view_describe_into` in gtk/RnView.cpp and
  // `describeInto:depth:` in appkit/RnAppKitView.mm exactly. A field added in
  // the wrong place makes every line differ.
  std::string describeTree() const;

 private:
  // Paints the children into whatever space the target's transform is already
  // in, which is this view's own. Separate from `paint` so the clip and the
  // scroll offset have an obvious scope, and so no Direct2D type appears above.
  void paintChildren(ID2D1RenderTarget *target,
                     ID2D1BitmapRenderTarget *blendLayer = nullptr) const;
  // The spinner and the switch, drawn into this view's own coordinates.
  void paintControl(ID2D1RenderTarget *target) const;
  // React DevTools' overlay, over everything including the children.
  void paintHighlights(ID2D1RenderTarget *target) const;
  // The overlay scrollbars, in this view's own coordinates.
  void paintScrollIndicators(ID2D1RenderTarget *target) const;
  // Recomputes whether the back face is showing, and hides or shows the view.
  void updateBackFace();
  void applyVisibility();
  void paintBorders(ID2D1RenderTarget *target) const;
  void paintStrokedBorder(ID2D1RenderTarget *target) const;
  void paintOutline(ID2D1RenderTarget *target) const;
  // This view and everything inside it, with the target's transform already
  // placing its origin at zero. Split out of `paint` so that a filtered view
  // can draw the same thing into an offscreen bitmap and run an effect graph
  // over it.
  //
  // `blendLayer` is the offscreen `target` draws into when this view's whole
  // subtree goes through one, and null when it draws straight onto the window.
  // Only a blended child needs it: `mixBlendMode` has to read what is beneath
  // it, and what is beneath it is that bitmap.
  void paintContents(ID2D1RenderTarget *target,
                     ID2D1BitmapRenderTarget *blendLayer = nullptr) const;
  void paintFiltered(ID2D1RenderTarget *target) const;
  // This view's subtree through an offscreen, so that a blended child has a
  // backdrop to read. The view's opacity is applied to that bitmap rather than
  // inside it, which is both cheaper than a layer and necessary: a pushed layer
  // holds what is drawn in an intermediate surface the backdrop cannot see.
  void paintBlendLayer(ID2D1RenderTarget *target) const;
  // Blends one child into the layer, in place of painting it onto the target.
  void blendChildIntoLayer(ID2D1BitmapRenderTarget *layer, const RnWin32View *child) const;
  bool hasBlendedChild() const;
  void describeInto(std::string &out, int depth) const;

  int32_t tag_;
  RnRect frame_;
  RnWin32View *parent_ = nullptr;
  std::vector<RnWin32View *> children_;

  bool hasBackgroundColor_ = false;
  float backgroundColor_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float opacity_ = 1.0f;
  bool clipsChildren_ = false;
  // See setCornerRadii and setBorders for the orders these are kept in.
  float cornerRadii_[8] = {};
  bool hasCornerRadii_ = false;
  float borderWidths_[4] = {};
  float borderColours_[16] = {};
  bool hasBorders_ = false;
  HWND editablePeer_ = nullptr;
  std::string nativeId_;
  int zIndex_ = 0;
  PointerEvents pointerEvents_ = PointerEvents::Auto;
  float hitSlop_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  bool hasHitSlop_ = false;
  float outlineWidth_ = 0.0f;
  float outlineOffset_ = 0.0f;
  float outlineColour_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  LineStyle outlineStyle_ = LineStyle::Solid;
  LineStyle borderStyle_ = LineStyle::Solid;
  bool focusable_ = false;
  bool showsFocusRing_ = false;
  float scrollX_ = 0.0f;
  float scrollY_ = 0.0f;
  float indicatorVerticalOffset_ = 0.0f;
  float indicatorVerticalLength_ = 0.0f;
  float indicatorHorizontalOffset_ = 0.0f;
  float indicatorHorizontalLength_ = 0.0f;

  bool hidden_ = false;
  bool hidesBackFace_ = false;
  // Set by the back-face rule rather than by the app, so that clearing the
  // rule does not un-hide a view the app itself hid.
  bool hiddenByBackFace_ = false;
  bool hiddenByApp_ = false;
  std::vector<Highlight> highlights_;
  Control control_ = Control::None;
  ControlStyle controlStyle_;
  std::string controlDescription_;
  std::shared_ptr<RnWin32TextLayout> textLayout_;
  std::shared_ptr<RnWin32Image> image_;
  std::shared_ptr<RnWin32Painter> painter_;
  RnImageFit imageFit_ = RnImageFit::Cover;
  bool hasImageTint_ = false;
  float imageTint_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float imageBlur_ = 0.0f;
  // An animated image. `image_` is whichever frame is showing, so everything
  // that draws an image stays unchanged.
  std::vector<std::shared_ptr<RnWin32Image>> imageFrames_;
  std::vector<unsigned> imageDelays_;
  unsigned imageLoopCount_ = 0;
  size_t imageFrame_ = 0;
  double imageElapsedMs_ = 0.0;
  Filters filters_;
  // `mixBlendMode`: the keyword the app asked for, and whether this host can
  // blend it. Both, because the dump reports the first and the paint path asks
  // the second.
  std::string blendMode_;
  bool blends_ = false;
  RnAccessibleInfo accessible_;
  bool hasTransform_ = false;
  // The 2D affine part, in the order Direct2D's Matrix3x2F stores it:
  // _11, _12, _21, _22, _31, _32 -- which is also the order CSS writes a
  // matrix() and the six numbers describeTree prints.
  float transform_[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
};

// The deepest view at a point, in `root`'s own coordinates, or null for a miss.
//
// A free function because it is a pure function of the view tree and nothing
// else, which is also what makes it testable: hit testing is the part of input
// most likely to be quietly wrong, and it needs no mouse to exercise. It lives
// here rather than with a touch dispatcher so that testing it needs no React
// Native, which is the arrangement the AppKit side settled on too.
//
// Windows differs from both other platforms in having to do this at all. GTK
// gets picking from `gtk_widget_pick` and macOS from AppKit's own hit testing,
// so on those two the transform has to be pushed *into* the toolkit's geometry
// or clicks land in the wrong place. Here there is no toolkit geometry to push
// it into, so this inverts each view's matrix on the way down -- which means a
// rotated view is clickable where it is drawn, and it is `localToParent` that
// guarantees "where it is drawn" means the same thing to both.
RnWin32View *hitTest(RnWin32View *root, float x, float y);

} // namespace basalt::win32
