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

// Hidden from assistive technology, for accessible={false} and
// accessibilityElementsHidden.
- (void)setRnAccessibleHidden:(BOOL)hidden;

// The app's `nativeID`, which this platform uses to mark a view for a native
// behaviour it has no React Native prop for -- today, a drop target. See
// core/DragAndDrop.h, and core/TitleBarRegions.h for the older use of the
// same idea. Stored and never drawn.
@property(nonatomic, copy, nullable) NSString *rnNativeId;

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
- (void)setRnScrollIndicatorVerticalOffset:(CGFloat)verticalOffset
                            verticalLength:(CGFloat)verticalLength
                          horizontalOffset:(CGFloat)horizontalOffset
                          horizontalLength:(CGFloat)horizontalLength;

- (NSString *)describeTree;

@end

NS_ASSUME_NONNULL_END
#endif
