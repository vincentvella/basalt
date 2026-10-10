// The macOS view layer: an NSView that Fabric's mounting can drive.
//
// Deliberately the same shape as the GTK one in `gtk/RnView.h` -- a view owns a
// tag and an absolute frame, does no layout of its own, and places each child
// at the rect the shadow tree already resolved. Yoga has done the layout by the
// time a mutation arrives, and a second layout system underneath it is the
// thing to avoid, not to add.
//
// Two decisions are worth stating because they are easy to get wrong and hard
// to notice afterwards.
//
// The view is **flipped**. AppKit's default origin is bottom-left; React
// Native's is top-left, and every frame Fabric produces assumes top-left. An
// unflipped view renders a correct-looking layout upside down about its own
// centre, which reads as a mysterious ordering bug rather than a coordinate
// one.
//
// The view is **layer-backed**, and props are layer properties rather than
// drawing code. Background colour, opacity, corner radius, clipping and
// transform all exist on CALayer with the semantics React Native wants, so
// mapping onto them is both less code and closer to what the platform will do
// well. The GTK side has to compose these by hand in a snapshot; here it would
// be work to avoid using them.

#pragma once

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
// For CALayer, which `boxShadow` is made of: Cocoa declares the view and
// QuartzCore the layers under it.
#import <CoreImage/CoreImage.h>
#import <QuartzCore/QuartzCore.h>

NS_ASSUME_NONNULL_BEGIN

@class RnAppKitView;

// Where a view sends the mouse.
//
// Set on the surface root only. A view that has no handler walks up to one that
// does, which is how every view in the tree feeds a single dispatcher without
// anything having to attach one per view -- the same arrangement the GTK side
// gets by putting its controllers on the root.
//
// Points arrive in the handler's own coordinates, which is the surface root's,
// which is what React Native calls the page.
//
// `button` is W3C's numbering rather than AppKit's selector-per-button, because
// that is what a pointer event carries and what decides whether the click
// presses anything at all. See core/PointerButtons.h.
@protocol RnAppKitInputHandler <NSObject>
- (void)rnMouseDownAt:(NSPoint)point button:(int)button;
- (void)rnMouseDraggedTo:(NSPoint)point;
- (void)rnMouseUpAt:(NSPoint)point button:(int)button;
// The pointer moving with no button down, and the pointer leaving the surface.
// Not part of React Native's touch model -- a finger that is not touching does
// not exist -- but it is what W3C pointer events call hover, and what
// `onPointerEnter` and friends are fed from. See core/HoverTracker.h.
- (void)rnMouseMovedTo:(NSPoint)point;
- (void)rnMouseExited;
@end

// Where a view sends a change of keyboard focus.
//
// Set on the surface root, like RnAppKitInputHandler, and found by walking up
// from whichever view gained or lost it. AppKit has no reliable notification
// for "the window's first responder changed" -- NSWindow's firstResponder is
// not KVO-observable -- so the views that can hold focus report it themselves.
@protocol RnAppKitFocusHandler <NSObject>
- (void)rnView:(RnAppKitView *)view didChangeFocus:(BOOL)focused;
// Enter or space on a focused view. Returns YES if it was handled, so a view
// with nothing listening passes the key on rather than swallowing it.
- (BOOL)rnActivateView:(RnAppKitView *)view;
// Tab and Shift-Tab. AppKit's own key-view loop cannot do this for a window
// built without a nib; see AppKitFocus.h.
- (BOOL)rnMoveFocusForward:(BOOL)forward;
// A key a view declared through `<KeyHandler>`. Returns YES if some view in the
// focus path claimed it, in which case the key is consumed; NO passes it on, and
// passing it on is what keeps the menu, a focused text field and a scrolling
// ancestor working. Asked through this protocol and not answered here because the
// registry is in core, which the view library does not link. See
// core/KeyEvents.h.
- (BOOL)rnView:(RnAppKitView *)view handlesKey:(NSEvent *)event;
@end

// Where a scrolling view sends the wheel.
//
// Set only on views that are ScrollViews. A view with none calls super, which
// walks the responder chain to an ancestor that has one -- so a wheel over a
// plain view inside a list scrolls the list, and a list inside a list scrolls
// the inner one first, which is what AppKit's own nesting does.
@protocol RnAppKitScrollHandler <NSObject>
// Returns YES if the scroll was consumed.
//
// Two phases, because a touchpad has two gestures in one stream. `phase` is
// the fingers: it begins when they land and ends when they lift. `momentum` is
// what the system keeps sending after they have gone, and it is the only
// reason React Native's `onMomentumScrollBegin` and `onMomentumScrollEnd` can
// be answered honestly on this platform -- macOS does the deceleration itself,
// so there is nothing here to model, only something to report.
- (BOOL)rnScrollView:(RnAppKitView *)view
                  by:(NSPoint)delta
             precise:(BOOL)precise
               phase:(NSEventPhase)phase
            momentum:(NSEventPhase)momentum;
