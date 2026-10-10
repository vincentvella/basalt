// Selecting a paragraph's text with a pointer.
//
// Three questions, and the GTK half of each: where a point lands in the text,
// what the highlight looks like, and what a press and a drag do. The state
// machine behind the third is core/TextSelection.h and has its own tests; these
// are the ones that need Pango, a widget and a pointer.
//
// The pixels matter here more than usual. A selection is a wash under the
// glyphs, so the tree dump cannot show it and neither can a size: a highlight
// drawn at the wrong offset, or not drawn at all, looks identical to every
// assertion that is not about colour at a point.

#include "TestHarness.h"

#include "GtkMountingManager.h"
#include "GtkPixels.h"
#include "GtkTouchDispatcher.h"
#include "PangoTextLayout.h"
#include "RnView.h"
#include "PlatformServices.h"
#include "TextHighlight.h"
#include "TextSelection.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

#include <sstream>
#include <string>

using facebook::react::AttributedString;
using facebook::react::ParagraphAttributes;
using facebook::react::TextAttributes;

namespace {

// A window with a selectable paragraph in it, allocated and drawn.
struct Paragraph {
  GtkWidget *window;
  RnView *root;
  RnView *view;

  Paragraph(const char *text, float width, float height, bool selectable = true) {
    root = rn_view_new(1);
    window = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(window), static_cast<int>(width),
                               static_cast<int>(height));
    gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(root));
    rn_view_set_frame(root, 0.0F, 0.0F, width, height);

    view = rn_view_new(7);
    rn_view_set_frame(view, 0.0F, 0.0F, width, height);
    rn_view_insert_child(root, view, 0);

    TextAttributes attributes;
    attributes.fontSize = 24.0F;
    AttributedString::Fragment fragment;
    fragment.string = text;
    fragment.textAttributes = attributes;
    AttributedString string;
    string.appendFragment(std::move(fragment));

    PangoLayout *layout = basalt::buildTextLayout(string, ParagraphAttributes{}, width);
    const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
    rn_view_set_text_layout(view, layout, &black);
    g_object_unref(layout);
    rn_view_set_text_selectable(view, selectable ? TRUE : FALSE);

    gtk_widget_set_visible(window, TRUE);
    pump();
  }

  ~Paragraph() {
    gtk_window_destroy(GTK_WINDOW(window));
  }

  Paragraph(const Paragraph &) = delete;
  Paragraph &operator=(const Paragraph &) = delete;

  static void pump() {
    const gint64 deadline = g_get_monotonic_time() + 500000;
    while (g_get_monotonic_time() < deadline) {
      while (g_main_context_iteration(nullptr, FALSE)) {
      }
      g_usleep(1000);
    }
  }
};

// How many pixels in a band are the selection's blue, which is a wash over
// nothing: the view has no background, so what the highlight leaves is a
// translucent blue, and the blue channel is well clear of the other two.
int highlightPixels(const basalt::testing::RnPixels &pixels, int fromX, int toX, int height) {
  int count = 0;
  for (int x = fromX; x < toX; x++) {
    for (int y = 0; y < height; y++) {
      const basalt::testing::RnPixel pixel = pixels.at(x, y);
      if (pixel.alpha > 40 && pixel.blue > pixel.red + 20 && pixel.blue > pixel.green + 20) {
        count++;
      }
    }
  }
  return count;
}

} // namespace

TEST(selection_the_index_at_a_point_follows_the_text) {
  Paragraph paragraph("Selectable text", 400.0F, 60.0F);

  // The far left is the start, and a point past the end of the only line is the
  // end of the string: a drag off the end of a line selects to the end of it
  // rather than stopping where the glyphs do.
  EXPECT_EQ(rn_view_text_index_at(paragraph.view, 0.0, 10.0), 0);
  EXPECT_EQ(rn_view_text_index_at(paragraph.view, 399.0, 10.0),
            static_cast<int>(std::string("Selectable text").size()));

  // And somewhere in the middle is somewhere in the middle, which is as much as
  // a font-independent assertion can say.
  const int middle = rn_view_text_index_at(paragraph.view, 90.0, 10.0);
  EXPECT(middle > 0);
  EXPECT(middle < static_cast<int>(std::string("Selectable text").size()));
}

