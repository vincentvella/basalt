#import "RnAppKitView.h"

#include "ControlMetrics.h"
#include "Backface.h"
#include "FocusRing.h"
#include "ScrollIndicator.h"

#include <vector>

#import "AppKitTextPeer.h"

#include "PointerButtons.h"

#include <algorithm>
#include <cmath>

#import "RnTextLayout.h"

#import <CoreImage/CoreImage.h>
#import <QuartzCore/QuartzCore.h>

// Topmost first: AppKit's subviews array is back to front, and a hit test wants
// the view that is drawn on top of the others.
//
// A child is only entered when the point is inside its frame, which means a
// child that overflows a parent -- React Native's default, since `overflow` is
// `visible` -- is not reachable through that parent. In practice Fabric's view
// flattening hoists most such children out to an ancestor that does contain
// them, so this matters less than it reads; the GTK side has the same limit for
// the same reason.
// The spelling React Native uses for the prop, which is also CSS's, so the
// three hosts' dumps say the same words.
static const char *RnAppKitPointerEventsName(RnAppKitPointerEvents mode) {
  switch (mode) {
    case RnAppKitPointerEventsNone:
      return "none";
    case RnAppKitPointerEventsBoxNone:
      return "box-none";
    case RnAppKitPointerEventsBoxOnly:
      return "box-only";
    case RnAppKitPointerEventsAuto:
      break;
  }
  return "auto";
}

RnAppKitView *RnAppKitHitTest(RnAppKitView *root, CGFloat x, CGFloat y) {
  // `pointerEvents: none` takes the view and everything inside it out of hit
  // testing entirely, so the caller's loop carries on to whatever is behind.
  if (root == nil || root.hidden || root.rnPointerEvents == RnAppKitPointerEventsNone) {
    return nil;
  }

  // The point arrives relative to this view's visible top-left; child frames
  // are in its *bounds* space. For an unscrolled view those are the same, and
  // for a scrolled one they differ by exactly the offset -- which is what makes
  // hit testing follow the scroll without anything here knowing about
  // ScrollViews.
  const NSRect bounds = root.bounds;
  const CGFloat bx = x + bounds.origin.x;
  const CGFloat by = y + bounds.origin.y;

  // `hitSlop` grows the box this test accepts, and only this test: the view is
  // still drawn in its own frame. Children are reached through their own call,
  // so each view's slop is applied to its own box on the way down.
  if (!NSPointInRect(NSMakePoint(bx, by), [root rnHitArea])) {
    return nil;
  }

  // Back to front, so reversed is topmost first -- and "topmost" has to mean
  // what was painted last, which zIndex can change. Win32's hitTest walks its
  // own childrenInPaintOrder the same way round.
  //
  // `box-only` is the one mode that skips this: the box is the target and
  // nothing inside it is, which is what makes an overlay swallow a press meant
  // for a button drawn on top of it.
  if (root.rnPointerEvents != RnAppKitPointerEventsBoxOnly) {
    for (RnAppKitView *child in [root rnChildrenInPaintOrder].reverseObjectEnumerator) {
      // Into the child's own space by inverting what places it in this one.
      // Subtracting the frame origin would be the same thing for an
      // untransformed view and wrong for every other: a rotated view was
      // clickable where it would have been rather than where it is drawn.
      const CGAffineTransform toParent = [child rnLocalToParent];

      // A transform can be degenerate -- `scale: 0` is legal and draws
      // nothing. There is no point inside something with no area, so it is
      // skipped rather than inverted.
      const CGFloat determinant = toParent.a * toParent.d - toParent.b * toParent.c;
      if (determinant == 0.0) {
        continue;
      }

      const CGPoint local =
          CGPointApplyAffineTransform(NSMakePoint(bx, by), CGAffineTransformInvert(toParent));
      RnAppKitView *hit = RnAppKitHitTest(child, local.x, local.y);
      if (hit != nil) {
        return hit;
      }
    }
  }

  // `box-none` is transparent to a press that misses everything inside it.
  // Returning nil rather than the parent is the whole of it: the caller is
  // partway through its own list of children, so the press carries on to the
  // sibling *behind* this view -- which is what the absolutely-positioned
  // overlay this mode exists for is asking for.
  if (root.rnPointerEvents == RnAppKitPointerEventsBoxNone) {
    return nil;
  }
  return root;
}

// A rounded rectangle with a different radius pair at each corner.
//
// CGPathAddArcToPoint only draws circular arcs, and React Native's radii are
// elliptical, so the corners are cubic Beziers. kappa is the usual constant
// for approximating a quarter ellipse with one curve -- the error is under a
// thousandth of the radius, which is far below a pixel at any radius an
// interface uses.
static CGPathRef RnAppKitCreateRoundedPath(CGRect rect, const CGFloat radii[8]) {
  static const CGFloat kappa = (CGFloat)0.5522847498307936;
  const CGFloat x = CGRectGetMinX(rect);
  const CGFloat y = CGRectGetMinY(rect);
  const CGFloat w = CGRectGetWidth(rect);
  const CGFloat h = CGRectGetHeight(rect);

  // Clamp so that two radii along one edge can never exceed it. The values
  // arriving from Fabric are already clamped, but this path is also built for
  // the *inner* edge of a border, whose radii are reduced by the border widths
  // and can be anything.
  CGFloat r[8];
  for (int i = 0; i < 8; i++) {
    r[i] = radii[i] > 0 ? radii[i] : 0;
  }
  const CGFloat tlw = r[0], tlh = r[1], trw = r[2], trh = r[3];
  const CGFloat brw = r[4], brh = r[5], blw = r[6], blh = r[7];

  CGMutablePathRef path = CGPathCreateMutable();
  CGPathMoveToPoint(path, NULL, x + tlw, y);
  CGPathAddLineToPoint(path, NULL, x + w - trw, y);
  CGPathAddCurveToPoint(path, NULL,
                        x + w - trw + trw * kappa, y,
                        x + w, y + trh - trh * kappa,
                        x + w, y + trh);
  CGPathAddLineToPoint(path, NULL, x + w, y + h - brh);
  CGPathAddCurveToPoint(path, NULL,
                        x + w, y + h - brh + brh * kappa,
                        x + w - brw + brw * kappa, y + h,
                        x + w - brw, y + h);
  CGPathAddLineToPoint(path, NULL, x + blw, y + h);
  CGPathAddCurveToPoint(path, NULL,
                        x + blw - blw * kappa, y + h,
                        x, y + h - blh + blh * kappa,
                        x, y + h - blh);
  CGPathAddLineToPoint(path, NULL, x, y + tlh);
  CGPathAddCurveToPoint(path, NULL,
                        x, y + tlh - tlh * kappa,
                        x + tlw - tlw * kappa, y,
                        x + tlw, y);
  CGPathCloseSubpath(path);
  return path;
}

// One gradient as the view keeps it: the two points and its own stops. A
// transition hint turns three stops into eleven, so the lists are different
// lengths for no reason the caller controls, and each gradient owns its own.
struct RnAppKitGradientRecord {
  RnAppKitGradientKind kind;
  // Linear: the ends of the gradient line. Radial: the centre and the radii.
  CGPoint start;
  CGPoint end;
  CGPoint center;
  CGFloat radiusX;
  CGFloat radiusY;
  // Where the image goes and the tile it repeats in. See RnAppKitGradient.
  CGRect area;
  CGRect tile;
  bool repeats;
  std::vector<RnAppKitGradientStop> stops;
};

// A corner radius grown by a shadow's spread, which is not just an addition.
//
// The CSS spec says so and gives the curve: a corner tighter than the spread is
// rounded off more gently than the spread alone would make it, so a small radius
// on a widely spread shadow does not turn into a circle. Ported from React
// Native's iOS half, which took it from the same place.
// See https://drafts.csswg.org/css-backgrounds/#shadow-shape
static CGFloat RnAppKitSpreadRadius(CGFloat radius, CGFloat spread) {
  CGFloat adjustment = spread;
  if (radius < fabs(spread)) {
    const CGFloat ratio = radius / fabs(spread);
    adjustment *= 1.0 + pow(ratio - 1.0, 3.0);
  }
  return fmax(radius + adjustment, 0);
}

// The eight radii of a shadow's own box, each grown by the spread.
static void RnAppKitSpreadRadii(const CGFloat radii[8], CGFloat spread, CGFloat out[8]) {
  for (int i = 0; i < 8; i++) {
    out[i] = RnAppKitSpreadRadius(radii[i], spread);
  }
}

// The same path, walked the other way, so that adding it to another one punches
// a hole rather than filling it: `shadowPath` has no fill rule and winds
// non-zero. NSBezierPath has reversal built in, and hand-rolling it means
// reversing four elliptical corners in the right order, which is exactly the
// kind of code that is wrong in one corner only.
static CGPathRef RnAppKitCreateReversedPath(CGPathRef path) {
  NSBezierPath *bezier = [NSBezierPath bezierPathWithCGPath:path];
  return CGPathCreateCopy([bezier bezierPathByReversingPath].CGPath);
}

// A shadow cast outward by the view's box.
//
// The offset and the spread are baked into the path rather than set as
// `shadowOffset`, which is what keeps a positive dy pointing down in this
// flipped view, and the mask cuts the box itself out so the shadow is only ever
// outside it -- a shadow painted under a translucent background would otherwise
// show through it, which CSS does not do.
static CALayer *RnAppKitOutsetShadowLayer(const RnAppKitBoxShadow &shadow,
                                          const CGFloat radii[8],
                                          CGSize size) {
  CALayer *layer = [CALayer layer];
  layer.frame = CGRectMake(0, 0, size.width, size.height);
  layer.shadowOffset = CGSizeZero;
  layer.shadowOpacity = 1;
  // Half the radius, which is the same conversion the image blur uses and the
  // same one React Native's iOS half settled on: CSS's blur radius is twice the
  // gaussian's sigma, and CALayer's shadowRadius is the sigma.
  layer.shadowRadius = shadow.blur > 0 ? shadow.blur / 2 : 0;

  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGColorRef color = CGColorCreate(space, shadow.color);
  layer.shadowColor = color;
  CGColorRelease(color);
  CGColorSpaceRelease(space);

  CGFloat spreadRadii[8];
  RnAppKitSpreadRadii(radii, shadow.spread, spreadRadii);
  CGRect box = CGRectInset(layer.bounds, -shadow.spread, -shadow.spread);
  box = CGRectOffset(box, shadow.dx, shadow.dy);
  CGPathRef shadowPath = RnAppKitCreateRoundedPath(box, spreadRadii);
  layer.shadowPath = shadowPath;

  // Everything outside the view's own box, as an even-odd pair: the box, and a
  // rectangle big enough to hold the whole blur around it.
  CAShapeLayer *mask = [CAShapeLayer layer];
  mask.fillRule = kCAFillRuleEvenOdd;
  CGMutablePathRef maskPath = CGPathCreateMutable();
  CGPathRef viewPath = RnAppKitCreateRoundedPath(layer.bounds, radii);
  CGPathAddPath(maskPath, NULL, viewPath);
  const CGFloat room = 2 * (fabs(shadow.blur) + 1);
  CGPathRef around = RnAppKitCreateRoundedPath(CGRectInset(box, -room, -room), spreadRadii);
  CGPathAddPath(maskPath, NULL, around);
  mask.path = maskPath;
  layer.mask = mask;

  CGPathRelease(maskPath);
  CGPathRelease(viewPath);
  CGPathRelease(around);
  CGPathRelease(shadowPath);
  return layer;
}