@end

// The deepest view at a point, in `root`'s coordinates, or nil for a miss.
//
// A free function because it is a pure function of the view tree and nothing
// else, which is also what makes it testable: hit testing is the part of input
// most likely to be quietly wrong, and it needs no mouse to exercise. It lives
// here rather than with the dispatcher so that testing it needs no React Native.
RnAppKitView *_Nullable RnAppKitHitTest(RnAppKitView *_Nullable root, CGFloat x, CGFloat y);

@interface RnAppKitView : NSView

// Fabric's tag for this view. Set once, at creation.
@property(nonatomic, readonly) NSInteger rnTag;

+ (instancetype)viewWithTag:(NSInteger)tag;

// The frame the shadow tree resolved, in the parent's coordinates, top-left
// origin. Applied directly: nothing here recomputes it.
- (void)setRnFrameX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height;

// Components are premultiplied-free 0..1, as React Native's colour components
// arrive. Passing hasColor:NO means "no background", which is not the same as
// transparent black: a view with no background does not paint at all.
- (void)setRnBackgroundColorRed:(CGFloat)red
                          green:(CGFloat)green
                           blue:(CGFloat)blue
                          alpha:(CGFloat)alpha
                       hasColor:(BOOL)hasColor;

- (void)setRnOpacity:(CGFloat)opacity;

// React Native's transform, as sixteen floats in CSS `matrix3d` order -- which
// is CATransform3D's field order too, so the two are the same sixteen numbers
// in the same places. Null clears it.
//
// The anchor is the view's centre, which is what React Native means by an
// untouched `transformOrigin` and what a layer-backed NSView already uses.
- (void)setRnTransform:(nullable const float *)matrix;

// `backfaceVisibility: 'hidden'`. A view whose transform has turned it away
// from the viewer is not drawn at all -- nor are its children, which is what
// CALayer's own `doubleSided` does and what a card's back should do.
//
// Whether it has turned away is `basalt::facesAway`; see core/Backface.h for
// why the rule is shared rather than left to each toolkit.
- (void)setRnHidesBackFace:(BOOL)hides;

// `display: 'none'`. Separate from the back face above because the two are
// independent reasons to hide the same view, and Fabric applies them through
// different calls -- props, then layout metrics. Setting `hidden` directly
// from either one made the later call answer for both, which is why a view
// turned away from the viewer came back the moment its layout was applied.
- (void)setRnHidden:(BOOL)hidden;

// How an image fills its frame. Mirrors React Native's ImageResizeMode, minus
// Repeat, which needs a tiled draw rather than one image draw.
// Accessible states. Each is a tri-state: unset leaves AppKit's default alone,
// which is not the same as setting it false.
typedef NS_ENUM(NSInteger, RnAppKitAccessibleFlag) {
  RnAppKitAccessibleUnset,
  RnAppKitAccessibleFalse,
  RnAppKitAccessibleTrue,
};

// `pointerEvents`, which decides what a press can land on rather than what is
// drawn. CSS's four values, and React Native's: a view is a target, or it is
// not, or its children are and it is not, or it is and they are not.
//
// Here rather than in the mounting manager because hit testing is a pure
// function of the view tree -- `RnAppKitHitTest` takes no React Native types
// and has nowhere to look a prop up.
typedef NS_ENUM(NSInteger, RnAppKitPointerEvents) {
  RnAppKitPointerEventsAuto,
  RnAppKitPointerEventsNone,
  RnAppKitPointerEventsBoxNone,
  RnAppKitPointerEventsBoxOnly,
};

typedef NS_ENUM(NSInteger, RnAppKitImageFit) {
  RnAppKitImageFitCover,
  RnAppKitImageFitContain,
  RnAppKitImageFitStretch,
  RnAppKitImageFitCenter,
  // Tiled from the top left at the image's own size, which is what CSS
  // `repeat` and React Native's `resizeMode: 'repeat'` mean.
  RnAppKitImageFitRepeat,
};

// The decoded pixels of an <Image>. Pass nil to clear.
//
// A CGImage rather than a React Native type, so the view layer stays free of
// React Native headers -- the same arrangement the text layout has.
// AppKitImageLoader produces the image and AppKitMountingManager chooses the
// fit.
- (void)setRnImage:(nullable CGImageRef)image fit:(RnAppKitImageFit)fit;

// The frames of an animated image, their delays, and how many times round.
//
// `frames` holds CGImageRefs and `delaysMs` the file's own delays, one per
// frame, unclamped: core/ImageAnimation.h applies the clamp, because that rule
// is the same on every host. `loopCount` is zero for forever. Fewer than two
// frames, or nil, is a still image and clears any animation.
//
// The view shows frame zero immediately and schedules the rest. Passing the
// same `frames` array again is a no-op, which is how a layout-only mutation
// avoids restarting the animation; AppKitImageLoader keeps one array per URI
// for exactly that.
- (void)setRnImageFrames:(nullable NSArray *)frames
                delaysMs:(nullable NSArray<NSNumber *> *)delaysMs
               loopCount:(NSUInteger)loopCount;

