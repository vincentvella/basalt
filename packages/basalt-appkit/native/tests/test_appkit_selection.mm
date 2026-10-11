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

// --- What a screen reader can read -------------------------------------------
//
// The other half of this entry. A paragraph is an RnTextLayout drawn into a
// view, so VoiceOver had a label for it and nothing to move through; these are
// the NSAccessibility answers added for that, and the selection one is the
// pair of the GTK suite's. Indices are UTF-16 code units everywhere here,
// which is what NSAttributedString counts and what VoiceOver asks in.

namespace {

// An emoji outside the basic plane, which is two UTF-16 code units: text where
// a count of characters and a count of code units disagree, so an assertion
// cannot pass by accident.
const char *const kWithEmoji = "a \xf0\x9f\x8e\xb8 b";

} // namespace

TEST(appkit_selection_a_paragraph_hands_a_screen_reader_its_text) {
  RnAppKitView *view = paragraphView("hello world", 400, 60, YES);
  EXPECT_EQ((int)[view accessibilityNumberOfCharacters], 11);
  EXPECT_EQ(std::string([[view accessibilityStringForRange:NSMakeRange(6, 5)] UTF8String]),
            std::string("world"));
  // Past the end is clamped rather than throwing, which is what VoiceOver asks
  // for the moment a paragraph shrinks under it.
  EXPECT_EQ(std::string([[view accessibilityStringForRange:NSMakeRange(6, 99)] UTF8String]),
            std::string("world"));
  EXPECT_EQ(std::string([[view accessibilityStringForRange:NSMakeRange(99, 1)] UTF8String]),
            std::string(""));
  // The attributed form carries the run's font, which is what the plain one
  // cannot say.
  NSAttributedString *rich = [view accessibilityAttributedStringForRange:NSMakeRange(0, 5)];
  EXPECT_EQ((int)rich.length, 5);
  EXPECT(rich.length > 0 && [rich attribute:NSFontAttributeName atIndex:0 effectiveRange:nullptr] != nil);
}

TEST(appkit_selection_a_view_with_no_paragraph_has_nothing_to_read) {
  RnAppKitView *view = [RnAppKitView viewWithTag:3];
  EXPECT_EQ((int)[view accessibilityNumberOfCharacters], 0);
  EXPECT_EQ(std::string([[view accessibilityStringForRange:NSMakeRange(0, 5)] UTF8String]),
            std::string(""));
  EXPECT_EQ((int)[view accessibilitySelectedTextRange].length, 0);
}

TEST(appkit_selection_a_selection_is_what_a_screen_reader_is_told_is_selected) {
  RnAppKitView *view = paragraphView("Selectable text", 400, 60, YES);
  [view setRnTextSelectionStart:0 length:10];

  EXPECT_EQ(std::string([[view accessibilitySelectedText] UTF8String]), std::string("Selectable"));
  const NSRange range = [view accessibilitySelectedTextRange];
  EXPECT_EQ((int)range.location, 0);
  EXPECT_EQ((int)range.length, 10);
  // The same span said twice: what the clipboard would take is what VoiceOver
  // is told is selected.
  EXPECT_EQ(std::string([[view accessibilityStringForRange:range] UTF8String]),
            std::string([[view accessibilitySelectedText] UTF8String]));

  [view setRnTextSelectionStart:0 length:0];
  EXPECT_EQ((int)[view accessibilitySelectedTextRange].length, 0);
  EXPECT_EQ(std::string([[view accessibilitySelectedText] UTF8String]), std::string(""));
}

TEST(appkit_selection_a_screen_reader_can_ask_which_line_an_index_is_on) {
  // Narrow enough to wrap, so there is more than one line to be on.
  RnAppKitView *view = paragraphView("hello wonderful world", 120, 200, YES);
  const NSRange first = [view accessibilityRangeForLine:0];
  EXPECT_EQ((int)first.location, 0);
  EXPECT(first.length > 0);
  EXPECT(first.length < 21);
  EXPECT_EQ((int)[view accessibilityLineForIndex:0], 0);
  // An index on the second line is on a later line than one on the first,
  // which is the claim that does not depend on where this font wraps.
  EXPECT([view accessibilityLineForIndex:(NSInteger)NSMaxRange(first)] > 0);
  // A line nobody has is NSNotFound rather than an empty range at zero.
  EXPECT_EQ((int)[view accessibilityRangeForLine:99].location, (int)NSNotFound);
}

TEST(appkit_selection_an_index_is_a_whole_character_rather_than_half_a_pair) {
  RnAppKitView *view = paragraphView(kWithEmoji, 400, 60, YES);
  // "a ", then the guitar, which is two code units and one character.
  const NSRange composed = [view accessibilityRangeForIndex:2];
  EXPECT_EQ((int)composed.location, 2);
  EXPECT_EQ((int)composed.length, 2);
  // Asked about the second half, the answer is still the whole thing.
  EXPECT_EQ((int)[view accessibilityRangeForIndex:3].location, 2);
  EXPECT_EQ((int)[view accessibilityRangeForIndex:99].location, (int)NSNotFound);
}