// A shadow cast inward from the box's own edge.
//
// Built from the hole it leaves: the shadow path is a wide rectangle with the
// clear region punched out of it, so the blur spills inward from the edge of
// that hole, and the mask keeps all of it inside the view. The clear region is
// the box moved by the offset and shrunk by the spread, which is what makes an
// inset shadow with no offset a ring and one with an offset a crescent.
static CALayer *RnAppKitInsetShadowLayer(const RnAppKitBoxShadow &shadow,
                                         const CGFloat radii[8],
                                         CGSize size) {
  CALayer *layer = [CALayer layer];
  layer.frame = CGRectMake(0, 0, size.width, size.height);
  layer.shadowOffset = CGSizeZero;
  layer.shadowOpacity = 1;
  layer.shadowRadius = shadow.blur > 0 ? shadow.blur / 2 : 0;

  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGColorRef color = CGColorCreate(space, shadow.color);
  layer.shadowColor = color;
  CGColorRelease(color);
  CGColorSpaceRelease(space);

  const CGFloat room = fabs(shadow.blur) + 1;
  CGMutablePathRef path = CGPathCreateMutable();
  // Square, because this one is only there to be the outside of the hole: its
  // own corners are beyond the mask and never seen.
  const CGFloat square[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  CGPathRef outer = RnAppKitCreateRoundedPath(CGRectInset(layer.bounds, -room, -room), square);
  CGPathAddPath(path, NULL, outer);

  CGFloat holeRadii[8];
  RnAppKitSpreadRadii(radii, -shadow.spread, holeRadii);
  CGRect hole = CGRectOffset(layer.bounds, shadow.dx, shadow.dy);
  hole = CGRectInset(hole, shadow.spread, shadow.spread);
  if (CGRectIsNull(hole) || hole.size.width < 0 || hole.size.height < 0) {
    hole = CGRectZero;
  }
  CGPathRef holePath = RnAppKitCreateRoundedPath(hole, holeRadii);
  CGPathRef reversedHole = RnAppKitCreateReversedPath(holePath);
  CGPathAddPath(path, NULL, reversedHole);
  layer.shadowPath = path;

  CAShapeLayer *mask = [CAShapeLayer layer];
  CGPathRef viewPath = RnAppKitCreateRoundedPath(layer.bounds, radii);
  mask.path = viewPath;
  layer.mask = mask;

  CGPathRelease(path);
  CGPathRelease(outer);
  CGPathRelease(holePath);
  CGPathRelease(reversedHole);
  CGPathRelease(viewPath);
  return layer;
}

// Clips to the side of the line through `p` with direction `dir` that contains
// `inside`. Two of these give an edge the sector it owns at a corner.
//
// A half-plane rather than a polygon because a sector has no far end: the
// border band at a rounded corner runs as far out as the radius takes it, and
// any bounded shape has to guess how far that is.
static void RnAppKitClipToHalfPlane(CGContextRef context,
                                    CGPoint p,
                                    CGPoint dir,
                                    CGPoint inside,
                                    CGFloat extent) {
  const CGFloat length = std::hypot(dir.x, dir.y);
  if (length < (CGFloat)1e-6) {
    // Both borders meeting here are zero wide, so there is nothing to divide.
    return;
  }
  const CGPoint along = {dir.x / length, dir.y / length};
  const CGPoint normal = {-along.y, along.x};
  const CGFloat side =
      (inside.x - p.x) * normal.x + (inside.y - p.y) * normal.y;
  const CGFloat sign = side < 0 ? -1 : 1;

  const CGPoint a = {p.x + along.x * extent, p.y + along.y * extent};
  const CGPoint b = {p.x - along.x * extent, p.y - along.y * extent};
  const CGPoint c = {b.x + normal.x * sign * extent, b.y + normal.y * sign * extent};
  const CGPoint d = {a.x + normal.x * sign * extent, a.y + normal.y * sign * extent};

  CGContextBeginPath(context);
  CGContextMoveToPoint(context, a.x, a.y);
  CGContextAddLineToPoint(context, b.x, b.y);
  CGContextAddLineToPoint(context, c.x, c.y);
  CGContextAddLineToPoint(context, d.x, d.y);
  CGContextClosePath(context);
  CGContextClip(context);
}

// The overlay scrollbars, as a view of their own.
//
// AppKit paints subviews over their superview, and a ScrollView always has one
// -- its content -- covering the whole of it, so a thumb drawn in the scroll
// view's own `drawRect:` would be behind everything it is meant to float over.
// This sits above them all instead. See core/ScrollIndicator.h for the
// geometry, which all three hosts share.
@interface RnAppKitScrollIndicatorView : NSView
@property(nonatomic) CGFloat rnVerticalOffset;
@property(nonatomic) CGFloat rnVerticalLength;
@property(nonatomic) CGFloat rnHorizontalOffset;
@property(nonatomic) CGFloat rnHorizontalLength;
@end

@implementation RnAppKitScrollIndicatorView

// Top-left origin, like every other view here and like React Native.
- (BOOL)isFlipped {
  return YES;
}

// Paint and nothing else: a press falls through to the content underneath.
// Nothing here is a drag target yet -- docs/backlog records that.
- (NSView *)hitTest:(NSPoint)point {
  (void)point;
  return nil;
}

- (void)rnFillPill:(NSRect)bar inContext:(CGContextRef)context {
  const CGFloat radius = basalt::kScrollIndicatorThickness / 2.0;
  CGPathRef path = CGPathCreateWithRoundedRect(bar, radius, radius, NULL);
  CGContextAddPath(context, path);
  CGContextFillPath(context);
  CGPathRelease(path);
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  if (_rnVerticalLength <= 0.0 && _rnHorizontalLength <= 0.0) {
    return;
  }
  CGContextRef context = [NSGraphicsContext currentContext].CGContext;
  const NSSize size = self.bounds.size;
  const CGFloat thickness = basalt::kScrollIndicatorThickness;
  const CGFloat inset = basalt::kScrollIndicatorInset;
  // Neutral and translucent, so it reads over light and dark content alike --
  // the same colour the GTK and Win32 hosts use.
  CGContextSetRGBFillColor(context, 0.0, 0.0, 0.0, 0.35);

  if (_rnVerticalLength > 0.0) {
    [self rnFillPill:NSMakeRect(size.width - thickness - inset,
                                _rnVerticalOffset,
                                thickness,
                                _rnVerticalLength)
           inContext:context];
  }
  if (_rnHorizontalLength > 0.0) {
    [self rnFillPill:NSMakeRect(_rnHorizontalOffset,
                                size.height - thickness - inset,
                                _rnHorizontalLength,
                                thickness)
           inContext:context];
  }
}

@end

@implementation RnAppKitView {
  // React DevTools' overlay rectangles: eight floats each, plus a fill flag.
  // See setRnHighlights:filled:count:.
  std::vector<float> _highlights;
  std::vector<bool> _highlightFilled;
  NSString *_roleName;
  RnTextLayout *_textLayout;
  CGImageRef _image;
  NSColor *_imageTint;
  RnAppKitImageFit _imageFit;
  CGFloat _imageBlur;
  // The blurred copy of _image, at the device pixel size it was last drawn at,
  // or NULL when there is none to reuse. See -rnBlurredImageForSize:scale:.
  CGImageRef _imageBlurred;
  BOOL _hasBackgroundColor;
  CGFloat _backgroundComponents[4];
  CGFloat _opacity;
  // The `opacity()` out of the filter list, 1 when there is none.
  CGFloat _filterOpacity;
  BOOL _clipsChildren;
  CGFloat _cornerRadius;
  // Eight floats: a horizontal and a vertical radius per corner, in the order
  // top-left, top-right, bottom-right, bottom-left. React Native clamps
  // opposite corners against the frame before these arrive, so nothing here
  // has to.
  CGFloat _borderRadii[8];
  BOOL _hasBorderRadii;
  // top, right, bottom, left -- the order CSS names them.
  CGFloat _borderWidths[4];
  // Four RGBA quadruples, in the same edge order.
  CGFloat _borderColors[16];
  BOOL _hasBorders;
  // CSS's outline, and the layer it became. Zero width is none.
  CGFloat _outlineWidth;
  CGFloat _outlineOffset;
  CGFloat _outlineColor[4];
  RnAppKitBorderStyle _outlineStyle;
  CAShapeLayer *_outlineLayer;
  // The resolved `filter`, as the Core Image filters it became, or nil.
  NSArray<CIFilter *> *_filters;
  // `hitSlop`, top, right, bottom, left. Read only by the hit test.
  CGFloat _hitSlop[4];
  BOOL _hasHitSlop;
  RnAppKitBorderStyle _borderStyle;
  std::vector<RnAppKitBoxShadow> _boxShadows;
  // The `backgroundImage` gradients, each with its own stops.
  std::vector<RnAppKitGradientRecord> _gradients;
  NSMutableArray<CALayer *> *_boxShadowLayers;
  NSString *_cursorName;
  // `mixBlendMode`: the keyword asked for, and the Core Image filter it became.
  // The `dropShadow()` from `filter`, as the layer's own shadow.
  RnAppKitFilterShadow _filterShadow;
  NSString *_blendModeName;
  NSString *_blendFilterName;
  // The last text this view announced as a live region. See -rnAnnounce:.
  NSString *_lastAnnouncement;
  // The tags of the views in this one's labelled-by relation, for the tree dump.
  std::vector<NSInteger> _labelledBy;
  NSCursor *_cursor;
  CATransform3D _transform;
  BOOL _hasTransform;
  BOOL _hidesBackFace;
  BOOL _hiddenByApp;
  BOOL _hiddenByBackFace;
  NSInteger _zIndex;
  // Only the surface root has one; see -updateTrackingAreas.
  NSTrackingArea *_rnHoverTrackingArea;
  // Only a ScrollView with something to indicate has one.
  RnAppKitScrollIndicatorView *_indicators;
}

+ (instancetype)viewWithTag:(NSInteger)tag {
  RnAppKitView *view = [[RnAppKitView alloc] initWithFrame:NSZeroRect];
  view->_rnTag = tag;
  return view;
}

- (void)dealloc {
  CGImageRelease(_image);
  CGImageRelease(_imageBlurred);
  // An outset shadow's layer lives in the superlayer, so it outlives this view
  // unless it is taken out. Ordinarily -viewDidMoveToSuperview has already done
  // it; this is for a view released without being unmounted first.
  for (CALayer *layer in _boxShadowLayers) {
    [layer removeFromSuperlayer];
  }
  // The outline can be in the parent's layer for the same reason, so it has to
  // come out too.
  [_outlineLayer removeFromSuperlayer];
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self != nil) {
    _opacity = 1.0;
    _filterOpacity = 1.0;
    _imageFit = RnAppKitImageFitCover;
    _transform = CATransform3DIdentity;
    // Layer-backed from the start rather than on demand: a view that acquires a
    // layer later loses whatever was set on it before, and the props arrive in
    // whatever order the mutation stream happens to carry them.
    self.wantsLayer = YES;
    // Frames come from Fabric already resolved. Autoresizing would fight them.
    self.autoresizingMask = NSViewNotSizable;
    self.translatesAutoresizingMaskIntoConstraints = YES;
  }
  return self;
}

// See the note in the header: React Native's coordinates are top-left.
- (BOOL)isFlipped {
  return YES;
}

- (void)setRnFrameX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height {
  self.frame = NSMakeRect(x, y, width, height);
  // A mask layer is built against the bounds, so a resize invalidates it. The
  // uniform case is a plain cornerRadius and needs nothing.
  if (self.layer.mask != nil) {
    [self rnUpdateRadiusMask];
  }
  // Borders are drawn along the bounds too.
  if (_hasBorders) {
    self.needsDisplay = YES;
  }
  // And a box shadow's path is in the view's own coordinates, so every one of
  // them has to be rebuilt: a card that grew would otherwise keep the shadow of
  // the size it used to be. The outline is in the same position.
  if (!_boxShadows.empty()) {
    [self rnRebuildBoxShadowLayers];
  }
  if (_outlineWidth > 0) {
    [self rnRebuildOutlineLayer];
  }
}

- (void)setRnBackgroundColorRed:(CGFloat)red
                          green:(CGFloat)green
                           blue:(CGFloat)blue
                          alpha:(CGFloat)alpha
                       hasColor:(BOOL)hasColor {
  _hasBackgroundColor = hasColor;
  _backgroundComponents[0] = red;
  _backgroundComponents[1] = green;
  _backgroundComponents[2] = blue;
  _backgroundComponents[3] = alpha;

  if (!hasColor) {
    self.layer.backgroundColor = nil;
    return;
  }
  // sRGB explicitly. React Native's colour components are sRGB, and letting
  // CoreGraphics pick a device space shifts every colour slightly on a
  // wide-gamut display -- visible next to the same app on another platform,
  // which is exactly what this project is trying not to be.
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  const CGFloat components[4] = {red, green, blue, alpha};
  CGColorRef color = CGColorCreate(space, components);
  self.layer.backgroundColor = color;
  CGColorRelease(color);
  CGColorSpaceRelease(space);
}

// --- Accessibility ---------------------------------------------------------

// React Native's role vocabulary is mostly ARIA's; AppKit's is its own, older
// and smaller. Several entries below are the closest thing rather than an
// equivalent, and each says so -- an approximate role is better than none, and
// much better than a wrong one, which makes a control announce itself as
// something it is not.
static NSAccessibilityRole RnAccessibilityRoleFor(NSString *name) {
  static NSDictionary<NSString *, NSAccessibilityRole> *roles = nil;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    roles = @{
      @"button" : NSAccessibilityButtonRole,
      @"imagebutton" : NSAccessibilityButtonRole,
      @"link" : NSAccessibilityLinkRole,
      // A search field is a text field with a subrole on macOS, not a role of
      // its own. Reporting the role loses the "this searches" part, which is
      // what NSAccessibilitySearchFieldSubrole would carry -- a subrole needs a
      // second property, and this does not set one yet.
      @"search" : NSAccessibilityTextFieldRole,
      @"image" : NSAccessibilityImageRole,
      @"text" : NSAccessibilityStaticTextRole,
      @"adjustable" : NSAccessibilitySliderRole,
      @"checkbox" : NSAccessibilityCheckBoxRole,
      @"combobox" : NSAccessibilityComboBoxRole,
      @"menu" : NSAccessibilityMenuRole,
      @"menubar" : NSAccessibilityMenuBarRole,
      @"menuitem" : NSAccessibilityMenuItemRole,
      @"progressbar" : NSAccessibilityProgressIndicatorRole,
      @"radio" : NSAccessibilityRadioButtonRole,
      @"radiogroup" : NSAccessibilityRadioGroupRole,
      @"scrollbar" : NSAccessibilityScrollBarRole,
      @"spinbutton" : NSAccessibilityIncrementorRole,
      @"list" : NSAccessibilityListRole,
      @"grid" : NSAccessibilityTableRole,
      @"toolbar" : NSAccessibilityToolbarRole,
      @"tooltip" : NSAccessibilityHelpTagRole,
      // macOS has no toggle-button or switch role: VoiceOver announces both as
      // checkboxes, which is also what AppKit's own NSSwitch reports.
      @"togglebutton" : NSAccessibilityCheckBoxRole,
      @"switch" : NSAccessibilityCheckBoxRole,
      // A tab in an AX tab group is a radio button, which reads oddly and is
      // what every Mac application does.
      @"tab" : NSAccessibilityRadioButtonRole,
      @"tablist" : NSAccessibilityTabGroupRole,
      // No standalone header or alert role for a view. A group at least says
      // "these belong together", which a static text would not.
      @"header" : NSAccessibilityGroupRole,
      @"alert" : NSAccessibilityGroupRole,
      // Explicitly nothing: `none` and `presentation` mean "do not announce
      // this", which is handled by isAccessibilityElement below.
      @"none" : NSAccessibilityUnknownRole,
      @"presentation" : NSAccessibilityUnknownRole,
    };
  });
  NSAccessibilityRole role = roles[name];
  // Anything unrecognised is a group rather than a guess: a wrong role is worse
  // for a screen reader than a vague one, because it makes the view announce
  // itself as something it is not.
  return role != nil ? role : NSAccessibilityGroupRole;
}

- (void)setRnAccessibleRole:(NSString *)role {
  _roleName = role.length > 0 ? [role copy] : nil;

  if (_roleName == nil) {
    self.accessibilityRole = NSAccessibilityGroupRole;
    // A plain <View> is scenery. Leaving every one of them in the tree would
    // bury the handful that mean something under hundreds that do not.
    self.accessibilityElement = NO;
    return;
  }

  const NSAccessibilityRole mapped = RnAccessibilityRoleFor(_roleName);
  self.accessibilityRole = mapped;
  self.accessibilityElement = mapped != NSAccessibilityUnknownRole;
}

// The text of one view and everything under it, appended to `out`.
//
// Paragraphs only. A label can stand in for text, and the mounting manager
// prefers one before asking for this, as the GTK host does -- which keeps the
// rule in one place per host rather than in the view layer, where the props are
// not.
static void RnAppKitCollectText(RnAppKitView *view, NSMutableString *out) {
  NSString *own = view.rnOwnText;
  if (own.length > 0) {
    if (out.length > 0) {
      [out appendString:@" "];
    }
    [out appendString:own];
  }
  for (RnAppKitView *child in [view rnChildrenInPaintOrder]) {
    RnAppKitCollectText(child, out);
  }
}

- (NSString *)rnOwnText {
  return _textLayout.attributedString.string;
}

- (NSString *)rnCollectedText {
  NSMutableString *out = [NSMutableString string];
  RnAppKitCollectText(self, out);
  return out;
}

- (void)rnAnnounce:(NSString *)text assertive:(BOOL)assertive {
  if (text.length == 0) {
    return;
  }
  _lastAnnouncement = [text copy];
  // Posted against the application rather than this view: an announcement is not
  // about an element, and AppKit documents the app as the element to post it on.
  NSAccessibilityPostNotificationWithUserInfo(
      NSApp,
      NSAccessibilityAnnouncementRequestedNotification,
      @{
        NSAccessibilityAnnouncementKey : text,
        NSAccessibilityPriorityKey : @(assertive ? NSAccessibilityPriorityHigh
                                                 : NSAccessibilityPriorityMedium),
      });
}

- (NSString *)rnLastAnnouncement {
  return _lastAnnouncement;
}

- (void)setRnLabelledBy:(NSArray<RnAppKitView *> *)labels {
  _labelledBy.clear();
  for (RnAppKitView *label in labels) {
    _labelledBy.push_back(label.rnTag);
  }
  // The first, AppKit's property being one element rather than a list. Nil when
  // there is none, which is how an element says it has no such relation.
  self.accessibilityTitleUIElement = labels.firstObject;
  // And a view named by another is worth announcing: the label is on the other
  // view, so without this a field whose only name comes from its caption stays
  // out of the tree.
  if (labels.count > 0) {
    self.accessibilityElement = YES;
  }
}

- (void)setRnAccessibleLabel:(NSString *)label hint:(NSString *)hint {
  self.accessibilityLabel = label.length > 0 ? label : nil;
  // `accessibilityHelp`, not `accessibilityValue`: React Native's hint is the
  // supplementary description, which on a Mac is what a help tag carries.
  self.accessibilityHelp = hint.length > 0 ? hint : nil;

  // A label makes a view worth announcing even when it has no role -- which is
  // the common case for an icon-only <Pressable> with an accessibilityLabel.
  if (label.length > 0) {
    self.accessibilityElement = YES;
  }
}

// `accessibilityValue`, which React Native models as a range (min, max, now) and
// a text form that a screen reader prefers over the number.
//
// AppKit has `accessibilityMinValue` and `accessibilityMaxValue` for the range,
// and one `accessibilityValue` that carries either the number or the text.
// Setting nil is how an element says it has no such value, which matters because
// an absent part must stay absent: a view that never said what its range is must
// not be announced as sitting at the bottom of one.
//
// **This shares `accessibilityValue` with the checked state**, which AppKit
// expresses as an element's value the way a checkbox does. An explicit
// accessibilityValue wins, the app having said it outright, and the order below
// is what decides that: state is applied first and this second. Both reaching
// for the same property is an AppKit fact rather than a choice here.
- (void)setRnAccessibleValueMin:(NSNumber *)min
                            max:(NSNumber *)max
                            now:(NSNumber *)now
                           text:(NSString *)text {
  self.accessibilityMinValue = min;
  self.accessibilityMaxValue = max;

  // The text form first: "Tuesday" reads better than "3", which is the whole
  // reason React Native carries both.
  if (text.length > 0) {
    self.accessibilityValue = text;
  } else if (now != nil) {
    self.accessibilityValue = now;
  } else if (min != nil || max != nil) {
    // A range with no position in it. Leaving a stale value here would be worse
    // than leaving none.
    self.accessibilityValue = nil;
  }

  if (min != nil || max != nil || now != nil || text.length > 0) {
    // Worth announcing, the same reason a label alone makes a view an element.
    self.accessibilityElement = YES;
  }
}