// Starts the timer that shows the rest of the frames, if one is not running.
//
// Separate from `setRnImageFrames:` so that nothing animates by itself: the
// mounting manager starts it because the view is in an app, and the suite never
// does, which keeps a test about frames deterministic. The GTK view is split
// the same way, where it matters more -- rendering a widget there spins the
// main loop, and a self-driven animation would be several frames further on
// than the test left it.
- (void)rnStartImageAnimation;

// Moves the animation on by `milliseconds` and shows the frame that lands on,
// answering with how long until the next one, or zero when nothing more is due.
//
// The elapsed time is an argument rather than a clock read inside: the host's
// timer passes the delay it waited, and the suite passes whatever it likes,
// which is what makes "a hundred and fifty milliseconds later" something a test
// can say without sleeping.
- (double)rnAdvanceImageAnimationBy:(double)milliseconds;

// Which frame is showing, zero for a still image.
@property(nonatomic, readonly) NSUInteger rnImageFrameIndex;

// `tintColor`: recolours the image, keeping its alpha, so one silhouette asset
// can be drawn in any colour. Pass nil to clear.
//
// Separate from the image because the tint arrives from the props and the image
// from a loader, and either can land first.
- (void)setRnImageTint:(nullable NSColor *)tint;

// `blurRadius`: blurs the image and not the view, which is what the prop means
// -- a blurred photograph behind sharp text is the usual reason to ask for it.
// Zero or negative is no blur.
//
// The radius is a distance in this view's own coordinates rather than in the
// image's pixels, so the same number is the same picture here as on the GTK
// side. Also separate from the image for the reason the tint is.
- (void)setRnImageBlur:(CGFloat)radius;

// The paragraph this view draws, or nil for a view that draws none.
//
// A view either paints a layer or draws text; nothing here does both, because
// React Native's <Text> is a Paragraph node with no children of its own. Held
// as an opaque object so this header stays free of Core Text -- see
// RnTextLayout.h for the object, and CoreTextLayout.h for what builds one --
// both the mounting manager and the measurement seam go through that, so the
// size a view draws at is the size Yoga was told.
- (void)setRnTextLayout:(nullable id)layout;
- (void)setRnClipsChildren:(BOOL)clips;

// `zIndex`. Reorders painting and hit testing, and never the child list --
// Fabric's Insert and Remove carry an index into that list, so it has to stay
// in mutation order. Equal values keep document order, which is what CSS and
// React Native both promise.
- (void)setRnZIndex:(NSInteger)zIndex;
@property(nonatomic, readonly) NSInteger rnZIndex;

// `pointerEvents`. Read by RnAppKitHitTest and by nothing else -- it changes
// what a press finds and never what is drawn.
@property(nonatomic) RnAppKitPointerEvents rnPointerEvents;

// Whether this view takes keyboard focus, and therefore whether Tab stops on
// it.
//
// React Native has a `focusable` prop and it does not reach this platform:
// ReactCommon parses it only into Android's and tvOS's HostPlatformViewProps,
// and the C++ host's is a bare alias of BaseViewProps. What does reach here is
// `accessible`, which is what <Pressable> sets on everything it renders. See
// AppKitFocus.h.
//
// AppKit owns the chain: a view that accepts first responder status joins the
// window's key-view loop, so Tab and Shift+Tab work without this project
// deciding what "next" means.
@property(nonatomic) BOOL rnFocusable;

// This view's children in the order they paint: back to front, stably sorted
// by zIndex. The same list hit testing walks in reverse, so what is on top is
// what is hit -- the Win32 host pairs them the same way.
- (NSArray<RnAppKitView *> *)rnChildrenInPaintOrder;

// This view's placement in its parent: the frame's translation with the
// transform composed in, anchored at the centre.
//
// Public for one reason, which is the same reason Win32's `localToParent` is:
// hit testing inverts this, so deriving it anywhere else would let the two end
// up with different ideas of where a view is. A transform that is not affine --
// perspective -- has no 2D inverse, and this answers with the translation
// alone, which is what hit testing did for every transform before this existed.
- (CGAffineTransform)rnLocalToParent;

// A point in `root`'s coordinates, expressed in this view's own.
//
// Walks up composing `rnLocalToParent` and inverts the chain, which is the
// same matrix the hit test inverts one level at a time on the way down -- so a
// press and the coordinates it reports cannot disagree about where a view is.
//
// Not `-[NSView convertPoint:fromView:]`, which ignores the layer transform
// and so answers for a rotated view as though it were not rotated.
//
// NO when the chain has no inverse, which `scale: 0` produces and which has no
// sensible point in it; `out` is untouched then.
- (BOOL)rnPageToLocal:(NSPoint)page
             fromRoot:(nullable RnAppKitView *)root
                 into:(NSPoint *)out;
