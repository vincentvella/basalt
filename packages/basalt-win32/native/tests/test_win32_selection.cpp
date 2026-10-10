// Selecting a paragraph's text with a pointer, on Windows.
//
// The same three questions the GTK and AppKit suites ask, over the same claims,
// because the point of asking three times is that the hosts agree: where a
// point lands in the text, what the highlight looks like, and what a press and
// a drag do. The state machine behind the third is core/TextSelection.h and has
// its own tests.
//
// This host is the one that can render without a window, so the highlight is
// asserted against real pixels here the same way the other two assert it --
// a selection is a wash under the glyphs, so no tree dump can show it.

#include "TestHarness.h"

#include "RnWin32TextLayout.h"
#include "RnWin32View.h"
#include "TextSelection.h"
#include "Win32MountingManager.h"
#include "Win32Snapshot.h"
#include "Win32TouchDispatcher.h"

#include <memory>
#include <sstream>
#include <string>

using basalt::Win32MountingManager;
using basalt::Win32TouchDispatcher;
using basalt::win32::RnTextStyle;
using basalt::win32::RnWin32TextLayout;
using basalt::win32::RnWin32View;

namespace {

std::shared_ptr<RnWin32TextLayout> selectableLayout(const char *text) {
  RnTextStyle style;
  style.fontSize = 24.0f;
  return RnWin32TextLayout::create(text, style, 0);
}

// A view with a paragraph in it, sized and selectable.
std::unique_ptr<RnWin32View> paragraphView(const char *text,
                                           float width,
                                           float height,
                                           bool selectable) {
  auto view = std::make_unique<RnWin32View>(7);
  view->setFrame(0, 0, width, height);
  view->setTextLayout(selectableLayout(text));
  view->setTextSelectable(selectable);
  return view;
}

// How many pixels in a band are the selection's blue, which is a wash over
// transparent black: the blue channel ends up well clear of the other two.
int highlightPixels(const RnWin32View &view, int fromX, int toX) {
  const basalt::win32::RnPixels pixels = basalt::win32::renderToPixels(view);
  int count = 0;
  for (unsigned y = 0; y < pixels.height(); y++) {
    for (int x = fromX; x < toX && x < static_cast<int>(pixels.width()); x++) {
      const basalt::win32::RnPixel pixel = pixels.at(static_cast<unsigned>(x), y);
      if (pixel.alpha > 32 && pixel.blue > pixel.red + 20 && pixel.blue > pixel.green + 20) {
        count++;
      }
    }
  }
  return count;
}

} // namespace

TEST(win32_selection_the_index_at_a_point_follows_the_text) {
  const std::unique_ptr<RnWin32View> view = paragraphView("Selectable text", 400, 60, true);

  EXPECT_EQ(view->textIndexAtPoint(0.0f, 10.0f), 0);
  EXPECT_EQ(view->textIndexAtPoint(399.0f, 10.0f),
            static_cast<int>(std::string("Selectable text").size()));

  const int middle = view->textIndexAtPoint(90.0f, 10.0f);
  EXPECT(middle > 0);
  EXPECT(middle < static_cast<int>(std::string("Selectable text").size()));
}

TEST(win32_selection_a_view_with_no_paragraph_has_no_index) {
  RnWin32View view(3);
  EXPECT_EQ(view.textIndexAtPoint(10.0f, 10.0f), -1);
}

TEST(win32_selection_the_selected_text_is_what_the_clipboard_would_take) {
  const std::unique_ptr<RnWin32View> view = paragraphView("Selectable text", 400, 60, true);
  EXPECT(view->selectedText().empty());

  view->setTextSelection(0, 10);
  EXPECT_EQ(view->selectedText(), std::string("Selectable"));

  // A range that runs past the end of a paragraph rebuilt under it: clamped
  // rather than read past, which is a crash on a shorter string.
  view->setTextSelection(11, 1000);
  EXPECT_EQ(view->selectedText(), std::string("text"));
}

TEST(win32_selection_the_highlight_is_drawn_under_the_selected_range) {
  const std::unique_ptr<RnWin32View> view = paragraphView("Selectable text", 400, 60, true);

  // Nothing selected: no wash anywhere, which is the control for the two
  // assertions below.
  EXPECT_EQ(highlightPixels(*view, 0, 400), 0);

  // The first four characters, which at 24pt is the left end of the line and
  // nowhere near the right.
  view->setTextSelection(0, 4);
  EXPECT(highlightPixels(*view, 0, 60) > 100);
  EXPECT_EQ(highlightPixels(*view, 250, 400), 0);

  // Clearing it takes the wash away rather than leaving the last one drawn.
  view->setTextSelection(0, 0);
  EXPECT_EQ(highlightPixels(*view, 0, 400), 0);
}