- (void)setRnAccessibleStateDisabled:(RnAppKitAccessibleFlag)disabled
                             checked:(RnAppKitAccessibleFlag)checked
                            selected:(RnAppKitAccessibleFlag)selected
                            expanded:(RnAppKitAccessibleFlag)expanded
                                busy:(RnAppKitAccessibleFlag)busy {
  if (disabled != RnAppKitAccessibleUnset) {
    self.accessibilityEnabled = disabled != RnAppKitAccessibleTrue;
  }
  if (checked != RnAppKitAccessibleUnset) {
    // AppKit expresses checked as the element's value, the way a checkbox does,
    // rather than as a state of its own.
    self.accessibilityValue = @(checked == RnAppKitAccessibleTrue);
  }
  if (selected != RnAppKitAccessibleUnset) {
    self.accessibilitySelected = selected == RnAppKitAccessibleTrue;
  }
  if (expanded != RnAppKitAccessibleUnset) {
    self.accessibilityExpanded = expanded == RnAppKitAccessibleTrue;
  }
  // `busy` has no AppKit equivalent. React Native's meaning is "this is
  // loading", which VoiceOver has no way to be told about a plain view; the
  // nearest thing is an NSAccessibilityProgressIndicator, which would be a
  // different role rather than a state. Left unreported rather than mapped onto
  // something that means something else.
  (void)busy;
}

- (void)setRnAccessibleHidden:(BOOL)hidden {
  if (hidden) {
    self.accessibilityElement = NO;
  }
  // Not `else { YES }`: whether an unhidden view is an element is decided by
  // its role and label above, and overriding that here would put every plain
  // <View> back into the tree.
}

- (void)setRnScrollOffsetX:(CGFloat)x y:(CGFloat)y {
  const NSRect bounds = self.bounds;
  if (bounds.origin.x == x && bounds.origin.y == y) {
    return;
  }
  [self setBoundsOrigin:NSMakePoint(x, y)];
  // setBoundsOrigin: moves subviews but does not repaint what this view draws
  // itself, which matters the moment a ScrollView has a background or text.
  self.needsDisplay = YES;
}

- (NSPoint)rnScrollOffset {
  return self.bounds.origin;
}

- (void)setRnScrollIndicatorVerticalOffset:(CGFloat)verticalOffset
                            verticalLength:(CGFloat)verticalLength
                          horizontalOffset:(CGFloat)horizontalOffset
                          horizontalLength:(CGFloat)horizontalLength {
  if (verticalLength <= 0.0 && horizontalLength <= 0.0) {
    [_indicators removeFromSuperview];
    _indicators = nil;
    return;
  }

  if (_indicators == nil) {
    _indicators = [[RnAppKitScrollIndicatorView alloc] initWithFrame:self.bounds];
    [self addSubview:_indicators positioned:NSWindowAbove relativeTo:nil];
  }

  // `bounds` rather than a rect at the origin: a scrolled view's bounds origin
  // is the scroll offset, so this pins the overlay to the viewport instead of
  // letting it travel with the content.
  _indicators.frame = self.bounds;

  if (_indicators.rnVerticalOffset == verticalOffset &&
      _indicators.rnVerticalLength == verticalLength &&
      _indicators.rnHorizontalOffset == horizontalOffset &&
      _indicators.rnHorizontalLength == horizontalLength) {
    return;
  }
  _indicators.rnVerticalOffset = verticalOffset;
  _indicators.rnVerticalLength = verticalLength;
  _indicators.rnHorizontalOffset = horizontalOffset;
  _indicators.rnHorizontalLength = horizontalLength;
  _indicators.needsDisplay = YES;
}

static const char *RnAppKitImageFitName(RnAppKitImageFit fit) {
  switch (fit) {
    case RnAppKitImageFitContain:
      return "contain";
    case RnAppKitImageFitStretch:
      return "stretch";
    case RnAppKitImageFitCenter:
      return "center";
    case RnAppKitImageFitRepeat:
      return "repeat";
    case RnAppKitImageFitCover:
      break;
  }
  return "cover";
}

- (void)setRnImageTint:(NSColor *)tint {
  if (_imageTint == tint || [_imageTint isEqual:tint]) {
    return;
  }
  _imageTint = tint;
  [self setNeedsDisplay:YES];
}

- (void)setRnImageBlur:(CGFloat)radius {
  const CGFloat wanted = radius > 0 ? radius : 0;
  if (_imageBlur == wanted) {
    return;
  }
  _imageBlur = wanted;
  CGImageRelease(_imageBlurred);
  _imageBlurred = nullptr;
  [self setNeedsDisplay:YES];
}

// The blurred copy to draw in place of the image, or NULL when there is no blur
// or Core Image would not produce one.
//
// Kept on the view because `drawRect:` runs on every frame of a live resize and
// a Core Image round trip per frame is not free, where the GTK side's blur is a
// render node the GPU applies for nothing. The cache is keyed by the pixel size
// it was made at, and thrown away whenever the image or the radius changes.
- (CGImageRef)rnBlurredImageForSize:(CGSize)size scale:(CGFloat)scale {
  if (_image == nullptr || _imageBlur <= 0) {
    return nullptr;
  }
  const size_t width = (size_t)lround(size.width * scale);
  const size_t height = (size_t)lround(size.height * scale);
  if (width == 0 || height == 0) {
    return nullptr;
  }
  if (_imageBlurred != nullptr && CGImageGetWidth(_imageBlurred) == width &&
      CGImageGetHeight(_imageBlurred) == height) {
    return _imageBlurred;
  }
  CGImageRelease(_imageBlurred);
  _imageBlurred = RnBlurredImageCreate(_image, width, height, _imageBlur * scale / 2.0);
  return _imageBlurred;
}

- (void)setRnImage:(CGImageRef)image fit:(RnAppKitImageFit)fit {
  if (_image == image && _imageFit == fit) {
    return;
  }
  // Retained, not borrowed: the loader's cache owns one reference and this owns
  // another, so a view outliving an eviction still has pixels to draw.
  CGImageRef previous = _image;
  _image = image != nullptr ? CGImageRetain(image) : nullptr;
  CGImageRelease(previous);
  CGImageRelease(_imageBlurred);
  _imageBlurred = nullptr;
  _imageFit = fit;
  self.needsDisplay = YES;
}

// `image`, blurred, at `width` by `height` device pixels.
//
// Blurred at the size it will be drawn rather than in its own pixels, so that
// the radius is a distance on screen. Blurring in the source's pixels instead
// would make one prop almost invisible on a large photograph and overwhelming
// on an icon, and would not agree with the GTK side, which pushes a GSK blur
// node in widget coordinates.
//
// Sigma is half the radius, which is measured rather than chosen. A hard edge
// blurred by `gtk_snapshot_push_blur` comes out at sigma = 0.47 * radius under
// GTK's GL renderer, measured off a downloaded texture; React Native's own iOS
// path blurs with three box convolutions of
// `floor((radius * scale * 3 * sqrt(2 * pi) / 4 + 0.5) / 2) | 1`, whose variance
// works out to the same 0.47. Half the radius is what both of those agree on,
// within the few percent between a true gaussian and three boxes, and it is what
// the test here measures on the same kind of edge.
static CGImageRef RnBlurredImageCreate(CGImageRef image,
                                       size_t width,
                                       size_t height,
                                       CGFloat sigma) {
  CIImage *source = [CIImage imageWithCGImage:image];
  const CGRect extent = source.extent;
  if (extent.size.width <= 0 || extent.size.height <= 0) {
    return nullptr;
  }
  CIImage *scaled = [source
      imageByApplyingTransform:CGAffineTransformMakeScale((CGFloat)width / extent.size.width,
                                                          (CGFloat)height / extent.size.height)];
  // Clamped before the blur and cropped after it. Without the clamp the edges
  // blur out into transparency, so a `cover` photograph would gain a soft frame
  // it has on no other platform: iOS extends its edge pixels instead
  // (kvImageEdgeExtend) and GSK blurs inside the clip.
  CIImage *blurred =
      [[scaled imageByClampingToExtent] imageByApplyingGaussianBlurWithSigma:sigma];
  const CGRect box = CGRectMake(0, 0, (CGFloat)width, (CGFloat)height);

  // One context for the process: building a CIContext is the expensive part of
  // Core Image, not running the filter.
  //
  // Colour management off, which is the difference between this looking like the
  // other two platforms and not. Core Image converts to linear light by default
  // and blurs there; GSK blurs the encoded pixels, and React Native's iOS path
  // runs vImageBoxConvolve over 8-bit sRGB bytes. Measured on a black-to-white
  // edge, the managed blur spreads about 1.2 times as far as either. Unmanaged,
  // the filter works on the sample values as given, like both of them.
  static CIContext *renderer = nil;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    renderer = [CIContext contextWithOptions:@{kCIContextWorkingColorSpace : (id)[NSNull null]}];
  });
  return [renderer createCGImage:blurred fromRect:box];
}

// Where the image lands inside the frame.
//
// The same arithmetic as the GTK side's, deliberately -- two platforms
// disagreeing about what `resizeMode: 'contain'` means would be the sort of
// difference nobody thinks to check. Duplicated rather than shared because the
// view layers link no common code at all, which is what keeps them testable
// without React Native.
- (NSRect)rnImageRectForSize:(NSSize)size {
  const CGFloat imageWidth = (CGFloat)CGImageGetWidth(_image);
  const CGFloat imageHeight = (CGFloat)CGImageGetHeight(_image);
  if (_imageFit == RnAppKitImageFitStretch || imageWidth <= 0 || imageHeight <= 0) {
    return NSMakeRect(0, 0, size.width, size.height);
  }

  CGFloat scale = 1.0;
  switch (_imageFit) {
    case RnAppKitImageFitContain:
      scale = MIN(size.width / imageWidth, size.height / imageHeight);
      break;
    case RnAppKitImageFitCover:
      scale = MAX(size.width / imageWidth, size.height / imageHeight);
      break;
    case RnAppKitImageFitCenter:
      // Centre at natural size, but never larger than the frame -- which is
      // what React Native's `center` does.
      scale = MIN(1.0, MIN(size.width / imageWidth, size.height / imageHeight));
      break;
    case RnAppKitImageFitRepeat:
      // One tile, at natural size. Where it is placed and how many there are
      // is the draw's business, not this function's.
      break;
    case RnAppKitImageFitStretch:
      break;
  }

  const CGFloat width = imageWidth * scale;
  const CGFloat height = imageHeight * scale;
  return NSMakeRect((size.width - width) / 2.0, (size.height - height) / 2.0, width, height);
}

- (void)setRnTextLayout:(id)layout {
  _textLayout = (RnTextLayout *)layout;
  // A layer-backed view with a drawRect: gets its contents from that draw, and
  // only redraws when told. Without this the first paragraph appears and no
  // later one ever does.
  self.needsDisplay = YES;
}

// Only views with text draw anything; the rest are pure CALayer properties and
// this is never called for them.
//
// The background is *not* drawn here. It is a layer property, which means Core
// Animation paints it under this and a view can have both without either
// knowing about the other.
- (void)setRnHighlights:(const float *)rectangles
                 filled:(const bool *)filled
                  count:(NSInteger)count {
  _highlights.clear();
  _highlightFilled.clear();
  if (rectangles != nullptr && count > 0) {
    _highlights.assign(rectangles, rectangles + (count * 8));
    _highlightFilled.assign(filled, filled + count);
  }
  self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  const BOOL ring = [self rnShowsFocusRing];
  if (_textLayout == nil && _image == nullptr && !_hasBorders && !ring &&
      _highlightFilled.empty() && _gradients.empty()) {
    return;
  }
  CGContextRef context = [NSGraphicsContext currentContext].CGContext;
  const NSSize size = self.bounds.size;

  // The background image, under everything this view draws and over the
  // background colour, which is a layer property and so is painted below all of
  // this. That is CSS's order.
  [self rnDrawGradientsInContext:context size:size];

  if (_image != nullptr) {
    const NSRect destination = [self rnImageRectForSize:size];
    // A blurred copy stands in for the image below, including as the tint's
    // mask: blurring an alpha mask and filling it with one solid colour is the
    // same picture as blurring the fill, because the colour is constant. That
    // is what makes this match the GTK side, which pushes its blur around the
    // tint rather than around the silhouette it is masked from.
    //
    // Made at the destination's size, which for `repeat` is one tile, so every
    // tile blurs alike as it does on GTK. The device scale comes from the
    // context rather than from the window, because a view drawn into a bitmap
    // -- a test, a snapshot -- has no window to ask.
    CGImageRef pixels = _image;
    if (_imageBlur > 0) {
      const CGFloat deviceScale =
          CGContextConvertSizeToDeviceSpace(context, CGSizeMake(1, 1)).width;
      CGImageRef blurred = [self rnBlurredImageForSize:destination.size
                                                scale:deviceScale > 0 ? deviceScale : 1];
      if (blurred != nullptr) {
        pixels = blurred;
      }
    }

    CGContextSaveGState(context);
    // cover, center and repeat can all put pixels outside the frame, and an
    // <Image> never paints beyond its own box on iOS or Android.
    if (_imageFit != RnAppKitImageFitContain && _imageFit != RnAppKitImageFitStretch) {
      CGContextClipToRect(context, NSMakeRect(0, 0, size.width, size.height));
    }
    // CGImage draws bottom-up and this view is flipped, so without this every
    // photograph comes out upside down -- which reads as a broken decoder
    // rather than a coordinate system.
    CGContextTranslateCTM(context, 0, size.height);
    CGContextScaleCTM(context, 1, -1);
    const NSRect drawn = NSMakeRect(destination.origin.x,
                                    size.height - destination.origin.y - destination.size.height,
                                    destination.size.width,
                                    destination.size.height);
    if (_imageFit == RnAppKitImageFitRepeat) {
      // Tiled from the top left at natural size. `CGContextDrawTiledImage`
      // takes the *first* tile's rect and repeats it across the clip, which is
      // why the clip above is what stops it painting the whole window.
      //
      // The first tile is at the top of the view, which in this flipped
      // context is `size.height - tileHeight` -- so a box that is not a whole
      // number of tiles high has its partial tile at the bottom, as CSS
      // `repeat` does, rather than at the top.
      const NSRect tile = NSMakeRect(0,
                                     size.height - destination.size.height,
                                     destination.size.width,
                                     destination.size.height);
      if (_imageTint != nil) {
        CGContextClipToMask(context, tile, pixels);
        CGContextSetFillColorWithColor(context, _imageTint.CGColor);
        CGContextFillRect(context, NSMakeRect(0, 0, size.width, size.height));
      } else {
        CGContextDrawTiledImage(context, tile, pixels);
      }
    } else if (_imageTint != nil) {
      // The image becomes a stencil and the colour is what is drawn. Clipping
      // to the mask uses the image's alpha, which is what `tintColor` means:
      // recolour the silhouette rather than blend with the pixels.
      CGContextClipToMask(context, drawn, pixels);
      CGContextSetFillColorWithColor(context, _imageTint.CGColor);
      CGContextFillRect(context, drawn);
    } else {
      CGContextDrawImage(context, drawn, pixels);
    }
    CGContextRestoreGState(context);
  }

  // Text sits above the image and below any children, which is the order the
  // GTK side paints in too.
  if (_textLayout != nil) {
    [_textLayout drawInContext:context size:size];
  }

  // Borders paint over the content, as they do on every other platform.
  if (_hasBorders) {
    [self rnDrawBordersInContext:context size:size];
  }

  // The focus ring, over everything including the border, because it is the
  // answer to "where am I" and must not be hidden by what it is drawn on.
  //
  // Inside the bounds rather than outside: drawRect: is clipped to them, and a
  // ring that is inset here and outset on Linux would be a difference an app
  // did not ask for. It follows the view's own corner radii, so it hugs a
  // rounded button.
  // React DevTools' overlay, over everything including the border. Above the
  // app on purpose: it is not part of it, and an inspected element half hidden
  // behind a card would be pointing at the wrong thing.
  for (size_t i = 0; i < _highlightFilled.size(); i++) {
    const float *rectangle = _highlights.data() + (i * 8);
    const NSRect area = NSMakeRect(rectangle[0], rectangle[1], rectangle[2], rectangle[3]);
    if (_highlightFilled[i]) {
      CGContextSetRGBFillColor(context, rectangle[4], rectangle[5], rectangle[6], rectangle[7]);
      CGContextFillRect(context, area);
    }
    // Opaque on the outline even when the fill is not, so the edge of an
    // inspected element is a line rather than a suggestion. Stroked down the
    // middle, so the path is inset by half the width to keep it inside.
    CGContextSetRGBStrokeColor(context, rectangle[4], rectangle[5], rectangle[6], 1.0);
    CGContextSetLineWidth(context, basalt::kHighlightBorderWidth);
    const CGFloat inset = basalt::kHighlightBorderWidth / 2.0;
    CGContextStrokeRect(context, NSInsetRect(area, inset, inset));
  }

  if (ring) {
    [self rnDrawFocusRingInContext:context size:size];
  }
}