- (void)setRnCornerRadius:(CGFloat)radius;

// Per-corner radii, as four (horizontal, vertical) pairs -- eight floats -- in
// the order top-left, top-right, bottom-right, bottom-left. React Native's
// radii are elliptical and CALayer's single `cornerRadius` cannot express
// that, so anything a plain cornerRadius cannot say is applied as a mask layer
// instead. Null clears them. `setRnCornerRadius:` is the uniform-circular
// shorthand and goes through here.
- (void)setRnBorderRadii:(nullable const CGFloat *)radii;

// Per-edge border widths and colours, in the order top, right, bottom, left --
// the order CSS names them, and the order the GTK and Win32 sides store them
// in. `widths` is four floats; `colors` is sixteen, four RGBA quadruples in
// the same edge order. Either being null clears the border.
- (void)setRnBorderWidths:(nullable const CGFloat *)widths
                   colors:(nullable const CGFloat *)colors;

// One colour stop of a gradient: an offset from 0 to 1 along the gradient line
// and the colour there, as RGBA components.
typedef struct {
  CGFloat offset;
  CGFloat color[4];
} RnAppKitGradientStop;

// Which kind of gradient a record is. CSS's `background-image` is one list that
// can hold both, and the first in it is the one on top, so they cannot be two
// lists here without losing the order between them.
typedef NS_ENUM(NSInteger, RnAppKitGradientKind) {
  RnAppKitGradientKindLinear,
  RnAppKitGradientKindRadial,
};

// One `backgroundImage` gradient, already resolved against this view's size: a
// linear one's two end points, or a radial one's centre and two radii, in this
// view's own coordinates.
//
// Resolved rather than described, because the resolution is CSS's and is shared
// with the GTK host in core/Gradients.h -- the angle or the ending shape, the box
// size and the stop fixup all go into these numbers and the offsets. This layer
// draws what it is given. The stops are borrowed for the duration of the call.
typedef struct {
  RnAppKitGradientKind kind;
  // Linear only: the ends of the gradient line.
  CGPoint start;
  CGPoint end;
  // Radial only: the centre of the ending shape and its two radii. A circle is
  // an ellipse whose radii are equal, which is what CSS makes of one.
  CGPoint center;
  CGFloat radiusX;
  CGFloat radiusY;
  // Where the image goes and how it tiles, from `backgroundSize`,
  // `backgroundPosition` and `backgroundRepeat`, resolved in
  // core/BackgroundLayers.h: `area` is the rectangle the gradient fills, in this
  // view's coordinates, and `tile` is what it repeats in -- the same rectangle
  // for `repeat`, wider for `space`, and the painting area's extent on an axis
  // that does not repeat, so one step covers it.
  //
  // The gradient's own geometry above is resolved against `area`'s size and
  // offset to its origin, so this layer draws rather than positions.
  CGRect area;
  CGRect tile;
  bool repeats;
  const RnAppKitGradientStop *stops;
  NSInteger stopCount;
} RnAppKitGradient;

// `backgroundImage`. Replaces whatever was there; pass NULL or 0 for none.
//
// Drawn above the background colour, which is a layer property and therefore
// below anything `drawRect:` paints, and below the image and the text, which is
// where CSS puts a background image. First in the list is on top, so they are
// drawn back to front.
- (void)setRnGradients:(nullable const RnAppKitGradient *)gradients count:(NSInteger)count;

// How many are set, for the tests and the tree dump.
@property(nonatomic, readonly) NSInteger rnGradientCount;

// One CSS box shadow, in React Native's own terms: `BoxShadow` carries exactly
// these fields. The colour is RGBA components in that order, as the border
// colours arrive, so this header stays free of NSColor for the same reason the
// rest of it is.
typedef struct {
  CGFloat dx;
  CGFloat dy;
  CGFloat blur;
  CGFloat spread;
  CGFloat color[4];
  bool inset;
} RnAppKitBoxShadow;

// `boxShadow`. Replaces whatever was there; pass NULL or 0 for none.
//
// Order is CSS's: the first shadow in the list is the one on top. Each becomes a
// CALayer of its own with a shadow path and a mask, which is the only way to
// paint outside a view -- `drawRect:` is clipped to the bounds and an outset
// shadow is by definition outside them. It is also what React Native's own iOS
// half does, so a shadow lands in the same place on both.
- (void)setRnBoxShadows:(nullable const RnAppKitBoxShadow *)shadows count:(NSInteger)count;

// How many are set, for the tests and the tree dump.
@property(nonatomic, readonly) NSInteger rnBoxShadowCount;

// The layers those shadows became, bottom to top, for the tests: each one's
// `shadowPath`, `shadowRadius` and `mask` are the whole feature, and they are
// what macOS composites rather than anything this code draws.
@property(nonatomic, readonly) NSArray<CALayer *> *rnBoxShadowLayers;