TEST(selection_a_view_with_no_paragraph_has_no_index) {
  RnView *view = rn_view_new(3);
  g_object_ref_sink(view);
  EXPECT_EQ(rn_view_text_index_at(view, 10.0, 10.0), -1);
  g_object_unref(view);
}

TEST(selection_the_highlight_is_drawn_under_the_selected_range) {
  Paragraph paragraph("Selectable text", 400.0F, 60.0F);

  // Nothing selected: no wash anywhere, which is the control for the two
  // assertions below.
  {
    const basalt::testing::RnPixels pixels =
        basalt::testing::renderView(paragraph.view, 400, 60);
    EXPECT_EQ(highlightPixels(pixels, 0, 400, 60), 0);
  }

  // The first four characters, which at 24pt is the left end of the line and
  // nowhere near the right.
  rn_view_set_text_selection(paragraph.view, 0, 4);
  {
    const basalt::testing::RnPixels pixels =
        basalt::testing::renderView(paragraph.view, 400, 60);
    EXPECT(highlightPixels(pixels, 0, 60, 60) > 100);
    EXPECT_EQ(highlightPixels(pixels, 250, 400, 60), 0);
  }

  // Clearing it takes the wash away rather than leaving the last one drawn.
  rn_view_set_text_selection(paragraph.view, 0, 0);
  {
    const basalt::testing::RnPixels pixels =
        basalt::testing::renderView(paragraph.view, 400, 60);
    EXPECT_EQ(highlightPixels(pixels, 0, 400, 60), 0);
  }
}

TEST(selection_a_paragraph_that_stops_being_selectable_loses_its_highlight) {
  // The prop going false is an app saying this text is not to be selected, and
  // a highlight left drawn would be the one state nothing can clear.
  Paragraph paragraph("Selectable text", 400.0F, 60.0F);
  rn_view_set_text_selection(paragraph.view, 0, 6);
  rn_view_set_text_selectable(paragraph.view, FALSE);
  EXPECT_EQ(rn_view_get_text_selection_length(paragraph.view), 0);

  const basalt::testing::RnPixels pixels = basalt::testing::renderView(paragraph.view, 400, 60);
  EXPECT_EQ(highlightPixels(pixels, 0, 400, 60), 0);
}

TEST(selection_a_rebuilt_paragraph_keeps_a_selection_of_the_same_text) {
  // Every mutation that touches a paragraph builds a new layout, a parent
  // re-rendering being enough, so dropping the selection each time would make
  // it impossible to keep one in a live app.
  Paragraph paragraph("Selectable text", 400.0F, 60.0F);
  rn_view_set_text_selection(paragraph.view, 0, 10);

  const auto layoutFor = [](const char *text, float width) {
    TextAttributes attributes;
    attributes.fontSize = 24.0F;
    AttributedString::Fragment fragment;
    fragment.string = text;
    fragment.textAttributes = attributes;
    AttributedString string;
    string.appendFragment(std::move(fragment));
    return basalt::buildTextLayout(string, ParagraphAttributes{}, width);
  };

  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  PangoLayout *same = layoutFor("Selectable text", 400.0F);
  rn_view_set_text_layout(paragraph.view, same, &black);
  g_object_unref(same);
  EXPECT_EQ(rn_view_get_text_selection_length(paragraph.view), 10);

  // Different text is a different string, and the old offsets would highlight
  // whatever now sits at them.
  PangoLayout *other = layoutFor("Something else entirely", 400.0F);
  rn_view_set_text_layout(paragraph.view, other, &black);
  g_object_unref(other);
  EXPECT_EQ(rn_view_get_text_selection_length(paragraph.view), 0);
}

TEST(selection_the_selected_text_is_what_the_clipboard_would_take) {
  Paragraph paragraph("Selectable text", 400.0F, 60.0F);

  EXPECT(rn_view_copy_selected_text(paragraph.view) == nullptr);

  rn_view_set_text_selection(paragraph.view, 0, 10);
  char *selected = rn_view_copy_selected_text(paragraph.view);
  EXPECT(selected != nullptr);
  EXPECT_EQ(std::string(selected != nullptr ? selected : ""), std::string("Selectable"));
  g_free(selected);

  // A range that runs past the end of a paragraph that was re-laid-out under
  // it: clamped rather than read past, which is a crash on a shorter string.
  rn_view_set_text_selection(paragraph.view, 11, 1000);
  selected = rn_view_copy_selected_text(paragraph.view);
  EXPECT_EQ(std::string(selected != nullptr ? selected : ""), std::string("text"));
  g_free(selected);
}