- (void)rnDrawFocusRingInContext:(CGContextRef)context size:(NSSize)size {
  const CGFloat width = basalt::kFocusRingWidth;
  // Stroked down the middle of the line, so the path is inset by half of it to
  // keep the whole ring inside the view.
  const NSRect rect = NSMakeRect(width / 2.0, width / 2.0,
                                 MAX(0.0, size.width - width),
                                 MAX(0.0, size.height - width));
  CGFloat radii[8];
  for (int i = 0; i < 8; i++) {
    radii[i] = _hasBorderRadii ? MAX(0.0, _borderRadii[i] - width / 2.0) : 0.0;
  }
  CGPathRef path = RnAppKitCreateRoundedPath(rect, radii);
  CGContextSaveGState(context);
  CGContextSetRGBStrokeColor(context,
                             basalt::kFocusRingRed,
                             basalt::kFocusRingGreen,
                             basalt::kFocusRingBlue,
                             basalt::kFocusRingAlpha);
  CGContextSetLineWidth(context, width);
  CGContextAddPath(context, path);
  CGContextStrokePath(context);
  CGContextRestoreGState(context);
  CGPathRelease(path);
}

// A dotted or dashed border: one stroked path instead of four filled edges.
//
// The same shape of drawing as the GTK side's, down to the dash pattern, because
// a dashed border that started its dashes in a different place on each desktop
// would be a difference nobody asked for. The top edge's width and colour decide
// the stroke: a stroked path has one pen.
- (void)rnStrokeDashedBorderInContext:(CGContextRef)context size:(NSSize)size {
  const CGFloat width = _borderWidths[0] > 0 ? _borderWidths[0] : 1.0;

  // Inset by half the width, because a stroke straddles its path while the
  // filled edges sit inside the box. Without that a 4pt dashed border would
  // paint two points outside the view and over its neighbour.
  //
  // The corner radii are left as they are rather than shrunk by the inset, which
  // is what GSK's own inset does on the other host: it pulls the corners in by
  // about a third of the inset, and matching it keeps the two drawing one shape.
  const CGRect centred = CGRectInset(CGRectMake(0, 0, size.width, size.height),
                                     width / 2.0, width / 2.0);
  if (centred.size.width <= 0 || centred.size.height <= 0) {
    return;
  }
  CGPathRef path = RnAppKitCreateRoundedPath(centred, _borderRadii);

  CGContextSaveGState(context);
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  const CGFloat components[4] = {_borderColors[0], _borderColors[1], _borderColors[2],
                                 _borderColors[3]};
  CGColorRef color = CGColorCreate(space, components);
  CGContextSetStrokeColorWithColor(context, color);
  CGContextSetLineWidth(context, width);

  // Scaled to the width, the way a browser does it. Dotted is round caps on a
  // zero-length dash, which is what makes a dot round rather than a short dash.
  if (_borderStyle == RnAppKitBorderStyleDotted) {
    const CGFloat dots[2] = {0, width * 2.0};
    CGContextSetLineCap(context, kCGLineCapRound);
    CGContextSetLineDash(context, 0, dots, 2);
  } else {
    const CGFloat dashes[2] = {width * 3.0, width * 2.0};
    CGContextSetLineDash(context, 0, dashes, 2);
  }

  CGContextAddPath(context, path);
  CGContextStrokePath(context);

  CGColorRelease(color);
  CGColorSpaceRelease(space);
  CGContextRestoreGState(context);
  CGPathRelease(path);
}

// The four edges, each its own width and colour.
//
// This is CSS's own algorithm rather than a stroke: an edge is the region
// between the outer and inner rounded rectangles, clipped to a wedge whose
// sides are the diagonals from the outer corner to the inner one. That is what
// mitres two edges of different colours against each other, and a stroke
// cannot do it -- which matters as soon as one edge differs, and is invisible
// until then.
- (void)rnDrawBordersInContext:(CGContextRef)context size:(NSSize)size {
  if (_borderStyle != RnAppKitBorderStyleSolid) {
    [self rnStrokeDashedBorderInContext:context size:size];
    return;
  }
  const CGFloat w = size.width;
  const CGFloat h = size.height;
  const CGFloat top = _borderWidths[0];
  const CGFloat right = _borderWidths[1];
  const CGFloat bottom = _borderWidths[2];
  const CGFloat left = _borderWidths[3];

  CGPathRef outer = RnAppKitCreateRoundedPath(CGRectMake(0, 0, w, h), _borderRadii);

  // The inner radii shrink by the width of the edges that meet at the corner.
  // A corner whose radius is smaller than its border is square on the inside,
  // which the clamp in the path builder takes care of.
  const CGFloat innerRadii[8] = {
      _borderRadii[0] - left, _borderRadii[1] - top,
      _borderRadii[2] - right, _borderRadii[3] - top,
      _borderRadii[4] - right, _borderRadii[5] - bottom,
      _borderRadii[6] - left, _borderRadii[7] - bottom,
  };
  const CGFloat innerWidth = w - left - right;
  const CGFloat innerHeight = h - top - bottom;
  CGPathRef inner = nullptr;
  if (innerWidth > 0 && innerHeight > 0) {
    inner = RnAppKitCreateRoundedPath(
        CGRectMake(left, top, innerWidth, innerHeight), innerRadii);
  }

  // The diagonal at each corner, as a point and a direction, and a point known
  // to be inside each edge's own band.
  //
  // Deliberately not four quadrilaterals stopping at the inner rectangle. That
  // is right only while the corners are square: a radius pushes the border band
  // outside those quads, and the part of the arc beyond them is then inside no
  // wedge at all and never painted. What that looks like is not a missing
  // sliver -- it is a corner whose colour stops early, which reads as a chamfer
  // rather than as a bug.
  //
  // Two half-plane clips per edge instead. Successive clips intersect, so the
  // pair is the sector the edge owns, and a sector is unbounded: it covers the
  // whole arc however large the radius. It also stays correct when the two
  // diagonals cross inside the view, which a quadrilateral cannot -- that
  // happens whenever the border is thick relative to the frame, and it turns a
  // quad into a bowtie.
  const CGPoint corners[4] = {{0, 0}, {w, 0}, {w, h}, {0, h}};
  const CGPoint directions[4] = {
      {left, top}, {-right, top}, {-right, -bottom}, {left, -bottom}};
  // Edge e is bounded by the diagonals at corner e and corner e+1.
  const CGPoint insides[4] = {
      {w / 2, top / 2},
      {w - right / 2, h / 2},
      {w / 2, h - bottom / 2},
      {left / 2, h / 2},
  };
  const CGFloat extent = (w + h) * 4;

  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  for (int edge = 0; edge < 4; edge++) {
    if (_borderWidths[edge] <= 0 || _borderColors[edge * 4 + 3] <= 0) {
      continue;
    }
    CGContextSaveGState(context);

    RnAppKitClipToHalfPlane(context, corners[edge], directions[edge], insides[edge], extent);
    RnAppKitClipToHalfPlane(
        context, corners[(edge + 1) % 4], directions[(edge + 1) % 4], insides[edge], extent);

    // Outer minus inner, as an even-odd fill of the two subpaths.
    CGContextBeginPath(context);
    CGContextAddPath(context, outer);
    if (inner != nullptr) {
      CGContextAddPath(context, inner);
    }
    const CGFloat components[4] = {
        _borderColors[edge * 4 + 0], _borderColors[edge * 4 + 1],
        _borderColors[edge * 4 + 2], _borderColors[edge * 4 + 3]};
    CGColorRef color = CGColorCreate(space, components);
    CGContextSetFillColorWithColor(context, color);
    CGContextEOFillPath(context);
    CGColorRelease(color);

    CGContextRestoreGState(context);
  }
  CGColorSpaceRelease(space);
  CGPathRelease(outer);
  if (inner != nullptr) {
    CGPathRelease(inner);
  }
}

- (void)setRnOpacity:(CGFloat)opacity {
  _opacity = opacity;
  [self rnApplyOpacity];
}

// The view's own opacity and any `opacity()` in its filter list, which CSS lets
// an app ask for in both places at once.
- (void)rnApplyOpacity {
  self.layer.opacity = (float)(_opacity * _filterOpacity);
}

- (void)setRnHidesBackFace:(BOOL)hides {
  if (_hidesBackFace == hides) {
    return;
  }
  _hidesBackFace = hides;
  [self rnUpdateBackFaceVisibility];
}

// Hidden through `self.hidden` rather than by skipping the draw: it takes the
// children with it, which is what a back face should do, and it takes hit
// testing with it too -- the back of a card is not clickable.
//
// The two reasons are kept apart and combined here. `self.hidden` is one flag
// and `display: none` writes it too, so whichever of the two spoke last used
// to answer for both -- and layout metrics are applied after props, so a card
// turned away from the viewer was reliably un-hidden a moment later.
- (void)rnApplyVisibility {
  self.hidden = (_hiddenByApp || _hiddenByBackFace) ? YES : NO;
}

- (void)setRnHidden:(BOOL)hidden {
  _hiddenByApp = hidden;
  [self rnApplyVisibility];
}

- (void)rnUpdateBackFaceVisibility {
  BOOL away = NO;
  if (_hidesBackFace) {
    float matrix[16];
    const CGFloat *fields = (const CGFloat *)&_transform;
    for (int index = 0; index < 16; index++) {
      matrix[index] = (float)fields[index];
    }
    away = basalt::facesAway(matrix) ? YES : NO;
  }
  // Recomputed rather than returned early on, so that turning the prop off
  // shows a view it had hidden instead of leaving it hidden forever.
  _hiddenByBackFace = away;
  [self rnApplyVisibility];
}

- (void)setRnTransform:(nullable const float *)matrix {
  if (matrix == nullptr) {
    _hasTransform = NO;
    _transform = CATransform3DIdentity;
    self.layer.transform = CATransform3DIdentity;
    [self rnUpdateBackFaceVisibility];
    return;
  }

  // Field by field rather than a memcpy: CATransform3D's members are CGFloat,
  // which is not guaranteed to be double, and the layout is only documented as
  // "m11 through m44 in this order".
  CATransform3D transform;
  CGFloat *fields = (CGFloat *)&transform;
  for (int index = 0; index < 16; index++) {
    fields[index] = (CGFloat)matrix[index];
  }

  _transform = transform;
  _hasTransform = !CATransform3DIsIdentity(transform);
  [self rnUpdateBackFaceVisibility];
  self.layer.transform = transform;
}

- (void)setRnClipsChildren:(BOOL)clips {
  _clipsChildren = clips;
  self.layer.masksToBounds = clips;
  // Which decides where an outset shadow and an outline live, so both move with
  // it.
  if (!_boxShadows.empty()) {
    [self rnRebuildBoxShadowLayers];
  }
  if (_outlineWidth > 0) {
    [self rnRebuildOutlineLayer];
  }
}

- (void)setRnZIndex:(NSInteger)zIndex {
  _zIndex = zIndex;
  // Core Animation composites sibling layers by zPosition, so painting reorders
  // without the subviews array moving -- and the subviews array is the child
  // list Fabric indexes into. Reordering it to paint would corrupt the next
  // Insert.
  self.layer.zPosition = (CGFloat)zIndex;
}

- (NSInteger)rnZIndex {
  return _zIndex;
}

- (CGAffineTransform)rnLocalToParent {
  const NSRect frame = self.frame;
  const CGAffineTransform translation =
      CGAffineTransformMakeTranslation(frame.origin.x, frame.origin.y);

  // `CATransform3DIsAffine` is false for a perspective transform, which has no
  // 2D inverse to hit test with. React Native can express one; nothing here
  // draws it yet -- see docs/backlog/correctness.md -- and answering with the
  // translation keeps such a view clickable at its untransformed place rather
  // than nowhere at all.
  if (!_hasTransform || !CATransform3DIsAffine(_transform)) {
    return translation;
  }

  // Anchored at the centre, because that is where CALayer's default anchor
  // point is and so where the drawn transform turns. Written as move the
  // centre to the origin, transform, move it back, then place the frame --
  // the same four steps, in the same order, as Win32's `localToParent`.
  const CGFloat centreX = frame.size.width / 2.0;
  const CGFloat centreY = frame.size.height / 2.0;

  CGAffineTransform local = CGAffineTransformMakeTranslation(-centreX, -centreY);
  local = CGAffineTransformConcat(local, CATransform3DGetAffineTransform(_transform));
  local = CGAffineTransformConcat(local, CGAffineTransformMakeTranslation(centreX, centreY));
  return CGAffineTransformConcat(local, translation);
}

- (BOOL)rnPageToLocal:(NSPoint)page fromRoot:(RnAppKitView *)root into:(NSPoint *)out {
  CGAffineTransform toPage = CGAffineTransformIdentity;

  for (RnAppKitView *node = self; node != nil && node != root;) {
    toPage = CGAffineTransformConcat(toPage, [node rnLocalToParent]);

    RnAppKitView *parent =
        [node.superview isKindOfClass:[RnAppKitView class]] ? (RnAppKitView *)node.superview : nil;
    if (parent == nil) {
      break;
    }
    // A scrolled ancestor moves its children by its bounds origin, which is
    // not part of any child's own placement -- the same offset the hit test
    // adds back on the way down.
    const NSPoint scroll = parent.bounds.origin;
    toPage =
        CGAffineTransformConcat(toPage, CGAffineTransformMakeTranslation(-scroll.x, -scroll.y));
    node = parent;
  }

  const CGFloat determinant = toPage.a * toPage.d - toPage.b * toPage.c;
  if (determinant == 0.0) {
    return NO;
  }
  if (out != NULL) {
    *out = CGPointApplyAffineTransform(page, CGAffineTransformInvert(toPage));
  }
  return YES;
}

- (NSArray<RnAppKitView *> *)rnChildrenInPaintOrder {
  NSMutableArray<RnAppKitView *> *ordered = [NSMutableArray array];
  for (NSView *child in self.subviews) {
    if ([child isKindOfClass:[RnAppKitView class]]) {
      [ordered addObject:(RnAppKitView *)child];
    }
  }
  // Only when something asked for it. The overwhelmingly common view has no
  // zIndex anywhere among its children, and sorting every tree on every hit
  // test to discover that would be paid on every press.
  BOOL any = NO;
  for (RnAppKitView *child in ordered) {
    if (child.rnZIndex != 0) {
      any = YES;
      break;
    }
  }
  if (!any) {
    return ordered;
  }
  // Stable, so equal zIndex keeps document order.
  return [ordered sortedArrayWithOptions:NSSortStable
                         usingComparator:^NSComparisonResult(RnAppKitView *a, RnAppKitView *b) {
                           if (a.rnZIndex < b.rnZIndex) return NSOrderedAscending;
                           if (a.rnZIndex > b.rnZIndex) return NSOrderedDescending;
                           return NSOrderedSame;
                         }];
}