// `filter`, already resolved: the colour matrix the list comes to, one blur
// radius and one opacity. See core/Filters.h, which is shared with the GTK host
// and does the arithmetic the Filter Effects spec specifies.
//
// `matrix` is row-major in R, G, B, A order and `offset` is added after it, both
// in straight colour, which is what CIColorMatrix takes. `blurRadius` is CSS's
// radius, twice the gaussian's sigma. `opacity` is 1 when the list has no
// `opacity()`.
// One `dropShadow()` from a filter list: a shadow of the subtree's alpha rather
// than of its box, which is what makes it a filter and not a box shadow.
//
// `standardDeviation` is the gaussian's, which is what React Native parses and
// what `CALayer.shadowRadius` takes, so it crosses unchanged. The colour is RGBA
// components, as the border and shadow colours are.
typedef struct {
  CGFloat dx;
  CGFloat dy;
  CGFloat standardDeviation;
  CGFloat color[4];
} RnAppKitFilterShadow;

typedef struct {
  bool hasMatrix;
  CGFloat matrix[16];
  CGFloat offset[4];
  CGFloat blurRadius;
  CGFloat opacity;
  // Borrowed for the duration of the call, in the order they were written: the
  // first is the one nearest the content. Only the first is drawn -- a CALayer
  // has one shadow -- and backlog/platform-macos.md records that.
  const RnAppKitFilterShadow *shadows;
  NSInteger shadowCount;
} RnAppKitFilters;

// `filter`. Pass NULL for none.
//
// Applies to this view and everything inside it, which is what CSS does.
// `CALayer.filters` is the mechanism and is the reason this is one of the few
// props macOS does better than React Native's own iOS half: Core Image filters
// on a layer are public API here and private there, so iOS builds a SwiftUI
// wrapper and a multiply-blend layer to approximate what this sets directly.
- (void)setRnFilters:(nullable const RnAppKitFilters *)filters;

// The Core Image filters the layer is carrying, for the tests: a shadow or a
// filter is composited by Core Animation and `renderInContext:` draws neither,
// so what each filter was given is the observable half here. The GTK side has
// the render tree and checks a pixel.
@property(nonatomic, readonly) NSArray<CIFilter *> *rnFilters;

// The drop shadow the filter list asked for, as the layer properties it became,
// or all zeroes when there is none. Public for the tests, for the reason
// `rnFilters` is: Core Animation composites a shadow and `renderInContext:` does
// not draw one.
@property(nonatomic, readonly) RnAppKitFilterShadow rnFilterShadow;

// `hitSlop`: how far outside its own box this view answers a press, in the order
// top, right, bottom, left -- the order CSS names edges and the order the border
// widths arrive in. Null or all zeroes is no slop.
//
// Positive values grow the target. A 44pt row with a 20pt icon in it is the
// reason the prop exists: the icon is what the eye aims at and the finger, or on
// a desktop a hurried cursor, misses it.
//
// It changes hit testing and nothing else: the view is drawn in its own frame,
// and `RnAppKitHitTest` is the only reader. That also means it applies to hover
// and to a drop as well as to a press, which is what iOS does, those all going
// through one hit test.
- (void)setRnHitSlop:(nullable const CGFloat *)insets;

// The box a press has to land in: the bounds, grown by `hitSlop`. Read by
// `RnAppKitHitTest` and by the tests, which is why it is here rather than in the
// implementation.
@property(nonatomic, readonly) NSRect rnHitArea;

// `borderStyle`, for the two values that are not solid.
typedef NS_ENUM(NSInteger, RnAppKitBorderStyle) {
  RnAppKitBorderStyleSolid = 0,
  RnAppKitBorderStyleDotted,
  RnAppKitBorderStyleDashed,
};

// A dotted or dashed border is drawn differently from a solid one: one stroked
// path around the view's own rounded rectangle, with a dash pattern scaled to
// the border width the way every browser does it, since a fixed pattern looks
// like a hairline on a thick border and like a solid line on a thin one.
//
// It replaces the four-edge fill rather than joining it, or the outline would be
// painted twice and the dashes would sit on a solid line.
//
// One style for the whole outline rather than one per side, which is the limit of
// this and is the limit on the other hosts for the same reason: a stroked path
// carries one dash pattern, and React Native's per-side `borderStyles` would need
// four paths with the corners divided between them. The first side that asks for
// something other than solid decides the outline, and the backlog records that.
- (void)setRnBorderStyle:(RnAppKitBorderStyle)style;

// `outlineWidth`, `outlineColor`, `outlineOffset` and `outlineStyle`.
//
// CSS's outline, which is not a border: drawn *outside* the box, no layout space,
// and `offset` away from the border edge. A focus ring is what it is for.
//
// A layer rather than a `drawRect:`, for the reason an outset box shadow is one:
// `drawRect:` is clipped to the bounds and this is outside them. It follows the
// same placement rule as the shadows -- inside the view, or in the parent when
// the view clips its own layer -- so the two props behave alike.
//
// The colour is RGBA components, as the border colours arrive. A zero width is no
// outline.
- (void)setRnOutlineWidth:(CGFloat)width
                   offset:(CGFloat)offset
                    color:(nullable const CGFloat *)color
                    style:(RnAppKitBorderStyle)style;