TEST(selection_a_drag_across_selectable_text_selects_it) {
  Paragraph paragraph("Selectable text", 400.0F, 60.0F);
  basalt::GtkMountingManager manager;
  basalt::GtkTouchDispatcher dispatcher(&manager, paragraph.root);

  // Ten steps from the left edge to the middle of the line, which is a sweep
  // rather than a tap and so claims the gesture.
  dispatcher.synthesiseDrag(2.0, 10.0, 150.0, 10.0, 10);
  EXPECT(rn_view_get_text_selection_length(paragraph.view) > 0);
  EXPECT_EQ(rn_view_get_text_selection_start(paragraph.view), 0);

  // And a tap afterwards clears it, which is how a person dismisses a
  // highlight.
  dispatcher.synthesiseTap(200.0, 10.0);
  EXPECT_EQ(rn_view_get_text_selection_length(paragraph.view), 0);
}

TEST(selection_a_drag_across_text_that_is_not_selectable_selects_nothing) {
  Paragraph paragraph("Selectable text", 400.0F, 60.0F, /*selectable=*/false);
  basalt::GtkMountingManager manager;
  basalt::GtkTouchDispatcher dispatcher(&manager, paragraph.root);

  dispatcher.synthesiseDrag(2.0, 10.0, 150.0, 10.0, 10);
  EXPECT_EQ(rn_view_get_text_selection_length(paragraph.view), 0);
}

TEST(selection_a_backwards_drag_selects_from_where_it_ended) {
  Paragraph paragraph("Selectable text", 400.0F, 60.0F);
  basalt::GtkMountingManager manager;
  basalt::GtkTouchDispatcher dispatcher(&manager, paragraph.root);

  dispatcher.synthesiseDrag(150.0, 10.0, 2.0, 10.0, 10);
  EXPECT_EQ(rn_view_get_text_selection_start(paragraph.view), 0);
  EXPECT(rn_view_get_text_selection_length(paragraph.view) > 0);
}

TEST(selection_copying_puts_the_selected_text_on_the_clipboard) {
  // The two ways a copy is asked for -- `Copy` in a menu and Ctrl+C -- both go
  // through `basalt::copySelectedText`, which asks whoever owns the selection.
  // The dispatcher is what sets that up, in its constructor, which is why this
  // test builds one it otherwise does not use.
  Paragraph paragraph("Selectable text", 400.0F, 60.0F);
  basalt::GtkMountingManager manager;
  basalt::GtkTouchDispatcher dispatcher(&manager, paragraph.root);

  const std::string original = basalt::clipboardText();

  // Nothing selected: false, which is the answer that tells a `Copy` role to
  // ask the focused widget instead.
  EXPECT(!basalt::copySelectedText());

  dispatcher.synthesiseDrag(2.0, 10.0, 150.0, 10.0, 10);
  EXPECT(rn_view_get_text_selection_length(paragraph.view) > 0);
  EXPECT(basalt::copySelectedText());

  char *expected = rn_view_copy_selected_text(paragraph.view);
  EXPECT(expected != nullptr);
  EXPECT_EQ(basalt::clipboardText(), std::string(expected != nullptr ? expected : ""));
  g_free(expected);

  basalt::setClipboardText(original);
  Paragraph::pump();
}

TEST(selection_the_copy_role_copies_the_selection_rather_than_the_focused_widget) {
  // What Cmd+C and a context menu's `Copy` both perform. The role used to mean
  // "whatever has focus", which is a <TextInput> and nothing else; a paragraph
  // holds no focus, so without this branch a selected paragraph and a Copy in
  // the menu over it did nothing at all.
  Paragraph paragraph("Selectable text", 400.0F, 60.0F);
  basalt::GtkMountingManager manager;
  basalt::GtkTouchDispatcher dispatcher(&manager, paragraph.root);

  const std::string original = basalt::clipboardText();

  dispatcher.synthesiseDrag(2.0, 10.0, 150.0, 10.0, 10);
  char *expected = rn_view_copy_selected_text(paragraph.view);
  EXPECT(expected != nullptr);

  basalt::performMenuRole("copy");
  EXPECT_EQ(basalt::clipboardText(), std::string(expected != nullptr ? expected : ""));
  g_free(expected);

  basalt::setClipboardText(original);
  Paragraph::pump();
}