- (void)setRnCornerRadius:(CGFloat)radius {
  const CGFloat radii[8] = {radius, radius, radius, radius,
                            radius, radius, radius, radius};
  [self setRnBorderRadii:radii];
}

- (void)setRnBorderRadii:(nullable const CGFloat *)radii {
  BOOL any = NO;
  for (int i = 0; i < 8; i++) {
    _borderRadii[i] = radii != nullptr && radii[i] > 0 ? radii[i] : 0;
    if (_borderRadii[i] > 0) {
      any = YES;
    }
  }
  _hasBorderRadii = any;

  // A single circular radius is what CALayer can express directly, and is the
  // overwhelmingly common case -- one `borderRadius` in a stylesheet. Keeping
  // it on cornerRadius rather than on a mask means the usual view stays a
  // plain layer, and means `overflow: 'visible'` still lets children escape.
  BOOL uniform = YES;
  for (int i = 1; i < 8; i++) {
    if (_borderRadii[i] != _borderRadii[0]) {
      uniform = NO;
      break;
    }
  }

  if (uniform) {
    _cornerRadius = _borderRadii[0];
    self.layer.cornerRadius = _borderRadii[0];
    self.layer.mask = nil;
  } else {
    // Anything else needs a mask layer, which is what UIKit's own RCTView does
    // for the same reason. The cost is that a mask clips children whatever
    // `overflow` says -- CALayer offers no way to round a background without
    // clipping what sits on it.
    _cornerRadius = 0;
    self.layer.cornerRadius = 0;
    [self rnUpdateRadiusMask];
  }
  // The border is drawn along these radii.
  self.needsDisplay = YES;
  // And so is every shadow: a shadow follows the box it is cast by, and so does
  // the outline.
  if (!_boxShadows.empty()) {
    [self rnRebuildBoxShadowLayers];
  }
  if (_outlineWidth > 0) {
    [self rnRebuildOutlineLayer];
  }
}

- (void)rnUpdateRadiusMask {
  if (!_hasBorderRadii) {
    self.layer.mask = nil;
    return;
  }
  CAShapeLayer *mask = [CAShapeLayer layer];
  mask.frame = self.bounds;
  CGPathRef path = RnAppKitCreateRoundedPath(self.bounds, _borderRadii);
  mask.path = path;
  CGPathRelease(path);
  self.layer.mask = mask;
}

- (void)setRnBorderWidths:(nullable const CGFloat *)widths
                   colors:(nullable const CGFloat *)colors {
  BOOL any = NO;
  for (int edge = 0; edge < 4; edge++) {
    _borderWidths[edge] = widths != nullptr && widths[edge] > 0 ? widths[edge] : 0;
    for (int c = 0; c < 4; c++) {
      _borderColors[edge * 4 + c] = colors != nullptr ? colors[edge * 4 + c] : 0;
    }
    // A width with a transparent colour paints nothing, and the GTK side makes
    // the same judgement, so the two agree on whether a view has a border at
    // all rather than only on what it looks like.
    if (_borderWidths[edge] > 0 && _borderColors[edge * 4 + 3] > 0) {
      any = YES;
    }
  }
  _hasBorders = any;
  self.needsDisplay = YES;
}

// The NSCursor for a CSS cursor keyword, or nil for one macOS has no cursor for.
//
// The mapping React Native's own macOS fork makes, which is worth matching to the
// letter: an app moved from react-native-macos to this platform should get the
// same pointer over the same view. That fork is also where the holes come from --
// "all-scroll", "cell", "help", "move", "progress" and "wait" have no NSCursor,
// public or otherwise, and nil here means the view installs no cursor rect, so
// the cursor is inherited rather than forced back to an arrow.
//
// The resize family is the one place macOS 15 made this better: before it, the
// only resize cursors were the four axis-aligned ones, so a corner handle had
// nothing to show. `frameResizeCursorFromPosition:inDirections:` has the
// diagonals, and the fallbacks below are what the older system can do.
static NSCursor *RnAppKitCursorNamed(NSString *name) {
  if ([name isEqualToString:@"default"]) {
    return NSCursor.arrowCursor;
  }
  if ([name isEqualToString:@"pointer"]) {
    return NSCursor.pointingHandCursor;
  }
  if ([name isEqualToString:@"text"]) {
    return NSCursor.IBeamCursor;
  }
  if ([name isEqualToString:@"crosshair"]) {
    return NSCursor.crosshairCursor;
  }
  if ([name isEqualToString:@"grab"]) {
    return NSCursor.openHandCursor;
  }
  if ([name isEqualToString:@"grabbing"]) {
    return NSCursor.closedHandCursor;
  }
  if ([name isEqualToString:@"alias"]) {
    return NSCursor.dragLinkCursor;
  }
  if ([name isEqualToString:@"copy"]) {
    return NSCursor.dragCopyCursor;
  }
  if ([name isEqualToString:@"context-menu"]) {
    return NSCursor.contextualMenuCursor;
  }
  // CSS distinguishes "this is not a drop target" from "this is not allowed at
  // all"; macOS has one cursor for both, as does react-native-macos.
  if ([name isEqualToString:@"no-drop"] || [name isEqualToString:@"not-allowed"]) {
    return NSCursor.operationNotAllowedCursor;
  }
  // An empty one-pixel image, which is how macOS hides the pointer over a view.
  if ([name isEqualToString:@"none"]) {
    static NSCursor *blank = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      blank = [[NSCursor alloc] initWithImage:[[NSImage alloc] initWithSize:NSMakeSize(1, 1)]
                                      hotSpot:NSZeroPoint];
    });
    return blank;
  }
  if ([name isEqualToString:@"ew-resize"] || [name isEqualToString:@"e-resize"] ||
      [name isEqualToString:@"w-resize"]) {
    return NSCursor.resizeLeftRightCursor;
  }
  if ([name isEqualToString:@"ns-resize"] || [name isEqualToString:@"n-resize"] ||
      [name isEqualToString:@"s-resize"]) {
    return NSCursor.resizeUpDownCursor;
  }
  if ([name isEqualToString:@"col-resize"]) {
    if (@available(macOS 15.0, *)) {
      return NSCursor.columnResizeCursor;
    }
    return NSCursor.resizeLeftRightCursor;
  }
  if ([name isEqualToString:@"row-resize"]) {
    if (@available(macOS 15.0, *)) {
      return NSCursor.rowResizeCursor;
    }
    return NSCursor.resizeUpDownCursor;
  }
  if (@available(macOS 15.0, *)) {
    // The diagonals, which only exist as frame-resize cursors. "nesw" and "nwse"
    // are the two-headed pair and the single directions are one-headed, which is
    // what Outward and All mean here.
    if ([name isEqualToString:@"nesw-resize"]) {
      return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopRight
                                        inDirections:NSCursorFrameResizeDirectionsAll];
    }
    if ([name isEqualToString:@"nwse-resize"]) {
      return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopLeft
                                        inDirections:NSCursorFrameResizeDirectionsAll];
    }
    if ([name isEqualToString:@"ne-resize"]) {
      return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopRight
                                        inDirections:NSCursorFrameResizeDirectionsOutward];
    }
    if ([name isEqualToString:@"nw-resize"]) {
      return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopLeft
                                        inDirections:NSCursorFrameResizeDirectionsOutward];
    }
    if ([name isEqualToString:@"se-resize"]) {
      return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionBottomRight
                                        inDirections:NSCursorFrameResizeDirectionsOutward];
    }
    if ([name isEqualToString:@"sw-resize"]) {
      return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionBottomLeft
                                        inDirections:NSCursorFrameResizeDirectionsOutward];
    }
    if ([name isEqualToString:@"zoom-in"]) {
      return NSCursor.zoomInCursor;
    }
    if ([name isEqualToString:@"zoom-out"]) {
      return NSCursor.zoomOutCursor;
    }
  }
  return nil;
}

- (void)setRnCursorName:(NSString *)name {
  NSString *wanted = name.length > 0 ? name : nil;
  if (_cursorName == wanted || [_cursorName isEqualToString:wanted]) {
    return;
  }
  _cursorName = [wanted copy];
  _cursor = wanted != nil ? RnAppKitCursorNamed(wanted) : nil;
  // Cursor rects are cached by the window and only rebuilt when it is told to,
  // so a prop that changes while the pointer is already inside the view would
  // otherwise keep the old cursor until the pointer left and came back.
  [self.window invalidateCursorRectsForView:self];
}

- (NSCursor *)rnResolvedCursor {
  return _cursor;
}

// `mixBlendMode`, which on macOS is one property: a Core Image blend-mode filter
// on the layer, blending it with whatever is already beneath it.
//
// Public API here, as `CALayer.filters` is, and private on iOS -- React Native's
// own iOS half sets `compositingFilter` to a bare string, which is undocumented
// there. This sets the `CIFilter` the property is documented to take.
//
// The CSS names map one to one, `plus-lighter` included: it is linear dodge,
// which is clamped addition, and that is what the keyword means.
static NSString *RnAppKitBlendFilterNamed(NSString *keyword) {
  static NSDictionary<NSString *, NSString *> *names = nil;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    names = @{
      @"multiply" : @"CIMultiplyBlendMode",
      @"screen" : @"CIScreenBlendMode",
      @"overlay" : @"CIOverlayBlendMode",
      @"darken" : @"CIDarkenBlendMode",
      @"lighten" : @"CILightenBlendMode",
      @"color-dodge" : @"CIColorDodgeBlendMode",
      @"color-burn" : @"CIColorBurnBlendMode",
      @"hard-light" : @"CIHardLightBlendMode",
      @"soft-light" : @"CISoftLightBlendMode",
      @"difference" : @"CIDifferenceBlendMode",
      @"exclusion" : @"CIExclusionBlendMode",
      @"hue" : @"CIHueBlendMode",
      @"saturation" : @"CISaturationBlendMode",
      @"color" : @"CIColorBlendMode",
      @"luminosity" : @"CILuminosityBlendMode",
      @"plus-lighter" : @"CILinearDodgeBlendMode",
    };
  });
  return names[keyword];
}

- (void)setRnBlendModeName:(NSString *)name {
  NSString *wanted = name.length > 0 ? name : nil;
  if (_blendModeName == wanted || [_blendModeName isEqualToString:wanted]) {
    return;
  }
  _blendModeName = [wanted copy];
  _blendFilterName = wanted != nil ? RnAppKitBlendFilterNamed(wanted) : nil;
  // A keyword Core Image has no filter for leaves the layer unblended rather
  // than wrongly blended, and the keyword is still reported: the dump says what
  // the app asked for, which is the rule the cursor name follows too.
  self.layer.compositingFilter =
      _blendFilterName != nil ? [CIFilter filterWithName:_blendFilterName] : nil;
}

- (NSString *)rnBlendModeName {
  return _blendModeName;
}

- (NSString *)rnBlendFilterName {
  return _blendFilterName;
}

// AppKit asks for the rects rather than being told, which is why the cursor is
// held and applied here: one rect over the whole view, discarded and rebuilt
// each time, as react-native-macos does it. A child with its own cursor adds its
// own rect and wins inside its own bounds, which is what CSS does.
- (void)resetCursorRects {
  [self discardCursorRects];
  if (_cursor != nil) {
    [self addCursorRect:self.bounds cursor:_cursor];
  }
}

- (void)setRnGradients:(const RnAppKitGradient *)gradients count:(NSInteger)count {
  std::vector<RnAppKitGradientRecord> wanted;
  if (gradients != nullptr && count > 0) {
    wanted.reserve((size_t)count);
    for (NSInteger i = 0; i < count; i++) {
      RnAppKitGradientRecord record{gradients[i].kind,
                                    gradients[i].start,
                                    gradients[i].end,
                                    gradients[i].center,
                                    gradients[i].radiusX,
                                    gradients[i].radiusY,
                                    gradients[i].area,
                                    gradients[i].tile,
                                    gradients[i].repeats,
                                    {}};
      if (gradients[i].stops != nullptr && gradients[i].stopCount > 0) {
        record.stops.assign(gradients[i].stops, gradients[i].stops + gradients[i].stopCount);
      }
      wanted.push_back(std::move(record));
    }
  }

  // Compared before it is kept, for the reason the shadows are: a view re-sends
  // identical props on every mutation, and a redraw per mutation is a gradient
  // rasterised again for nothing.
  if (wanted.size() == _gradients.size()) {
    bool same = true;
    for (size_t i = 0; i < wanted.size() && same; i++) {
      same = wanted[i].kind == _gradients[i].kind &&
             CGRectEqualToRect(wanted[i].area, _gradients[i].area) &&
             CGRectEqualToRect(wanted[i].tile, _gradients[i].tile) &&
             wanted[i].repeats == _gradients[i].repeats &&
             CGPointEqualToPoint(wanted[i].start, _gradients[i].start) &&
             CGPointEqualToPoint(wanted[i].end, _gradients[i].end) &&
             CGPointEqualToPoint(wanted[i].center, _gradients[i].center) &&
             wanted[i].radiusX == _gradients[i].radiusX &&
             wanted[i].radiusY == _gradients[i].radiusY &&
             wanted[i].stops.size() == _gradients[i].stops.size() &&
             (wanted[i].stops.empty() ||
              memcmp(wanted[i].stops.data(),
                     _gradients[i].stops.data(),
                     wanted[i].stops.size() * sizeof(RnAppKitGradientStop)) == 0);
    }
    if (same) {
      return;
    }
  }
  _gradients = std::move(wanted);
  self.needsDisplay = YES;
}

- (NSInteger)rnGradientCount {
  return (NSInteger)_gradients.size();
}

// The gradients, back to front, clipped to the view's own rounded box.
//
// `drawRect:` is already clipped to the bounds, so only the rounded case needs a
// path; a gradient with square corners on a rounded card is the kind of
// difference a screenshot shows and a tree does not.
//
// Drawn beyond both ends of its line, which is what CSS does: the first and last
// colours extend to the edges of the box rather than leaving it unpainted, and
// the gradient line is often shorter than the box's diagonal.
- (void)rnDrawGradientsInContext:(CGContextRef)context size:(NSSize)size {
  if (_gradients.empty()) {
    return;
  }
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  for (auto gradient = _gradients.rbegin(); gradient != _gradients.rend(); ++gradient) {
    if (gradient->stops.empty()) {
      continue;
    }
    std::vector<CGFloat> components;
    std::vector<CGFloat> locations;
    components.reserve(gradient->stops.size() * 4);
    locations.reserve(gradient->stops.size());
    for (const RnAppKitGradientStop &stop : gradient->stops) {
      components.insert(components.end(), stop.color, stop.color + 4);
      locations.push_back(stop.offset);
    }
    CGGradientRef ramp = CGGradientCreateWithColorComponents(
        space, components.data(), locations.data(), locations.size());
    if (ramp == nullptr) {
      continue;
    }

    if (gradient->area.size.width <= 0 || gradient->area.size.height <= 0) {
      CGGradientRelease(ramp);
      continue;
    }

    CGContextSaveGState(context);
    if (_hasBorderRadii || _cornerRadius > 0) {
      CGFloat radii[8];
      for (int i = 0; i < 8; i++) {
        radii[i] = _hasBorderRadii ? _borderRadii[i] : _cornerRadius;
      }
      CGPathRef path = RnAppKitCreateRoundedPath(CGRectMake(0, 0, size.width, size.height), radii);
      CGContextAddPath(context, path);
      CGContextClip(context);
      CGPathRelease(path);
    }

    // Where the tiles go. Core Graphics has no repeat, so this is the loop GSK's
    // repeat node is on the other host: from the first tile, stepping by the
    // period until past the painting area. An axis that does not repeat arrives
    // with the painting area as its tile, so it steps once.
    const CGRect painting = CGRectMake(0, 0, size.width, size.height);
    // Nothing repeating is one tile, whatever the tile says: the flag is what
    // core resolved, and the GTK side reads it the same way by pushing no repeat
    // node at all. Stepping by a tile that happens to equal the image would draw
    // a grid for a `no-repeat` background, which is what the test for an image
    // filling its own rectangle caught.
    const CGFloat stepX = gradient->repeats && gradient->tile.size.width > 0
        ? gradient->tile.size.width
        : painting.size.width;
    const CGFloat stepY = gradient->repeats && gradient->tile.size.height > 0
        ? gradient->tile.size.height
        : painting.size.height;
    // Capped, because a stylesheet may ask for a one-point tile over a window:
    // 256 steps per axis is past the point where another tile changes a pixel on
    // any screen this runs on, and it keeps a pathological style from costing
    // minutes a frame. GSK's node has its own bound for the same reason.
    constexpr int kMaxTiles = 256;
    const CGFloat originX = gradient->repeats ? gradient->tile.origin.x : gradient->area.origin.x;
    const CGFloat originY = gradient->repeats ? gradient->tile.origin.y : gradient->area.origin.y;
    const int countX = gradient->repeats && stepX > 0
        ? (int)std::min<double>(kMaxTiles,
                                std::floor((CGRectGetMaxX(painting) - originX) / stepX) + 1)
        : 1;
    const int countY = gradient->repeats && stepY > 0
        ? (int)std::min<double>(kMaxTiles,
                                std::floor((CGRectGetMaxY(painting) - originY) / stepY) + 1)
        : 1;

    for (int row = 0; row < std::max(countY, 1); row++) {
      for (int column = 0; column < std::max(countX, 1); column++) {
        const CGFloat dx = column * stepX;
        const CGFloat dy = row * stepY;
        const CGRect imageRect = CGRectOffset(gradient->area, dx, dy);
        if (!CGRectIntersectsRect(imageRect, painting)) {
          continue;
        }

        CGContextSaveGState(context);
        // Clipped to the image's own rectangle: the gradient is drawn with
        // `kCGGradientDrawsBeforeStartLocation` and its `After` twin, which fill
        // the whole clip with the end colours -- so the clip is what keeps one
        // tile inside its tile.
        CGContextClipToRect(context, imageRect);
        CGContextTranslateCTM(context, dx, dy);
        [self rnDrawOneGradient:*gradient ramp:ramp context:context];
        CGContextRestoreGState(context);
      }
    }
    CGContextRestoreGState(context);
    CGGradientRelease(ramp);
  }
  CGColorSpaceRelease(space);
}