// The layer that outline became, for the tests: a stroked shape outside the box,
// which `renderInContext:` will not draw and a `drawRect:` bitmap cannot see.
@property(nonatomic, readonly, nullable) CAShapeLayer *rnOutlineLayer;

// Fabric's Insert and Remove carry an index into the parent's child list, so
// that list has to stay in mutation order.
- (void)insertRnChild:(RnAppKitView *)child atIndex:(NSInteger)index;
- (void)removeRnChild:(RnAppKitView *)child;

// The same one-line-per-view format the GTK side produces, so the two can be
// compared and eventually asserted on by the same tests.
// --- React DevTools' overlay --------------------------------------------------
//
// The rectangles a `DebuggingOverlay` draws: an inspected element's blue box,
// or an outline around everything that just re-rendered. Set from
// AppKitMountingManager, which parses them; see core/DebuggingOverlay.h.
//
// Eight numbers per rectangle -- x, y, width, height, then r, g, b, a -- and a
// flag per rectangle for whether to fill. Plain arrays rather than the core
// struct because this file has no React Native in it.
- (void)setRnHighlights:(nullable const float *)rectangles
                 filled:(nullable const bool *)filled
                  count:(NSInteger)count;

// Accessibility, as a screen reader sees it.
//
// The role arrives as React Native's own string -- "button", "image", "text" --
// rather than as an NSAccessibilityRole, and the view maps it. Two reasons.
// The mapping is a platform decision and belongs beside the platform; and the
// string is what `describeTree` reports, so the two hosts' trees can be
// compared without one of them speaking AppKit's vocabulary and the other
// speaking GTK's.
//
// Pass nil or an empty string for no role, which is what a plain <View> is.
- (void)setRnAccessibleRole:(nullable NSString *)role;

// `label` is the accessible name and `hint` the description; either may be nil
// or empty to leave it unset.
- (void)setRnAccessibleLabel:(nullable NSString *)label hint:(nullable NSString *)hint;

// Accessible states. Each is a tri-state: unset leaves AppKit's default alone,
// which is not the same as setting it false.

// accessibilityValue: a range and a position in it, or a text form that reads
// better than the number. nil for a part the app did not give, which is how an
// element says it has none. See the definition: this shares AppKit's one
// `accessibilityValue` with the checked state, and an explicit value wins.
- (void)setRnAccessibleValueMin:(nullable NSNumber *)min
                            max:(nullable NSNumber *)max
                            now:(nullable NSNumber *)now
                           text:(nullable NSString *)text;

- (void)setRnAccessibleStateDisabled:(RnAppKitAccessibleFlag)disabled
                             checked:(RnAppKitAccessibleFlag)checked
                            selected:(RnAppKitAccessibleFlag)selected
                            expanded:(RnAppKitAccessibleFlag)expanded
                                busy:(RnAppKitAccessibleFlag)busy;

// `accessibilityLabelledBy`, already resolved: the views whose text names this
// one. Pass nil or an empty array to take the relation off again.
//
// The prop names other views by their `nativeID`, and the resolution -- and
// above all *when* to resolve, Fabric mounting a field before the label that
// follows it -- is shared with the GTK host in core/LabelRegistry.h. This layer
// is handed the answer.
//
// **AppKit takes one.** `accessibilityTitleUIElement` is a single element where
// the prop is a list and GTK's relation is a list, so the first is used and the
// rest are kept only for the tree dump, which reports what was resolved on both
// hosts. backlog/accessibility.md records that.
- (void)setRnLabelledBy:(nullable NSArray<RnAppKitView *> *)labels;

// `experimental_accessibilityOrder`, resolved: the children this view wants a
// screen reader to read, in that order. Sets `accessibilityChildren`, which is
// what VoiceOver walks in place of the view hierarchy.
- (void)setRnAccessibilityOrder:(nullable NSArray<RnAppKitView *> *)children;

// The text of this view and everything inside it, as a screen reader would read
// it: each paragraph's string in tree order, separated by single spaces.
//
// Here for `accessibilityLiveRegion`, which announces a status message when it
// changes and so needs to know what the message currently says. An accessible
// label stands in for the text where one was set, which the mounting manager
// decides before calling this, as the GTK host does.
@property(nonatomic, readonly) NSString *rnCollectedText;

// The text this view draws itself, or nil: the paragraph it holds, and nothing
// from its children. The recursive half of `rnCollectedText` reads it from each
// view it walks, which is why it is here rather than in the implementation.
@property(nonatomic, readonly, nullable) NSString *rnOwnText;

