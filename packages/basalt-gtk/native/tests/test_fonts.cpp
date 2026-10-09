// Fonts loaded while the app is running, which is the seam `expo-font` arrives
// through, and the GTK half of core/FontRegistry.h.
//
// The half worth testing is not that fontconfig can add a file. It is that the
// name the app chose keeps working afterwards: a font file carries its own
// family name inside it, usually not the one the app passed to `loadAsync`, so
// `fontFamily: 'TheAppsName'` only renders in that font if the registry
// remembers the mapping and Pango is asked for the mapped name.
//
// The file is a real one, found through fontconfig rather than added to this
// repository as a fixture. Registering a font the machine already has, under a
// second name of the app's choosing, exercises exactly that mapping and does it
// on any machine that can render text at all. Monospaced on purpose: the width
// assertions compare against the default proportional family, and two
// proportional faces can measure the same string to the same width.
//
// A family the machine does *not* have is the case `expo-font` really exists
// for, and it gets its own test at the bottom of this file:
// tests/RenamedFontFile.h renames the family inside a copy of that same file,
// which is the one change that makes a font new. That is also the only test
// here that can see Pango's font map being reloaded, since a family already in
// fontconfig's index is one the map already had.

#include "TestHarness.h"

#include "FontRegistry.h"
#include "PangoTextLayout.h"
#include "RenamedFontFile.h"

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/textlayoutmanager/TextLayoutManager.h>
#include <react/utils/ContextContainer.h>

#include <cmath>
#include <filesystem>
#include <string>

namespace {

struct SystemFont {
  std::string path;
  // What fontconfig's own index calls it, which is not read out of the file and
  // so is not the same answer `registerFont` arrives at.
  std::string family;
};

SystemFont monospaceFont() {
  if (FcInit() == FcFalse) {
    return {};
  }
  FcPattern *wanted = FcNameParse(reinterpret_cast<const FcChar8 *>("monospace"));
  if (wanted == nullptr) {
    return {};
  }
  FcConfigSubstitute(nullptr, wanted, FcMatchPattern);
  FcDefaultSubstitute(wanted);

  FcResult result = FcResultNoMatch;
  FcPattern *matched = FcFontMatch(nullptr, wanted, &result);
  SystemFont found;
  if (matched != nullptr) {
    FcChar8 *file = nullptr;
    if (FcPatternGetString(matched, FC_FILE, 0, &file) == FcResultMatch && file != nullptr) {
      found.path = reinterpret_cast<const char *>(file);
    }
    FcChar8 *family = nullptr;
    if (FcPatternGetString(matched, FC_FAMILY, 0, &family) == FcResultMatch && family != nullptr) {
      found.family = reinterpret_cast<const char *>(family);
    }
    FcPatternDestroy(matched);
  }
  FcPatternDestroy(wanted);
  return found;
}

facebook::react::AttributedString paragraphIn(const std::string &family, const char *text) {
  facebook::react::TextAttributes attributes;
  attributes.fontSize = 24.0F;
  attributes.fontFamily = family;

  facebook::react::AttributedString::Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;

  facebook::react::AttributedString string;
  string.appendFragment(std::move(fragment));
  return string;
}

// The family Pango settled on for a paragraph that asked for `family`, taken
// from the font it resolved rather than from the description it was handed.
// That is the difference between a name having been written down and a font
// having been found: an unknown family reaches Pango unchanged and fontconfig
// substitutes something, which is what makes the negative control below mean
// anything.
std::string resolvedFamilyFor(const std::string &family) {
  PangoLayout *layout =
      basalt::buildTextLayout(paragraphIn(family, "Handgloves"),
                              facebook::react::ParagraphAttributes{}, -1.0F);
  std::string resolved;
  PangoLayoutIter *iter = pango_layout_get_iter(layout);
  PangoLayoutRun *run = pango_layout_iter_get_run_readonly(iter);
  if (run != nullptr && run->item->analysis.font != nullptr) {
    PangoFontDescription *description = pango_font_describe(run->item->analysis.font);
    const char *name = pango_font_description_get_family(description);
    if (name != nullptr) {
      resolved = name;
    }
    pango_font_description_free(description);
  }
  pango_layout_iter_free(iter);
  g_object_unref(layout);
  return resolved;
}

float widthFor(const std::string &family) {
  PangoLayout *layout =
      basalt::buildTextLayout(paragraphIn(family, "Handgloves"),
                              facebook::react::ParagraphAttributes{}, -1.0F);
  float width = 0.0F;
  float height = 0.0F;
  basalt::textLayoutSize(layout, &width, &height);
  g_object_unref(layout);
  return width;
}

} // namespace

