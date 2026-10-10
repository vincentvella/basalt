// Selecting a paragraph's text with a pointer, on AppKit.
//
// The same three questions the GTK suite asks, over the same claims, because
// the point of asking twice is that the two hosts agree: where a point lands in
// the text, what the highlight looks like, and what a press and a drag do. The
// state machine behind the third is core/TextSelection.h and has its own tests.
//
// The pixels matter here more than usual. A selection is a wash under the
// glyphs, so no tree dump can show it: a highlight drawn at the wrong offset,
// or not drawn at all, looks identical to every assertion that is not about
// colour at a point.

#include "TestHarness.h"

#include "AppKitMountingManager.h"
#include "AppKitTouchDispatcher.h"
#include "CoreTextLayout.h"
#include "PlatformServices.h"
#include "RnAppKitView.h"
#include "RnTextLayout.h"
#include "TextSelection.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

#include <sstream>
#include <string>

namespace {

RnTextLayout *paragraphLayout(const char *text, float width) {
  facebook::react::TextAttributes attributes;
  attributes.fontSize = 24.0F;
  facebook::react::AttributedString::Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;
  facebook::react::AttributedString string;
  string.appendFragment(std::move(fragment));
  (void)width;
  return basalt::buildTextLayout(string, facebook::react::ParagraphAttributes{});
}

// A view with a paragraph in it, sized and selectable.
RnAppKitView *paragraphView(const char *text, CGFloat width, CGFloat height, BOOL selectable) {
  RnAppKitView *view = [RnAppKitView viewWithTag:7];
  view.frame = NSMakeRect(0, 0, width, height);
  [view setRnTextLayout:paragraphLayout(text, static_cast<float>(width))];
  [view setRnTextSelectable:selectable];
  return view;
}

// How many pixels in a band are the selection's blue, which is a wash over
// white: the blue channel ends up well clear of the other two.
//
// The paragraph alone rather than a whole view: the highlight is drawn by the
// layout, and a view's own drawRect: wants a layer-backed window this does not
// need.
int highlightPixels(RnTextLayout *layout, CGSize size, int fromX, int toX) {
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width, (size_t)size.height, 8,
                                               0, space, kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextSetRGBFillColor(context, 1, 1, 1, 1);
  CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
  [layout drawInContext:context size:size];

  auto *pixels = static_cast<unsigned char *>(CGBitmapContextGetData(context));
  const size_t stride = CGBitmapContextGetBytesPerRow(context);
  int count = 0;
  for (int x = fromX; x < toX; x++) {
    for (int y = 0; y < (int)size.height; y++) {
      const unsigned char red = pixels[(size_t)y * stride + (size_t)x * 4];
      const unsigned char green = pixels[(size_t)y * stride + (size_t)x * 4 + 1];
      const unsigned char blue = pixels[(size_t)y * stride + (size_t)x * 4 + 2];
      if (blue > red + 20 && blue > green + 20) {
        count++;
      }
    }
  }
  CGContextRelease(context);
  return count;
}

} // namespace

TEST(appkit_selection_the_index_at_a_point_follows_the_text) {
  @autoreleasepool {
    RnAppKitView *view = paragraphView("Selectable text", 400, 60, YES);

    EXPECT_EQ((long)[view rnTextIndexAtPoint:CGPointMake(0, 10)], 0L);
    EXPECT_EQ((long)[view rnTextIndexAtPoint:CGPointMake(399, 10)],
              (long)std::string("Selectable text").size());

    const NSInteger middle = [view rnTextIndexAtPoint:CGPointMake(90, 10)];
    EXPECT(middle > 0);
    EXPECT(middle < (NSInteger)std::string("Selectable text").size());
  }
}

TEST(appkit_selection_a_view_with_no_paragraph_has_no_index) {
  @autoreleasepool {
    RnAppKitView *view = [RnAppKitView viewWithTag:3];
    EXPECT_EQ((long)[view rnTextIndexAtPoint:CGPointMake(10, 10)], -1L);
  }
}

TEST(appkit_selection_the_selected_text_is_what_the_clipboard_would_take) {
  @autoreleasepool {
    RnAppKitView *view = paragraphView("Selectable text", 400, 60, YES);
    EXPECT([view rnSelectedText] == nil);

    [view setRnTextSelectionStart:0 length:10];
    EXPECT_EQ(std::string([view rnSelectedText].UTF8String), std::string("Selectable"));

    // A range that runs past the end of a paragraph rebuilt under it: clamped
    // rather than read past, which is a crash on a shorter string.
    [view setRnTextSelectionStart:11 length:1000];
    EXPECT_EQ(std::string([view rnSelectedText].UTF8String), std::string("text"));
  }
}