// Reads `text` out to VoiceOver now, as `accessibilityLiveRegion` asks. Not a
// property: AppKit posts an announcement, and whether anybody is listening is
// VoiceOver's business.
//
// `assertive` asks to interrupt what is being read; polite waits for a gap,
// which is what the two values of the prop mean.
//
// The announcement is also remembered, which is the only way to see that it
// happened: nothing in an automated run is running VoiceOver, so
// `rnLastAnnouncement` is what the tests read. Deliberately not in the tree dump
// -- an announcement is an event and the dump is state -- and the host logs it.
- (void)rnAnnounce:(NSString *)text assertive:(BOOL)assertive;

// The last text this view announced, or nil.
@property(nonatomic, readonly, nullable) NSString *rnLastAnnouncement;

// Hidden from assistive technology, for accessible={false} and
// accessibilityElementsHidden.
- (void)setRnAccessibleHidden:(BOOL)hidden;

// The app's `nativeID`, which this platform uses to mark a view for a native
// behaviour it has no React Native prop for -- today, a drop target. See
// core/DragAndDrop.h, and core/TitleBarRegions.h for the older use of the
// same idea. Stored and never drawn.
@property(nonatomic, copy, nullable) NSString *rnNativeId;

// The app's `testID`, which is not `nativeID`: one is how an app names a view
// to itself, the other is how something driving the app from outside finds it.
// Setting it sets `accessibilityIdentifier`, which is what XCTest, Appium and
// the Accessibility Inspector all look for, and it is printed in the tree dump
// the way React Native spelled it.
@property(nonatomic, copy, nullable) NSString *rnTestId;

// `accessibilityViewIsModal`: a screen reader should stay inside this view
// rather than reading what is behind it. Sets AppKit's `accessibilityModal`,
// which is the same idea as ARIA's `aria-modal` and UIA's `IsDialog`.
@property(nonatomic, assign) BOOL rnAccessibleModal;

// `writingDirection`, for the tree dump: "ltr", "rtl", "natural", or nil when
// the app asked for nothing. The direction itself is in the paragraph style the
// layout carries; this is so the two hosts' dumps compare line by line.
@property(nonatomic, copy, nullable) NSString *rnWritingDirection;

// `spellCheck` and `autoCorrect`, for the tree dump: @"on", @"off" or nil for a
// field that said nothing, which is the third state rather than a default. What
// this host does with them is two properties on the NSTextView a field is or
// borrows; see AppKitTextPeer.h.
@property(nonatomic, copy, nullable) NSString *rnSpellCheck;
@property(nonatomic, copy, nullable) NSString *rnAutoCorrect;

// `autoCapitalize` and `keyboardType`, for the tree dump, as React Native
// spells them. **This host acts on neither**, and says so rather than
// pretending: macOS has no per-field automatic capitalisation
// (`NSSpellChecker`'s is the person's system setting) and no software keyboard
// to choose a layout for. GTK turns both into input hints and an input
// purpose, so printing the same words is what keeps the two dumps comparable
// and puts the difference in the support page, where it belongs.
@property(nonatomic, copy, nullable) NSString *rnAutoCapitalize;
@property(nonatomic, copy, nullable) NSString *rnKeyboardType;

// `caretHidden` and `contextMenuHidden`, for the tree dump. What this host
// *does* with them is a clear insertion point and a peer that refuses to build
// a menu; see AppKitTextPeer.h, which also records the half of the second one a
// single-line field cannot answer. Printed because each is the absence of
// something -- a blink, a menu -- so nothing else can see that they arrived.
@property(nonatomic, assign) BOOL rnCaretHidden;
@property(nonatomic, assign) BOOL rnContextMenuHidden;

// The `cursor` style property, as the CSS keyword React Native uses: "pointer",
// "text", "grab", "ns-resize" and the rest. Nil or empty leaves the cursor to
// whatever encloses this view, which is what `cursor: 'auto'` means.
//
// A keyword rather than an NSCursor because the name is what crosses the seam
// from the mounting manager, shared with the GTK host through
// core/CursorNames.h, and because macOS has no cursor for some of them: the
// mapping and its holes belong on this side. See `rnResolvedCursor`.
- (void)setRnCursorName:(nullable NSString *)name;

// The NSCursor that name resolves to, or nil for a keyword macOS has no cursor
// for -- "wait", "help", "move", "progress", "cell", "all-scroll". Nil means
// this view installs no cursor rect at all, so the cursor is inherited rather
// than forced back to an arrow.
//
// Public for the tests, which assert the mapping, and read by nothing else: the
// cursor itself is applied through `resetCursorRects`, as AppKit expects.
@property(nonatomic, readonly, nullable) NSCursor *rnResolvedCursor;