TEST(fonts_a_font_loaded_at_runtime_resolves_to_the_family_in_the_file) {
  const SystemFont found = monospaceFont();
  EXPECT(!found.path.empty());

  const std::string alias = "BasaltResolvedAlias";
  EXPECT(basalt::registerFont(alias, found.path));
  EXPECT(basalt::isFontRegistered(alias));

  // The point of the registry: what the app calls it maps to what the file
  // calls itself, which is the name fontconfig indexes by.
  const std::string resolved = basalt::resolveFontFamily(alias);
  EXPECT(resolved != alias);
  EXPECT_EQ(resolved, found.family);

  // And a name nobody registered comes back unchanged, so a system family
  // reaches Pango as itself rather than being rewritten.
  EXPECT_EQ(basalt::resolveFontFamily("Nimbus Sans"), std::string("Nimbus Sans"));
  EXPECT(!basalt::isFontRegistered("BasaltNeverLoaded"));
}

TEST(fonts_text_asking_for_a_runtime_family_is_laid_out_in_it) {
  const SystemFont found = monospaceFont();
  EXPECT(!found.path.empty());

  const std::string alias = "BasaltLaidOutAlias";
  EXPECT(basalt::registerFont(alias, found.path));

  // Asking for the app's name gets the same font as asking for the family
  // inside the file, which is what the mapping is for.
  EXPECT_EQ(resolvedFamilyFor(alias), resolvedFamilyFor(found.family));
  EXPECT_EQ(resolvedFamilyFor(alias), found.family);

  // The negative control, and the reason the assertion above is not vacuous: a
  // family nobody has reaches Pango unchanged, fontconfig substitutes the
  // default, and the text measures differently. A registry that forgot the
  // mapping would land here instead.
  const std::string fallback = resolvedFamilyFor("BasaltNeverLoaded");
  EXPECT(fallback != found.family);
  EXPECT(std::fabs(widthFor(alias) - widthFor("BasaltNeverLoaded")) > 0.5F);
}

TEST(fonts_a_font_registered_after_a_measurement_invalidates_it) {
  const SystemFont found = monospaceFont();
  EXPECT(!found.path.empty());

  // This is how every loader works, `useFonts` included: the app renders first,
  // in whatever the fallback is, and the font arrives afterwards. The
  // measurement from before has to stop counting, or the paragraph keeps the
  // fallback's size for the rest of the session and the font appears to have
  // done nothing.
  const std::string alias = "BasaltCacheAlias";
  EXPECT(!basalt::isFontRegistered(alias));

  const facebook::react::TextLayoutManager manager(
      std::make_shared<const facebook::react::ContextContainer>());
  const auto widthNow = [&] {
    return manager
        .measure(facebook::react::AttributedStringBox{paragraphIn(alias, "Handgloves")},
                 facebook::react::ParagraphAttributes{},
                 facebook::react::TextLayoutContext{},
                 facebook::react::LayoutConstraints{})
        .size.width;
  };

  const float fallbackWidth = widthNow();
  EXPECT(fallbackWidth > 0.0F);
  // Measured again with nothing changed, to show the cache is in the way: if
  // these two differed, the assertion below would prove nothing.
  EXPECT(std::fabs(widthNow() - fallbackWidth) < 0.01F);

  const unsigned long before = basalt::fontGeneration();
  EXPECT(basalt::registerFont(alias, found.path));
  EXPECT(basalt::fontGeneration() != before);

  EXPECT(std::fabs(widthNow() - fallbackWidth) > 0.5F);
}

TEST(fonts_a_font_the_system_does_not_have_becomes_usable) {
  const SystemFont found = monospaceFont();
  EXPECT(!found.path.empty());

  const std::filesystem::path copy =
      std::filesystem::temp_directory_path() / "basalt-gtk-renamed-font.ttf";
  const basalt::test::RenamedFont renamed =
      basalt::test::renameFontFile(found.path, found.family, copy.string());
  EXPECT(!renamed.path.empty());

  // Before: nobody has this family, so Pango substitutes something else for it.
  EXPECT(resolvedFamilyFor(renamed.family) != renamed.family);

  const std::string alias = "BasaltBundledAlias";
  EXPECT(basalt::registerFont(alias, renamed.path));
  EXPECT_EQ(basalt::resolveFontFamily(alias), renamed.family);

  // GTK on macOS, where this suite is often run next to the AppKit one, is as
  // far as this test can go. Measured rather than assumed: Homebrew's Pango
  // answers `pango_cairo_font_map_get_default` with a
  // `PangoCairoCoreTextFontMap`, which does not look at fontconfig at all, so a
  // font added to fontconfig is invisible to it however loudly it is announced.
  // The mapping above is what the registry owns and is asserted either way; the
  // rest of this test is a Linux fact and is proved there.
  if (PANGO_IS_FC_FONT_MAP(pango_cairo_font_map_get_default()) == FALSE) {
    std::filesystem::remove(copy);
    return;
  }

  // After: the app's name reaches it, and so does the family inside the file.
  // This is what `FcConfigAppFontAddFile` plus the font map being told about it
  // buys, and the only assertion in this file that can tell.
  EXPECT_EQ(resolvedFamilyFor(alias), renamed.family);
  EXPECT_EQ(resolvedFamilyFor(renamed.family), renamed.family);

  std::filesystem::remove(copy);
}