TEST(win32_selection_a_paragraph_that_stops_being_selectable_loses_its_highlight) {
  const std::unique_ptr<RnWin32View> view = paragraphView("Selectable text", 400, 60, true);
  view->setTextSelection(0, 6);
  view->setTextSelectable(false);
  EXPECT_EQ(view->textSelectionLength(), 0);
  EXPECT_EQ(highlightPixels(*view, 0, 400), 0);
}

TEST(win32_selection_a_rebuilt_paragraph_keeps_a_selection_of_the_same_text) {
  // Every mutation that touches a paragraph builds a new layout, a parent
  // re-rendering being enough, so dropping the selection each time would make
  // it impossible to keep one in a live app.
  const std::unique_ptr<RnWin32View> view = paragraphView("Selectable text", 400, 60, true);
  view->setTextSelection(0, 10);

  view->setTextLayout(selectableLayout("Selectable text"));
  EXPECT_EQ(view->textSelectionLength(), 10);

  // Different text is a different string, and the old offsets would highlight
  // whatever now sits at them.
  view->setTextLayout(selectableLayout("Something else entirely"));
  EXPECT_EQ(view->textSelectionLength(), 0);
}

TEST(win32_selection_a_drag_across_selectable_text_selects_it) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(1);
  root->setFrame(0, 0, 400, 60);

  // A child of the root rather than a mounted shadow view: what is under test
  // is the dispatcher's walk and the layout's hit test, and a hand-built view
  // is what the other two suites use for the same reason.
  // Held by the test rather than by the parent: a view does not own its
  // children here, the mounting registry does, and a hand-built one belongs to
  // whoever made it. See RnWin32View's tree section.
  const std::unique_ptr<RnWin32View> owned = paragraphView("Selectable text", 400, 60, true);
  RnWin32View *view = owned.get();
  root->insertChild(view, 0);

  Win32TouchDispatcher dispatcher(&manager, root);

  // Ten steps from the left edge to the middle of the line, which is a sweep
  // rather than a tap and so claims the gesture.
  dispatcher.synthesiseDrag(2, 10, 150, 10, 10);
  EXPECT(view->textSelectionLength() > 0);
  EXPECT_EQ(view->textSelectionStart(), 0);

  // What `Copy` and Ctrl+C would take, through the provider the dispatcher
  // installs: the same text the view reports.
  EXPECT_EQ(basalt::selectedText(), view->selectedText());

  // And a tap afterwards clears it, which is how a person dismisses a
  // highlight.
  dispatcher.synthesiseTap(200, 10);
  EXPECT_EQ(view->textSelectionLength(), 0);
  EXPECT(basalt::selectedText().empty());

  root->removeChild(view);
  manager.destroySurfaceRoot(1);
}

TEST(win32_selection_a_drag_across_text_that_is_not_selectable_selects_nothing) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(1);
  root->setFrame(0, 0, 400, 60);

  const std::unique_ptr<RnWin32View> owned = paragraphView("Selectable text", 400, 60, false);
  RnWin32View *view = owned.get();
  root->insertChild(view, 0);

  Win32TouchDispatcher dispatcher(&manager, root);
  dispatcher.synthesiseDrag(2, 10, 150, 10, 10);
  EXPECT_EQ(view->textSelectionLength(), 0);

  root->removeChild(view);
  manager.destroySurfaceRoot(1);
}

TEST(win32_selection_a_backwards_drag_selects_from_where_it_ended) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(1);
  root->setFrame(0, 0, 400, 60);

  // Held by the test rather than by the parent: a view does not own its
  // children here, the mounting registry does, and a hand-built one belongs to
  // whoever made it. See RnWin32View's tree section.
  const std::unique_ptr<RnWin32View> owned = paragraphView("Selectable text", 400, 60, true);
  RnWin32View *view = owned.get();
  root->insertChild(view, 0);

  Win32TouchDispatcher dispatcher(&manager, root);
  dispatcher.synthesiseDrag(150, 10, 2, 10, 10);
  EXPECT_EQ(view->textSelectionStart(), 0);
  EXPECT(view->textSelectionLength() > 0);

  root->removeChild(view);
  manager.destroySurfaceRoot(1);
}