// `mixBlendMode`, as the CSS keyword, shared with the GTK host through
// core/BlendModes.h: "multiply", "screen", "color-dodge" and the rest. Nil or
// empty is `normal`, meaning ordinary source-over compositing.
//
// Core Animation does the whole feature in one property on macOS:
// `CALayer.compositingFilter` takes a Core Image blend-mode filter and blends
// the layer with what is already beneath it. The keyword maps onto one
// `CI<Name>BlendMode` per CSS mode, including `plus-lighter`, which GSK has no
// node for.
- (void)setRnBlendModeName:(nullable NSString *)name;

// The keyword that was set, or nil: what the app asked for.
@property(nonatomic, readonly, nullable) NSString *rnBlendModeName;

// The name of the Core Image filter that keyword resolved to, or nil. Public for
// the tests, which assert the mapping and that Core Image has each filter: a
// compositing filter cannot be seen in a snapshot, because `renderInContext:`
// composites nothing.
@property(nonatomic, readonly, nullable) NSString *rnBlendFilterName;

// Set on the surface root. See RnAppKitInputHandler.
@property(nonatomic, weak, nullable) id<RnAppKitInputHandler> rnInputHandler;

// Set on the surface root. See RnAppKitFocusHandler.
@property(nonatomic, weak, nullable) id<RnAppKitFocusHandler> rnFocusHandler;

// The text field this view hosts, for <TextInput>.
//
// Set by AppKitTextInputManager, which owns it and positions it; the view only
// reads it, to report the field's contents in `describeTree`. Held weakly so a
// deleted field is not kept alive by the view that was showing it.
//
// A real NSTextField rather than a caret drawn on a paragraph: that brings
// input methods, selection, the clipboard and every key binding a Mac user
// expects, none of which is worth reimplementing and all of which is easy to
// get subtly wrong. The GTK side embeds a real GtkText for the same reason.
// The <TextInput> peer: an NSTextField for a single-line field and an
// NSTextView for a multiline one. See AppKitTextPeer.h.
@property(nonatomic, weak, nullable) NSView *rnEditable;

// --- Controls ----------------------------------------------------------------
//
// The three components that are a toolkit control rather than a box:
// <ActivityIndicator> and <RefreshControl> are an NSProgressIndicator,
// <Switch> is an NSSwitch. Held the same way the text peer above is -- weakly,
// because the subview relationship is what owns it -- and for the same reason:
// AppKit's own control brings the theme, the animation and the accessibility
// with it.
typedef NS_ENUM(NSInteger, RnAppKitControlKind) {
  RnAppKitControlNone = 0,
  RnAppKitControlSpinner,
  RnAppKitControlSwitch,
};

@property(nonatomic, weak, nullable) NSView *rnControl;
@property(nonatomic) RnAppKitControlKind rnControlKind;

// What `describeTree` prints for this control, written by
// core/DesktopControls.h so that three hosts cannot describe the same switch
// differently.
@property(nonatomic, copy, nullable) NSString *rnControlDescription;

// Set on ScrollViews only. See RnAppKitScrollHandler.
@property(nonatomic, weak, nullable) id<RnAppKitScrollHandler> rnScrollHandler;

// Shifts this view's children by (-x, -y), which is how a ScrollView scrolls.
//
// Through `bounds.origin` rather than by moving every child: AppKit then shifts
// drawing, hit testing and the clip together, and the frames the mounting
// manager wrote stay exactly the ones Yoga produced. The GTK side does the same
// thing by shifting children in its layout manager, for the same reason --
// React Native decides sizes, and the platform only places.
- (void)setRnScrollOffsetX:(CGFloat)x y:(CGFloat)y;
- (NSPoint)rnScrollOffset;

// The overlay scrollbars, as core/ScrollIndicator.h decides them: an offset
// along the track and a length, per axis, with a length of zero meaning "do not
// draw one".
//
// Drawn by a subview kept above every React child rather than in `drawRect:`,
// because AppKit paints subviews over their superview and a ScrollView's whole
// job is to have one covering it. That view is not an RnAppKitView, so it stays
// out of the paint order, out of hit testing and out of `describeTree`'s
// children -- the numbers are reported on the ScrollView's own line instead.
// `indicatorStyle`, arriving as the colour `core/ScrollIndicator.h` resolved it
// to rather than as the enum: the mapping is the same on all three desktops and
// belongs in one place, and this layer knows nothing about React Native's
// vocabulary.
- (void)setRnScrollIndicatorColourRed:(CGFloat)red
                                green:(CGFloat)green
                                 blue:(CGFloat)blue
                                alpha:(CGFloat)alpha;
// What that colour is now, for the test suite: the thumb is drawn into a layer
// a test can rasterise, but reading the colour back is the cheaper assertion.
- (NSColor *)rnScrollIndicatorColour;

- (void)setRnScrollIndicatorVerticalOffset:(CGFloat)verticalOffset
                            verticalLength:(CGFloat)verticalLength
                          horizontalOffset:(CGFloat)horizontalOffset
                          horizontalLength:(CGFloat)horizontalLength;

- (NSString *)describeTree;

@end

NS_ASSUME_NONNULL_END
#endif