// One gradient, at the rectangle its geometry was resolved against. The caller
// has clipped and translated to the tile.
- (void)rnDrawOneGradient:(const RnAppKitGradientRecord &)gradientRef
                     ramp:(CGGradientRef)ramp
                  context:(CGContextRef)context {
  const RnAppKitGradientRecord *gradient = &gradientRef;
  if (gradient->kind == RnAppKitGradientKindRadial) {
    // Core Graphics draws radial gradients between two *circles*, so an ellipse
    // is a scaled coordinate system rather than a different call: squash the y
    // axis around the centre by the ratio of the radii and draw a circle of the
    // horizontal radius. A host that drew a circle of one radius would be
    // drawing a plausible gradient of the wrong shape.
    const CGFloat radiusX = gradient->radiusX;
    const CGFloat radiusY = gradient->radiusY;
    if (radiusX <= 0 || radiusY <= 0) {
      // An ending shape with no size. The caller's clip and state are undone by
      // its own restore.
      return;
    }
    // Scaled *about the centre*, so the centre itself does not move and the
    // circle of the horizontal radius comes out as an ellipse with the vertical
    // one. Scaling about the origin instead would need the centre divided by the
    // same ratio, which is the mistake this comment exists to stop: it draws an
    // ellipse of the right shape in the wrong place.
    CGContextTranslateCTM(context, gradient->center.x, gradient->center.y);
    CGContextScaleCTM(context, 1.0, radiusY / radiusX);
    CGContextTranslateCTM(context, -gradient->center.x, -gradient->center.y);
    CGContextDrawRadialGradient(context,
                                ramp,
                                gradient->center,
                                0.0,
                                gradient->center,
                                radiusX,
                                kCGGradientDrawsBeforeStartLocation |
                                    kCGGradientDrawsAfterEndLocation);
  } else {
    CGContextDrawLinearGradient(context,
                                ramp,
                                gradient->start,
                                gradient->end,
                                kCGGradientDrawsBeforeStartLocation |
                                    kCGGradientDrawsAfterEndLocation);
  }
}

- (void)setRnBoxShadows:(const RnAppKitBoxShadow *)shadows count:(NSInteger)count {
  std::vector<RnAppKitBoxShadow> wanted;
  if (shadows != nullptr && count > 0) {
    wanted.assign(shadows, shadows + count);
  }
  // Compared rather than rebuilt blindly: a view re-sends identical props on
  // every mutation, and a layer rebuilt per mutation is a shadow recomputed for
  // nothing on every keystroke.
  if (wanted.size() == _boxShadows.size() &&
      (wanted.empty() || memcmp(wanted.data(), _boxShadows.data(),
                                wanted.size() * sizeof(RnAppKitBoxShadow)) == 0)) {
    return;
  }
  _boxShadows = std::move(wanted);
  [self rnRebuildBoxShadowLayers];
}

- (NSInteger)rnBoxShadowCount {
  return (NSInteger)_boxShadows.size();
}

- (NSArray<CALayer *> *)rnBoxShadowLayers {
  return _boxShadowLayers != nil ? [_boxShadowLayers copy] : @[];
}

// One layer per shadow, rebuilt whenever the list, the view's size, its radii or
// its place in the tree changes -- a shadow's path is in the view's own
// coordinates and an outset one lives in its parent, so all four invalidate it.
//
// Added back to front, because CSS says the first shadow in the list is the one
// on top and sublayers composite in order.
//
// **Where an outset shadow goes depends on whether this view clips itself.**
//
// Normally it is a sublayer of the view, which is what React Native's iOS half
// does: the mask cuts the box out of the shadow, so it only ever paints outside,
// and being inside the view's own layer it composites exactly where the view
// does -- above earlier siblings, below later ones.
//
// That arrangement loses the shadow entirely when the view clips its own layer,
// which `overflow: 'hidden'` does through `masksToBounds` and non-uniform radii
// do through a mask layer. CSS clips neither: an element's own shadow is outside
// its box and is not subject to its overflow. So a clipping view casts its
// shadow into the *parent's* layer instead, below its own.
//
// The cost of that path is z-order among siblings: AppKit attaches a subview's
// layer lazily, at the first display, and these are built while mounting -- so
// `below:self.layer` has nothing to aim at yet and the shadow lands at the
// bottom of the parent, below every sibling rather than below this view alone.
// Visible only where a sibling overlaps the shadow of a clipping view, which is
// the narrower case of the two.
//
// Inset shadows are always sublayers of this view, where clipping is harmless
// and right. They do paint above this view's own drawn text rather than under
// it, which the GTK host gets right for free; backlog/correctness.md records
// both of these.
// Whether this view's own layer clips what is inside it, which decides where an
// outset shadow can live. Both answers come from props: `overflow: 'hidden'` and
// a set of radii too elliptical for CALayer's cornerRadius.
- (BOOL)rnClipsItsOwnLayer {
  return _clipsChildren || self.layer.mask != nil;
}

- (void)rnRebuildBoxShadowLayers {
  for (CALayer *layer in _boxShadowLayers) {
    [layer removeFromSuperlayer];
  }
  [_boxShadowLayers removeAllObjects];
  if (_boxShadows.empty()) {
    return;
  }
  if (_boxShadowLayers == nil) {
    _boxShadowLayers = [NSMutableArray array];
  }

  const CGSize size = self.bounds.size;
  if (size.width <= 0 || size.height <= 0) {
    return;
  }
  CGFloat radii[8];
  for (int i = 0; i < 8; i++) {
    radii[i] = _hasBorderRadii ? _borderRadii[i] : 0;
  }

  for (auto shadow = _boxShadows.rbegin(); shadow != _boxShadows.rend(); ++shadow) {
    // A shadow React Native could not parse a colour for arrives with none.
    // Painting it would put a black shadow under a view that asked for nothing.
    if (shadow->color[3] <= 0) {
      continue;
    }
    if (shadow->inset) {
      CALayer *layer = RnAppKitInsetShadowLayer(*shadow, radii, size);
      [self.layer addSublayer:layer];
      [_boxShadowLayers addObject:layer];
      continue;
    }
    CALayer *layer = RnAppKitOutsetShadowLayer(*shadow, radii, size);
    if (![self rnClipsItsOwnLayer]) {
      [self.layer addSublayer:layer];
      [_boxShadowLayers addObject:layer];
      continue;
    }
    CALayer *parent = self.superview.layer;
    if (parent == nil) {
      // A clipping surface root, or one not yet mounted. Nothing to cast onto;
      // the next move into a superview rebuilds this.
      continue;
    }
    // In the parent's coordinates, which are this view's frame. Both are flipped
    // the same way, so the frame carries over as it stands.
    layer.frame = self.frame;
    [parent insertSublayer:layer below:self.layer];
    [_boxShadowLayers addObject:layer];
  }
}

// A view that moves, or is removed, takes its shadows with it. Without this an
// unmounted card leaves its shadow behind in the parent, which is the one bug
// this arrangement can have that the simpler one cannot.
- (void)viewDidMoveToSuperview {
  [super viewDidMoveToSuperview];
  if (!_boxShadows.empty()) {
    [self rnRebuildBoxShadowLayers];
  }
  if (_outlineWidth > 0) {
    [self rnRebuildOutlineLayer];
  }
}

- (void)setRnOutlineWidth:(CGFloat)width
                   offset:(CGFloat)offset
                    color:(const CGFloat *)color
                    style:(RnAppKitBorderStyle)style {
  const CGFloat wanted = width > 0 ? width : 0;
  CGFloat components[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; i++) {
    components[i] = color != nullptr ? color[i] : 0;
  }
  if (_outlineWidth == wanted && _outlineOffset == offset && _outlineStyle == style &&
      memcmp(_outlineColor, components, sizeof(components)) == 0) {
    return;
  }
  _outlineWidth = wanted;
  _outlineOffset = offset;
  memcpy(_outlineColor, components, sizeof(components));
  _outlineStyle = style;
  [self rnRebuildOutlineLayer];
}

- (CAShapeLayer *)rnOutlineLayer {
  return _outlineLayer;
}

// The outline, as a stroked shape outside the box.
//
// Stroked rather than a layer border, which is what React Native's iOS half uses
// for the solid case: a stroke takes a dash pattern, so dotted and dashed need no
// second mechanism, and it takes an arbitrary path, so elliptical radii need no
// special case either. The path sits half a width outside the offset, a stroke
// straddling its path.
//
// Placed by the same rule the box shadows use -- inside the view, or in the
// parent when the view clips its own layer -- so an outline on a card with
// `overflow: 'hidden'` is not quietly clipped away.
- (void)rnRebuildOutlineLayer {
  [_outlineLayer removeFromSuperlayer];
  _outlineLayer = nil;
  if (_outlineWidth <= 0 || _outlineColor[3] <= 0) {
    return;
  }
  const NSRect bounds = self.bounds;
  if (bounds.size.width <= 0 || bounds.size.height <= 0) {
    return;
  }

  const CGFloat grow = _outlineOffset + _outlineWidth / 2;
  CGFloat radii[8];
  for (int i = 0; i < 8; i++) {
    // Each non-zero radius grows with the ring so it stays concentric; a corner
    // that was square stays square, as it does on the GTK side.
    radii[i] = _hasBorderRadii ? (_borderRadii[i] > 0 ? _borderRadii[i] + grow : 0)
                               : (_cornerRadius > 0 ? _cornerRadius + grow : 0);
  }
  CGPathRef path =
      RnAppKitCreateRoundedPath(CGRectInset(bounds, -grow, -grow), radii);

  CAShapeLayer *layer = [CAShapeLayer layer];
  layer.path = path;
  layer.fillColor = nil;
  layer.lineWidth = _outlineWidth;
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGColorRef colour = CGColorCreate(space, _outlineColor);
  layer.strokeColor = colour;
  CGColorRelease(colour);
  CGColorSpaceRelease(space);

  if (_outlineStyle == RnAppKitBorderStyleDotted) {
    layer.lineCap = kCALineCapRound;
    layer.lineDashPattern = @[ @0, @(_outlineWidth * 2) ];
  } else if (_outlineStyle == RnAppKitBorderStyleDashed) {
    layer.lineDashPattern = @[ @(_outlineWidth * 3), @(_outlineWidth * 2) ];
  }

  if ([self rnClipsItsOwnLayer]) {
    CALayer *parent = self.superview.layer;
    if (parent != nil) {
      layer.frame = self.frame;
      [parent insertSublayer:layer below:self.layer];
      _outlineLayer = layer;
    }
  } else {
    layer.frame = bounds;
    [self.layer addSublayer:layer];
    _outlineLayer = layer;
  }
  CGPathRelease(path);
}

- (void)setRnFilters:(const RnAppKitFilters *)filters {
  if (filters == nullptr) {
    // Every half has to come off, and there are three. A list of nothing but
    // `opacity()` leaves no Core Image filter behind, and so does a list of
    // nothing but `dropShadow()` -- so an early return on `_filters` alone keeps
    // the view faded, or shadowed, for good. The first of those two was caught
    // by the test for taking a filter away; the second was caught by the same
    // test written again for the shadow, which is the argument for writing it.
    if (_filters == nil && _filterOpacity == 1 && _filterShadow.color[3] == 0 &&
        _filterShadow.standardDeviation == 0) {
      return;
    }
    _filters = nil;
    self.layer.filters = nil;
    _filterOpacity = 1;
    [self rnApplyOpacity];
    [self rnApplyFilterShadow:nullptr];
    return;
  }

  NSMutableArray<CIFilter *> *built = [NSMutableArray array];
  if (filters->hasMatrix) {
    // CIColorMatrix takes the matrix as four input vectors, one per *output*
    // channel's weights, plus a bias -- which is this matrix row by row.
    CIFilter *matrix = [CIFilter filterWithName:@"CIColorMatrix"];
    const auto row = [filters](int index) {
      return [CIVector vectorWithX:filters->matrix[index * 4 + 0]
                                Y:filters->matrix[index * 4 + 1]
                                Z:filters->matrix[index * 4 + 2]
                                W:filters->matrix[index * 4 + 3]];
    };
    [matrix setValue:row(0) forKey:@"inputRVector"];
    [matrix setValue:row(1) forKey:@"inputGVector"];
    [matrix setValue:row(2) forKey:@"inputBVector"];
    [matrix setValue:row(3) forKey:@"inputAVector"];
    [matrix setValue:[CIVector vectorWithX:filters->offset[0]
                                         Y:filters->offset[1]
                                         Z:filters->offset[2]
                                         W:filters->offset[3]]
              forKey:@"inputBiasVector"];
    [built addObject:matrix];
  }
  if (filters->blurRadius > 0) {
    CIFilter *blur = [CIFilter filterWithName:@"CIGaussianBlur"];
    // Sigma is half the radius, the same conversion the image blur and the box
    // shadows use.
    [blur setValue:@(filters->blurRadius / 2) forKey:@"inputRadius"];
    [built addObject:blur];
  }
  _filters = built.count > 0 ? built : nil;
  self.layer.filters = _filters;

  // `opacity()` is not a Core Image filter here: it multiplies the view's own
  // opacity, which is a layer property and cheaper than a filter pass. The GTK
  // side folds it into its opacity node for the same reason.
  _filterOpacity = filters->opacity;
  [self rnApplyOpacity];

  [self rnApplyFilterShadow:filters->shadowCount > 0 ? filters->shadows : nullptr];
}