TEST(appkit_selection_the_highlight_is_drawn_under_the_selected_range) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(400, 60);
    RnTextLayout *layout = paragraphLayout("Selectable text", 400);

    // Nothing selected: no wash anywhere, which is the control for the two
    // assertions below.
    EXPECT_EQ(highlightPixels(layout, size, 0, 400), 0);

    // The first four characters, which at 24pt is the left end of the line and
    // nowhere near the right.
    layout.selection = NSMakeRange(0, 4);
    EXPECT(highlightPixels(layout, size, 0, 60) > 100);
    EXPECT_EQ(highlightPixels(layout, size, 250, 400), 0);

    // Clearing it takes the wash away rather than leaving the last one drawn.
    layout.selection = NSMakeRange(0, 0);
    EXPECT_EQ(highlightPixels(layout, size, 0, 400), 0);
  }
}

TEST(appkit_selection_a_paragraph_that_stops_being_selectable_loses_its_highlight) {
  @autoreleasepool {
    RnAppKitView *view = paragraphView("Selectable text", 400, 60, YES);
    [view setRnTextSelectionStart:0 length:6];
    [view setRnTextSelectable:NO];
    EXPECT_EQ((long)[view rnTextSelectionLength], 0L);
  }
}

TEST(appkit_selection_a_rebuilt_paragraph_keeps_a_selection_of_the_same_text) {
  @autoreleasepool {
    RnAppKitView *view = paragraphView("Selectable text", 400, 60, YES);
    [view setRnTextSelectionStart:0 length:10];

    // Every mutation that touches a paragraph builds a new layout, a parent
    // re-rendering being enough, so dropping the selection each time would make
    // it impossible to keep one in a live app.
    [view setRnTextLayout:paragraphLayout("Selectable text", 400)];
    EXPECT_EQ((long)[view rnTextSelectionLength], 10L);

    // Different text is a different string, and the old offsets would highlight
    // whatever now sits at them.
    [view setRnTextLayout:paragraphLayout("Something else entirely", 400)];
    EXPECT_EQ((long)[view rnTextSelectionLength], 0L);
  }
}

TEST(appkit_selection_a_drag_across_selectable_text_selects_it) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(1);
    root.frame = NSMakeRect(0, 0, 400, 60);

    RnAppKitView *view = paragraphView("Selectable text", 400, 60, YES);
    [root addSubview:view];

    basalt::AppKitTouchDispatcher dispatcher(&manager, root);

    // Ten steps from the left edge to the middle of the line, which is a sweep
    // rather than a tap and so claims the gesture.
    dispatcher.synthesiseDrag(2, 10, 150, 10, 10);
    EXPECT([view rnTextSelectionLength] > 0);
    EXPECT_EQ((long)[view rnTextSelectionStart], 0L);

    // What `Copy` and Cmd+C would take, through the provider the dispatcher
    // installs: the same text the view reports.
    EXPECT_EQ(basalt::selectedText(), std::string([view rnSelectedText].UTF8String));

    // And a tap afterwards clears it, which is how a person dismisses a
    // highlight.
    dispatcher.synthesiseTap(200, 10);
    EXPECT_EQ((long)[view rnTextSelectionLength], 0L);
    EXPECT(basalt::selectedText().empty());

    manager.destroySurfaceRoot(1);
  }
}

TEST(appkit_selection_a_drag_across_text_that_is_not_selectable_selects_nothing) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(1);
    root.frame = NSMakeRect(0, 0, 400, 60);

    RnAppKitView *view = paragraphView("Selectable text", 400, 60, NO);
    [root addSubview:view];

    basalt::AppKitTouchDispatcher dispatcher(&manager, root);
    dispatcher.synthesiseDrag(2, 10, 150, 10, 10);
    EXPECT_EQ((long)[view rnTextSelectionLength], 0L);

    manager.destroySurfaceRoot(1);
  }
}

TEST(appkit_selection_a_backwards_drag_selects_from_where_it_ended) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(1);
    root.frame = NSMakeRect(0, 0, 400, 60);

    RnAppKitView *view = paragraphView("Selectable text", 400, 60, YES);
    [root addSubview:view];

    basalt::AppKitTouchDispatcher dispatcher(&manager, root);
    dispatcher.synthesiseDrag(150, 10, 2, 10, 10);
    EXPECT_EQ((long)[view rnTextSelectionStart], 0L);
    EXPECT([view rnTextSelectionLength] > 0);

    manager.destroySurfaceRoot(1);
  }
}

TEST(appkit_selection_the_copy_role_copies_the_selection) {
  @autoreleasepool {
    // What Cmd+C performs on this host: the application menu's Copy item
    // carries the key equivalent and performs the role, so there is no second
    // key path. The role used to mean "whatever the responder chain will take
    // a `copy:` for", which is a field and never a paragraph.
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(1);
    root.frame = NSMakeRect(0, 0, 400, 60);

    RnAppKitView *view = paragraphView("Selectable text", 400, 60, YES);
    [root addSubview:view];

    basalt::AppKitTouchDispatcher dispatcher(&manager, root);
    const std::string original = basalt::clipboardText();

    dispatcher.synthesiseDrag(2, 10, 150, 10, 10);
    const std::string expected = std::string([view rnSelectedText].UTF8String);
    EXPECT(!expected.empty());

    basalt::performMenuRole("copy");
    EXPECT_EQ(basalt::clipboardText(), expected);

    basalt::setClipboardText(original);
    manager.destroySurfaceRoot(1);
  }
}