// `dropShadow()`, which is the layer's own shadow rather than a Core Image
// filter: with no `shadowPath`, Core Animation casts the shadow from the
// layer's *alpha*, which is what the filter function means and what a box shadow
// cannot do. The box shadows beside it are separate layers with a path each, so
// the two mechanisms do not collide.
//
// `CALayer.shadowRadius` is the gaussian's standard deviation, so what React
// Native parsed crosses unchanged -- where the GTK side doubles it, GSK's radius
// being CSS's. React Native's own iOS half passes the same number to SwiftUI's
// `.shadow(radius:)`.
//
// One shadow, because a layer has one. A list of several needs a wrapper layer
// each and is recorded in backlog/platform-macos.md rather than half done.
- (void)rnApplyFilterShadow:(const RnAppKitFilterShadow *)shadow {
  if (shadow == nullptr) {
    if (_filterShadow.color[3] == 0 && _filterShadow.standardDeviation == 0) {
      return;
    }
    _filterShadow = RnAppKitFilterShadow{};
    self.layer.shadowOpacity = 0;
    self.layer.shadowColor = nil;
    self.layer.shadowRadius = 0;
    self.layer.shadowOffset = CGSizeZero;
    return;
  }
  if (memcmp(&_filterShadow, shadow, sizeof(RnAppKitFilterShadow)) == 0) {
    return;
  }
  _filterShadow = *shadow;

  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  const CGFloat opaque[4] = {shadow->color[0], shadow->color[1], shadow->color[2], 1};
  CGColorRef colour = CGColorCreate(space, opaque);
  self.layer.shadowColor = colour;
  CGColorRelease(colour);
  CGColorSpaceRelease(space);
  // The colour's alpha is the layer's shadowOpacity, which is where Core
  // Animation keeps it: a shadowColor with an alpha and an opacity of 1 would
  // multiply the two.
  self.layer.shadowOpacity = (float)shadow->color[3];
  self.layer.shadowRadius = shadow->standardDeviation;
  // This view is flipped, so a positive dy is down, as CSS means it. The layer
  // is not, which is why the sign is flipped here and not in the shadow path the
  // box shadows build.
  self.layer.shadowOffset = CGSizeMake(shadow->dx, -shadow->dy);
  // No path: the shadow is cast from the alpha, which is the whole point.
  self.layer.shadowPath = nil;
}

- (RnAppKitFilterShadow)rnFilterShadow {
  return _filterShadow;
}

- (NSArray<CIFilter *> *)rnFilters {
  return _filters != nil ? _filters : @[];
}

- (void)setRnHitSlop:(const CGFloat *)insets {
  BOOL any = NO;
  for (int edge = 0; edge < 4; edge++) {
    _hitSlop[edge] = insets != nullptr ? insets[edge] : 0;
    if (_hitSlop[edge] != 0) {
      any = YES;
    }
  }
  _hasHitSlop = any;
  // Nothing to redraw: the prop moves no pixels.
}

// The box a press has to land in, which is the bounds unless `hitSlop` says
// otherwise. In the view's own coordinates, so a scrolled view's offset is
// already in `bounds`.
- (NSRect)rnHitArea {
  const NSRect bounds = self.bounds;
  if (!_hasHitSlop) {
    return bounds;
  }
  return NSMakeRect(bounds.origin.x - _hitSlop[3],
                    bounds.origin.y - _hitSlop[0],
                    bounds.size.width + _hitSlop[1] + _hitSlop[3],
                    bounds.size.height + _hitSlop[0] + _hitSlop[2]);
}

- (void)setRnBorderStyle:(RnAppKitBorderStyle)style {
  if (_borderStyle == style) {
    return;
  }
  _borderStyle = style;
  self.needsDisplay = YES;
}

- (void)insertRnChild:(RnAppKitView *)child atIndex:(NSInteger)index {
  NSArray<NSView *> *children = self.subviews;
  if (index >= (NSInteger)children.count) {
    [self addSubview:child];
    [self rnRaiseIndicators];
    return;
  }
  [self addSubview:child positioned:NSWindowBelow relativeTo:children[(NSUInteger)index]];
}

// A child added at or past the end lands above the overlay, so put it back on
// top. Cheap, and the alternative -- teaching every insert about a view that is
// not part of the tree -- puts the overlay into code that has nothing to do
// with it.
- (void)rnRaiseIndicators {
  if (_indicators != nil) {
    [self addSubview:_indicators positioned:NSWindowAbove relativeTo:nil];
  }
}

- (void)removeRnChild:(RnAppKitView *)child {
  // Removed, not destroyed: a Delete mutation for the same tag follows
  // separately, and until then the view may be inserted somewhere else.
  if (child.superview == self) {
    [child removeFromSuperview];
  }
}

- (void)describeInto:(NSMutableString *)out depth:(NSInteger)depth {
  for (NSInteger i = 0; i < depth; i++) {
    [out appendString:@"  "];
  }
  const NSRect frame = self.frame;
  [out appendFormat:@"view tag=%ld frame=(%g,%g %gx%g)",
                    (long)self.rnTag,
                    frame.origin.x,
                    frame.origin.y,
                    frame.size.width,
                    frame.size.height];

  if (_hasBackgroundColor) {
    [out appendFormat:@" bg=#%02x%02x%02x%02x",
                      (unsigned)(_backgroundComponents[0] * 255.0 + 0.5),
                      (unsigned)(_backgroundComponents[1] * 255.0 + 0.5),
                      (unsigned)(_backgroundComponents[2] * 255.0 + 0.5),
                      (unsigned)(_backgroundComponents[3] * 255.0 + 0.5)];
  }
  // Field order matches the GTK side exactly -- bg, opacity, clip, then text --
  // because scripts/compare_hosts.sh diffs the two dumps line by line and a
  // reordering would read as every line differing.
  if (_opacity < 1.0) {
    [out appendFormat:@" opacity=%g", _opacity];
  }
  if (_clipsChildren) {
    [out appendString:@" clip"];
  }
  // Whether anything is drawn at all. Without this a view hidden by
  // `display: none` or by a back face turned away reads exactly like a visible
  // one, and the only prop in this dump that removes a view entirely was the
  // only one it could not show.
  if (self.hidden) {
    [out appendString:@" hidden"];
  }
  // Per-corner radii and per-edge borders, in the same fields and the same
  // order the GTK and Win32 sides print. They are here for the same reason
  // `transform=` is, and the comment there names the precedent: a frame cannot
  // show a border, so a bordered view and a bare one read as identical in this
  // dump -- which is how this host went until phase 47 applying neither.
  if (_hasBorderRadii) {
    [out appendFormat:@" radii=(%g,%g,%g,%g,%g,%g,%g,%g)",
                      (double)_borderRadii[0], (double)_borderRadii[1],
                      (double)_borderRadii[2], (double)_borderRadii[3],
                      (double)_borderRadii[4], (double)_borderRadii[5],
                      (double)_borderRadii[6], (double)_borderRadii[7]];
  }
  if (_hasBorders) {
    [out appendFormat:@" borderw=(%g,%g,%g,%g)",
                      (double)_borderWidths[0], (double)_borderWidths[1],
                      (double)_borderWidths[2], (double)_borderWidths[3]];
    [out appendString:@" borderc=("];
    for (int edge = 0; edge < 4; edge++) {
      [out appendFormat:@"%s#%02x%02x%02x%02x",
                        edge == 0 ? "" : ",",
                        (unsigned)(_borderColors[edge * 4 + 0] * 255.0 + 0.5),
                        (unsigned)(_borderColors[edge * 4 + 1] * 255.0 + 0.5),
                        (unsigned)(_borderColors[edge * 4 + 2] * 255.0 + 0.5),
                        (unsigned)(_borderColors[edge * 4 + 3] * 255.0 + 0.5)];
    }
    [out appendString:@")"];
    // The style, when it is not solid. Printed for the same reason the widths
    // are: a dashed border and a solid one are the same four widths and the same
    // four colours, and this is the only thing that can say the prop arrived.
    if (_borderStyle != RnAppKitBorderStyleSolid) {
      [out appendFormat:@" border-style=%s",
                        _borderStyle == RnAppKitBorderStyleDotted ? "dotted" : "dashed"];
    }
  }
  // Gradients: how many, each one's line and stop count, where the image goes
  // and the tile it repeats in, spelled as GTK spells them. Not every stop,
  // which a transition hint can turn into eleven of: what a cross-host diff
  // needs is that the same gradient arrived with the same geometry, and the stop
  // fixup is asserted in core's own tests.
  //
  // `at=` is the rectangle the image fills and `tile=` is the period, printed
  // only when it repeats, so a `no-repeat` background is the line without a
  // tile. The three background props are invisible in every other line here.
  for (const RnAppKitGradientRecord &gradient : _gradients) {
    if (gradient.kind == RnAppKitGradientKindRadial) {
      [out appendFormat:@" gradient=(radial (%g,%g) %gx%g,%lu stops",
                        (double)gradient.center.x,
                        (double)gradient.center.y,
                        (double)gradient.radiusX,
                        (double)gradient.radiusY,
                        (unsigned long)gradient.stops.size()];
    } else {
      [out appendFormat:@" gradient=((%g,%g)-(%g,%g),%lu stops",
                        (double)gradient.start.x,
                        (double)gradient.start.y,
                        (double)gradient.end.x,
                        (double)gradient.end.y,
                        (unsigned long)gradient.stops.size()];
    }
    [out appendFormat:@",at=(%g,%g %gx%g)",
                      (double)gradient.area.origin.x,
                      (double)gradient.area.origin.y,
                      (double)gradient.area.size.width,
                      (double)gradient.area.size.height];
    if (gradient.repeats) {
      [out appendFormat:@",tile=(%g,%g %gx%g)",
                        (double)gradient.tile.origin.x,
                        (double)gradient.tile.origin.y,
                        (double)gradient.tile.size.width,
                        (double)gradient.tile.size.height];
    }
    [out appendString:@")"];
  }

  // Box shadows, each in full and spelled as GTK spells them. Nothing else in
  // this dump can say a shadow is there, and the numbers are the whole feature:
  // an offset that went to the wrong axis or a spread read as a blur still draws
  // a plausible shadow.
  for (const RnAppKitBoxShadow &shadow : _boxShadows) {
    [out appendFormat:@" shadow=(%s%g,%g,%g,%g,#%02x%02x%02x%02x)",
                      shadow.inset ? "inset " : "",
                      (double)shadow.dx,
                      (double)shadow.dy,
                      (double)shadow.blur,
                      (double)shadow.spread,
                      (unsigned)(shadow.color[0] * 255.0 + 0.5),
                      (unsigned)(shadow.color[1] * 255.0 + 0.5),
                      (unsigned)(shadow.color[2] * 255.0 + 0.5),
                      (unsigned)(shadow.color[3] * 255.0 + 0.5)];
  }
  if (_hasTransform) {
    // The 2D affine part, which is all either platform draws, in the order
    // CSS writes a matrix(): a, b, c, d, tx, ty. The GTK side prints the same
    // six from its own matrix, so a transform is comparable across the two --
    // without which a view that is rotated on one desktop and not on the other
    // looks identical in this dump. Which is exactly how the macOS host went
    // this long without applying transforms at all.
    [out appendFormat:@" transform=(%g,%g,%g,%g,%g,%g)",
                      (double)_transform.m11,
                      (double)_transform.m12,
                      (double)_transform.m21,
                      (double)_transform.m22,
                      (double)_transform.m41,
                      (double)_transform.m42];
  }
  const NSPoint scroll = self.bounds.origin;
  if (scroll.x != 0 || scroll.y != 0) {
    [out appendFormat:@" scroll=(%g,%g)", scroll.x, scroll.y];
  }
  // The overlay scrollbars, which are otherwise pure paint and so invisible to
  // every test this project has. Printed only when there is one, so a view that
  // does not scroll stays as short as it was.
  if (_indicators != nil && _indicators.rnVerticalLength > 0) {
    [out appendFormat:@" scrollbar-v=(%g,%g)",
                      _indicators.rnVerticalOffset,
                      _indicators.rnVerticalLength];
  }
  if (_indicators != nil && _indicators.rnHorizontalLength > 0) {
    [out appendFormat:@" scrollbar-h=(%g,%g)",
                      _indicators.rnHorizontalOffset,
                      _indicators.rnHorizontalLength];
  }
  // Printed only when it is not the default, like every other field here.
  // Worth printing at all because it is invisible: a view with
  // `pointerEvents: none` is drawn exactly like one without, and the only way
  // the cross-host diff can say the prop arrived on all three is if each one
  // reports it.
  if (self.rnPointerEvents != RnAppKitPointerEventsAuto) {
    [out appendFormat:@" pe=%s", RnAppKitPointerEventsName(self.rnPointerEvents)];
  }
  // Printed for the reason pointerEvents is, and spelled as GTK spells it: the
  // prop is invisible in a still picture, so a dump is the only thing that can
  // say it arrived on both hosts.
  if (_cursorName != nil) {
    [out appendFormat:@" cursor=%@", _cursorName];
  }
  // The blend mode the app asked for, spelled as GTK spells it: the keyword,
  // not the Core Image filter it became.
  if (_blendModeName != nil) {
    [out appendFormat:@" blend=%@", _blendModeName];
  }
  // The outline, which is invisible in every other line: it is not a border and
  // a view with one has the same frame and the same colours without it. Spelled
  // as GTK spells it.
  if (_outlineWidth > 0) {
    [out appendFormat:@" outline=(%g,%g,#%02x%02x%02x%02x",
                      (double)_outlineWidth,
                      (double)_outlineOffset,
                      (unsigned)(_outlineColor[0] * 255.0 + 0.5),
                      (unsigned)(_outlineColor[1] * 255.0 + 0.5),
                      (unsigned)(_outlineColor[2] * 255.0 + 0.5),
                      (unsigned)(_outlineColor[3] * 255.0 + 0.5)];
    if (_outlineStyle != RnAppKitBorderStyleSolid) {
      [out appendFormat:@",%s",
                        _outlineStyle == RnAppKitBorderStyleDotted ? "dotted" : "dashed"];
    }
    [out appendString:@")"];
  }

  // `filter`, as the pieces it came to plus what its matrix makes of one probe
  // colour, spelled exactly as GTK spells it: sixteen numbers would drown the
  // line, and one colour still fails when a matrix is wrong. The probe is
  // (1, 0.5, 0.25) so no two channels can be swapped unnoticed.
  if (_filters != nil || _filterOpacity < 1 || _filterShadow.color[3] > 0) {
    [out appendString:@" filter=("];
    BOOL first = YES;
    for (CIFilter *filter in _filters) {
      if ([filter.name isEqualToString:@"CIColorMatrix"]) {
        const CGFloat probe[4] = {1.0, 0.5, 0.25, 1.0};
        CGFloat result[4] = {0, 0, 0, 0};
        NSArray<NSString *> *keys =
            @[ @"inputRVector", @"inputGVector", @"inputBVector", @"inputAVector" ];
        CIVector *bias = [filter valueForKey:@"inputBiasVector"];
        for (int row = 0; row < 4; row++) {
          CIVector *weights = [filter valueForKey:keys[(NSUInteger)row]];
          result[row] = [bias valueAtIndex:(size_t)row] + weights.X * probe[0] +
                        weights.Y * probe[1] + weights.Z * probe[2] + weights.W * probe[3];
        }
        const auto byte = [](CGFloat value) {
          const CGFloat clamped = value < 0 ? 0 : (value > 1 ? 1 : value);
          return (unsigned)(clamped * 255.0 + 0.5);
        };
        [out appendFormat:@"probe=#%02x%02x%02x%02x",
                          byte(result[0]),
                          byte(result[1]),
                          byte(result[2]),
                          byte(result[3])];
        first = NO;
      } else if ([filter.name isEqualToString:@"CIGaussianBlur"]) {
        const double sigma = [[filter valueForKey:@"inputRadius"] doubleValue];
        [out appendFormat:@"%sblur=%g", first ? "" : ",", sigma * 2.0];
        first = NO;
      }
    }
    if (_filterOpacity < 1) {
      [out appendFormat:@"%sopacity=%g", first ? "" : ",", (double)_filterOpacity];
      first = NO;
    }
    // The drop shadow, with the standard deviation React Native parsed, which is
    // also what this layer was given. GTK prints the same number from a radius
    // it doubled.
    if (_filterShadow.color[3] > 0) {
      [out appendFormat:@"%sshadow=(%g,%g,%g,#%02x%02x%02x%02x)",
                        first ? "" : ",",
                        (double)_filterShadow.dx,
                        (double)_filterShadow.dy,
                        (double)_filterShadow.standardDeviation,
                        (unsigned)(_filterShadow.color[0] * 255.0 + 0.5),
                        (unsigned)(_filterShadow.color[1] * 255.0 + 0.5),
                        (unsigned)(_filterShadow.color[2] * 255.0 + 0.5),
                        (unsigned)(_filterShadow.color[3] * 255.0 + 0.5)];
    }
    [out appendString:@")"];
  }

  // `hitSlop`, which is invisible in every other line of this dump: a view with
  // a bigger target is drawn exactly like one without.
  if (_hasHitSlop) {
    [out appendFormat:@" hit-slop=(%g,%g,%g,%g)",
                      (double)_hitSlop[0],
                      (double)_hitSlop[1],
                      (double)_hitSlop[2],
                      (double)_hitSlop[3]];
  }
  // The resolved labelled-by relation, by tag, spelled as GTK spells it. Every
  // tag that resolved, not only the one AppKit could use, so the two hosts print
  // the same line and the difference stays in what each platform does with it.
  if (!_labelledBy.empty()) {
    [out appendString:@" labelled-by="];
    for (size_t i = 0; i < _labelledBy.size(); i++) {
      [out appendFormat:@"%s%ld", i == 0 ? "" : ",", (long)_labelledBy[i]];
    }
  }
  if (_image != nullptr) {
    // The same `texture=WxH fit=<name>` the GTK side emits, so an <Image> shows
    // up in the cross-platform diff and the end-to-end suite can assert on it.
    // The fit is here because it is the only thing about a drawn image that a
    // frame cannot show: two views the same size holding the same picture are
    // identical in every other line of this dump and different on screen.
    [out appendFormat:@" texture=%zux%zu", CGImageGetWidth(_image), CGImageGetHeight(_image)];
    [out appendFormat:@" fit=%s", RnAppKitImageFitName(_imageFit)];
    // Printed for the same reason the fit is: a tinted image and an untinted
    // one are identical in every other line of this dump and different on
    // screen. Formatted exactly as GTK formats a colour, so the cross-host diff
    // can compare them.
    if (_imageTint != nil) {
      NSColor *rgb = [_imageTint colorUsingColorSpace:NSColorSpace.sRGBColorSpace] ?: _imageTint;
      [out appendFormat:@" tint=#%02x%02x%02x%02x",
                        (unsigned)(rgb.redComponent * 255.0 + 0.5),
                        (unsigned)(rgb.greenComponent * 255.0 + 0.5),
                        (unsigned)(rgb.blueComponent * 255.0 + 0.5),
                        (unsigned)(rgb.alphaComponent * 255.0 + 0.5)];
    }
    // And the blur, spelled as GTK spells it. Invisible in this dump for the
    // reason the tint is, and in one more: the prop is read in a branch that
    // knows ImageProps, and a line here is the only thing that can say it got
    // out of that branch and onto the view.
    if (_imageBlur > 0) {
      [out appendFormat:@" blur=%g", (double)_imageBlur];
    }
  }
  if (_textLayout != nil) {
    NSString *text = _textLayout.attributedString.string;
    if (text.length > 0) {
      // Escaped like g_strescape's output on the other side, so a string with a
      // quote or a newline in it stays one line and stays comparable.
      NSMutableString *escaped = [text mutableCopy];
      [escaped replaceOccurrencesOfString:@"\\" withString:@"\\\\"
                                  options:0 range:NSMakeRange(0, escaped.length)];
      [escaped replaceOccurrencesOfString:@"\"" withString:@"\\\""
                                  options:0 range:NSMakeRange(0, escaped.length)];
      [escaped replaceOccurrencesOfString:@"\n" withString:@"\\n"
                                  options:0 range:NSMakeRange(0, escaped.length)];
      [out appendFormat:@" text=\"%@\"", escaped];
    }
  }
  // The paragraph's text shadow, spelled as GTK spells it: no other line can
  // show it, a shadowed paragraph having the same text, colour and box. The
  // standard deviation React Native parsed, which is also what this context was
  // given.
  if (_textLayout != nil && _textLayout.shadowColor != nil) {
    NSColor *colour = [_textLayout.shadowColor
        colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    [out appendFormat:@" text-shadow=(%g,%g,%g,#%02x%02x%02x%02x)",
                      (double)_textLayout.shadowOffset.width,
                      (double)_textLayout.shadowOffset.height,
                      (double)_textLayout.shadowStandardDeviation,
                      (unsigned)(colour.redComponent * 255.0 + 0.5),
                      (unsigned)(colour.greenComponent * 255.0 + 0.5),
                      (unsigned)(colour.blueComponent * 255.0 + 0.5),
                      (unsigned)(colour.alphaComponent * 255.0 + 0.5)];
  }

  // A text field's content lives in its NSTextField peer, not in anything this
  // view draws, so it would otherwise be invisible to every test that reads
  // this tree. Same spelling as the GTK side's.
  if (self.rnEditable != nil) {
    NSString *value = RnPeerText(self.rnEditable);
    NSMutableString *escaped = [value mutableCopy];
    [escaped replaceOccurrencesOfString:@"\\" withString:@"\\\\"
                                options:0 range:NSMakeRange(0, escaped.length)];
    [escaped replaceOccurrencesOfString:@"\"" withString:@"\\\""
                                options:0 range:NSMakeRange(0, escaped.length)];
    [out appendFormat:@" editable=\"%@\"", escaped];

    // First responder is the field's *field editor* while it is being edited,
    // not the field itself, so asking the window whether it is editing this one
    // is the question that actually has the right answer.
    NSWindow *window = self.window;
    // A multiline peer *is* the editor, so it is the first responder itself;
    // a single-line one is being edited through a field editor whose delegate
    // is the field. RnPeerEditor answers for both.
    const BOOL focused = window != nil && window.firstResponder != nil &&
        window.firstResponder == RnPeerEditor(self.rnEditable);
    if (focused) {
      [out appendString:@" focused"];
    }
  }

  // How many DevTools highlights this view is drawing. In the dump because they
  // are otherwise invisible to everything but a screenshot, and because a
  // command that arrived and drew nothing is exactly the failure worth
  // catching.
  if (!_highlightFilled.empty()) {
    [out appendFormat:@" highlights=%lu", (unsigned long)_highlightFilled.size()];
  }

  // What kind of control this view is, and what state it is in. Written by
  // core/DesktopControls.h rather than formatted here, for the same reason the
  // role name below is React Native's vocabulary and not AppKit's.
  if (_rnControlDescription != nil) {
    [out appendFormat:@" control=%@", _rnControlDescription];
  }

  // React Native's role name, not AppKit's. The GTK side reports the same
  // string for the same reason: this dump is compared line by line across two
  // platforms, and each reporting its own toolkit's vocabulary would make every
  // accessible view look like a difference. That the *platform* role was really
  // applied is asserted in each platform's unit tests instead.
  if (_roleName != nil) {
    [out appendFormat:@" role=%@", _roleName];
  }
  // Whether Tab stops here, after `role=` because that is where the GTK side
  // prints it and this dump is diffed line by line. Whether it is focused *now*
  // is deliberately not printed: that depends on what the window server did
  // when the window opened, which is not a property of the platform and would
  // make this dump differ between two machines running the same app.
  if (self.rnFocusable) {
    [out appendString:@" focusable"];
  }
  [out appendString:@"\n"];

  for (NSView *child in self.subviews) {
    if ([child isKindOfClass:[RnAppKitView class]]) {
      [(RnAppKitView *)child describeInto:out depth:depth + 1];
    }
  }
}

// --- Input ----------------------------------------------------------------
//
// AppKit delivers a mouse event to the view its own hit testing picked, which
// is some view in this tree; React Native wants it against the surface root,
// hit-tested React Native's way. So every view forwards, and the root is where
// the forwarding stops.
//
// Not overriding hitTest: to make the root swallow everything instead: that
// would also swallow the cursor rectangles, tooltips and tracking areas any
// later component needs, and AppKit's own hit testing is doing no harm here.

// Keyboard focus.
//
// AppKit's key-view loop is the chain: a view that accepts first responder
// status is reached by Tab, and `nextValidKeyView` walks the view hierarchy, so
// the order is tree order without this project deciding what "next" means. The
// GTK host gets the same for the same reason; the Win32 host has no such loop
// and builds one. See AppKitFocus.h.

- (BOOL)acceptsFirstResponder {
  return _rnFocusable;
}

- (BOOL)canBecomeKeyView {
  // Not inherited: NSView's default also consults the Full Keyboard Access
  // setting for some views, and a React Native app's buttons are not AppKit
  // controls that a user has opted into tabbing to -- they are the whole
  // interface.
  return _rnFocusable && !self.isHiddenOrHasHiddenAncestor;
}

- (nullable id<RnAppKitFocusHandler>)rnFocusHandlerForTree {
  NSView *view = self;
  while (view != nil) {
    if ([view isKindOfClass:[RnAppKitView class]]) {
      id<RnAppKitFocusHandler> handler = ((RnAppKitView *)view).rnFocusHandler;
      if (handler != nil) {
        return handler;
      }
    }
    view = view.superview;
  }
  return nil;
}

- (BOOL)becomeFirstResponder {
  if (![super becomeFirstResponder]) {
    return NO;
  }
  // The ring is drawn by this view, so it has to redraw when focus arrives.
  self.needsDisplay = YES;
  [[self rnFocusHandlerForTree] rnView:self didChangeFocus:YES];
  return YES;
}

- (BOOL)resignFirstResponder {
  if (![super resignFirstResponder]) {
    return NO;
  }
  self.needsDisplay = YES;
  [[self rnFocusHandlerForTree] rnView:self didChangeFocus:NO];
  return YES;
}

- (void)keyDown:(NSEvent *)event {
  // An app's own declared shortcuts first, before activation and before Tab.
  //
  // Deliberately first: an app that binds Enter or Tab means it, and a platform
  // default that won over an explicit declaration would be impossible to
  // override. The cost is that binding Tab breaks focus movement inside that
  // view, which is the app's business and is what a browser does too.
  if ([[self rnFocusHandlerForTree] rnView:self handlesKey:event]) {
    return;
  }
  // Return, enter and space, which are the two keys that activate a control on
  // every desktop and in the browser. Anything else goes on, so a view with
  // focus does not swallow the window's own shortcuts.
  const unichar first = event.charactersIgnoringModifiers.length > 0
      ? [event.charactersIgnoringModifiers characterAtIndex:0]
      : 0;
  const BOOL activates = first == NSCarriageReturnCharacter ||
      first == NSEnterCharacter || first == ' ';
  if (activates && [[self rnFocusHandlerForTree] rnActivateView:self]) {
    return;
  }
  // Tab, which on this platform is this project's own: AppKit's key-view loop
  // does nothing for a window built without a nib. Shift-Tab arrives as the
  // same character with the shift modifier set.
  if (first == NSTabCharacter || first == NSBackTabCharacter) {
    const BOOL forward = first == NSTabCharacter &&
        (event.modifierFlags & NSEventModifierFlagShift) == 0;
    if ([[self rnFocusHandlerForTree] rnMoveFocusForward:forward]) {
      return;
    }
  }
  [super keyDown:event];
}

// Whether to paint the ring: focused, and focused by this window rather than by
// a window that is not in front.
- (BOOL)rnShowsFocusRing {
  NSWindow *window = self.window;
  return _rnFocusable && window != nil && window.firstResponder == self;
}

- (nullable id<RnAppKitInputHandler>)rnHandler:(NSView **)outRoot {
  NSView *view = self;
  while (view != nil) {
    if ([view isKindOfClass:[RnAppKitView class]]) {
      id<RnAppKitInputHandler> handler = ((RnAppKitView *)view).rnInputHandler;
      if (handler != nil) {
        *outRoot = view;
        return handler;
      }
    }
    view = view.superview;
  }
  return nil;
}

- (void)forwardMouse:(NSEvent *)event kind:(char)kind {
  NSView *root = nil;
  id<RnAppKitInputHandler> handler = [self rnHandler:&root];
  if (handler == nil) {
    return;
  }
  const NSPoint point = [root convertPoint:event.locationInWindow fromView:nil];
  // AppKit's `buttonNumber` is 0 for left, 1 for right, 2 for the wheel. W3C
  // has the middle one and the right one the other way round, which is exactly
  // the kind of thing worth converting once rather than comparing twice.
  int button = 0;
  switch (event.buttonNumber) {
    case 0: button = (int)basalt::PointerButton::Primary; break;
    case 1: button = (int)basalt::PointerButton::Secondary; break;
    default: button = (int)basalt::PointerButton::Middle; break;
  }
  switch (kind) {
    case 'd': [handler rnMouseDownAt:point button:button]; break;
    case 'm': [handler rnMouseDraggedTo:point]; break;
    case 'u': [handler rnMouseUpAt:point button:button]; break;
    case 'h': [handler rnMouseMovedTo:point]; break;
    default: break;
  }
}

// Hover.
//
// A view gets `mouseMoved:` only if something asked for it, and the tracking
// area is that ask. Only the surface root has an input handler, and the area
// covers its whole visible rect, so one area serves every view in the tree --
// which is the same arrangement the GTK side gets by attaching its motion
// controller to the root rather than to each widget.
//
// NSTrackingInVisibleRect leaves the area's geometry to AppKit, so a window
// resize does not need this recomputed; `updateTrackingAreas` still has to
// exist, because that is where AppKit expects an area to be re-added.
- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  if (self.rnInputHandler == nil) {
    if (_rnHoverTrackingArea != nil) {
      [self removeTrackingArea:_rnHoverTrackingArea];
      _rnHoverTrackingArea = nil;
    }
    return;
  }
  if (_rnHoverTrackingArea != nil) {
    return;
  }
  _rnHoverTrackingArea = [[NSTrackingArea alloc]
      initWithRect:NSZeroRect
           options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingInVisibleRect |
                   NSTrackingActiveInKeyWindow
             owner:self
          userInfo:nil];
  [self addTrackingArea:_rnHoverTrackingArea];
}

- (void)mouseMoved:(NSEvent *)event {
  [self forwardMouse:event kind:'h'];
}

- (void)mouseExited:(NSEvent *)event {
  NSView *root = nil;
  id<RnAppKitInputHandler> handler = [self rnHandler:&root];
  if (handler != nil) {
    [handler rnMouseExited];
  }
  [super mouseExited:event];
}

// Wheel and touchpad. Unhandled scrolls go to super, which walks the responder
// chain -- so a wheel over a plain view inside a list reaches the list.
- (void)scrollWheel:(NSEvent *)event {
  id<RnAppKitScrollHandler> handler = self.rnScrollHandler;
  if (handler != nil) {
    const NSPoint delta = NSMakePoint(event.scrollingDeltaX, event.scrollingDeltaY);
    // `hasPreciseScrollingDeltas` is the only thing that can tell a touchpad's
    // pixels from a wheel's line counts -- both arrive as small numbers, so the
    // deltas alone cannot. Passed on rather than resolved here: how many pixels
    // a line is worth is a cross-platform decision, and it is made next to the
    // GTK side's copy of it.
    if ([handler rnScrollView:self
                           by:delta
                      precise:event.hasPreciseScrollingDeltas
                        phase:event.phase
                     momentum:event.momentumPhase]) {
      return;
    }
  }
  [super scrollWheel:event];
}

- (void)mouseDown:(NSEvent *)event {
  [self forwardMouse:event kind:'d'];
}

- (void)mouseDragged:(NSEvent *)event {
  [self forwardMouse:event kind:'m'];
}

- (void)mouseUp:(NSEvent *)event {
  [self forwardMouse:event kind:'u'];
}

// Every button, and which one it was now travels with it. React Native's touch
// model still has no concept of one -- that belongs to pointer events -- so a
// secondary click arrives as a pointer event and presses nothing, which is what
// it means on every desktop. See core/PointerButtons.h.
- (void)rightMouseDown:(NSEvent *)event {
  [self forwardMouse:event kind:'d'];
}

- (void)rightMouseDragged:(NSEvent *)event {
  [self forwardMouse:event kind:'m'];
}

- (void)rightMouseUp:(NSEvent *)event {
  [self forwardMouse:event kind:'u'];
}

- (NSString *)describeTree {
  NSMutableString *out = [NSMutableString string];
  [self describeInto:out depth:0];
  return out;
}

@end
