// Fonts loaded while the app is running, which is the seam `expo-font` arrives
// through.
//
// The half worth testing is not that Core Text can register a file. It is that
// the name the app chose keeps working afterwards: a font file carries its own
// family name inside it, usually not the one the app passed to `loadAsync`, so
// `fontFamily: 'TheAppsName'` only renders in that font if the registry
// remembers the mapping and the layout asks it. That mapping is the whole of
// core/FontRegistry.h, and until now nothing on this host asked it anything.
//
// The file is a real one, found through Core Text rather than added to this
// repository as a fixture. Registering a font the system already has, under a
// second name of the app's choosing, exercises exactly the mapping this seam
// exists for and does it on any Mac.
//
// A face the system does *not* have is the case `expo-font` really exists for,
// and it gets its own test at the bottom of this file: tests/RenamedFontFile.h
// renames the family inside a copy of that same file, which is the one change
// that makes a font new to the machine.

#include "TestHarness.h"

#import "CoreTextLayout.h"
#import "RnTextLayout.h"

#include "FontRegistry.h"
#include "RenamedFontFile.h"

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/textlayoutmanager/TextLayoutManager.h>
#include <react/utils/ContextContainer.h>

#include <cmath>
#include <filesystem>
#include <string>

namespace {

// Monospaced on purpose. The assertions below compare against the proportional
// system font, and two proportional faces can measure the same string to the
// same width, which would make a passing test mean nothing.
NSString *const kKnownFamily = @"Courier New";

// Where the system keeps that family's file. `kCTFontURLAttribute` is the only
// way to ask: a descriptor built from a family name answers with the name, and
// `registerFont` needs a path.
std::string pathOfKnownFamily() {
  NSFontDescriptor *wanted = [NSFontDescriptor fontDescriptorWithFontAttributes:@{
    NSFontFamilyAttribute : kKnownFamily,
  }];
  NSArray<NSFontDescriptor *> *matches = [wanted
      matchingFontDescriptorsWithMandatoryKeys:[NSSet setWithObject:NSFontFamilyAttribute]];
  for (NSFontDescriptor *descriptor in matches) {
    NSURL *url = (__bridge_transfer NSURL *)CTFontDescriptorCopyAttribute(
        (__bridge CTFontDescriptorRef)descriptor, kCTFontURLAttribute);
    if (url.isFileURL && url.path.length > 0) {
      return url.path.UTF8String;
    }
  }
  return {};
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

// The font Core Text was actually handed for a paragraph asking for `family`,
// which is the question the registry exists to answer. Through buildTextLayout
// rather than through fontFor directly, because the path from a prop to a font
// is what an app's `fontFamily` travels.
NSString *familyLaidOutFor(const std::string &family) {
  NSAttributedString *built =
      basalt::buildTextLayout(paragraphIn(family, "Handgloves"),
                              facebook::react::ParagraphAttributes{})
          .attributedString;
  NSFont *font = [built attribute:NSFontAttributeName atIndex:0 effectiveRange:nullptr];
  return font.familyName;
}

} // namespace

TEST(appkit_a_font_loaded_at_runtime_resolves_to_the_family_in_the_file) {
  @autoreleasepool {
    const std::string path = pathOfKnownFamily();
    EXPECT(!path.empty());

    const std::string alias = "BasaltResolvedAlias";
    EXPECT(basalt::registerFont(alias, path));
    EXPECT(basalt::isFontRegistered(alias));

    // The point of the registry: what the app calls it maps to what the file
    // calls itself, which is the name Core Text indexes by.
    EXPECT_EQ(basalt::resolveFontFamily(alias), std::string(kKnownFamily.UTF8String));

    // And a name nobody registered comes back unchanged, so a system family
    // reaches Core Text as itself rather than being rewritten.
    EXPECT_EQ(basalt::resolveFontFamily("Helvetica"), std::string("Helvetica"));
    EXPECT(!basalt::isFontRegistered("BasaltNeverLoaded"));
  }
}

TEST(appkit_text_asking_for_a_runtime_family_is_laid_out_in_it) {
  @autoreleasepool {
    const std::string path = pathOfKnownFamily();
    EXPECT(!path.empty());

    const std::string alias = "BasaltLaidOutAlias";
    EXPECT(basalt::registerFont(alias, path));

    EXPECT_EQ(std::string(familyLaidOutFor(alias).UTF8String),
              std::string(kKnownFamily.UTF8String));

    // The negative control, and the reason the assertion above is not vacuous:
    // a family the system does not have falls back to the system font, so a
    // registry that forgot the mapping would land here instead.
    NSString *fallback = familyLaidOutFor("BasaltNeverLoaded");
    EXPECT(![fallback isEqualToString:kKnownFamily]);
    EXPECT_EQ(std::string(fallback.UTF8String),
              std::string([NSFont systemFontOfSize:24].familyName.UTF8String));
  }
}

TEST(appkit_a_font_registered_after_a_measurement_invalidates_it) {
  @autoreleasepool {
    const std::string path = pathOfKnownFamily();
    EXPECT(!path.empty());

    // This is how every loader works, `useFonts` included: the app renders
    // first, in whatever the fallback is, and the font arrives afterwards. The
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
    // the two differed here, the test below would prove nothing.
    EXPECT(std::fabs(widthNow() - fallbackWidth) < 0.01F);

    const unsigned long before = basalt::fontGeneration();
    EXPECT(basalt::registerFont(alias, path));
    EXPECT(basalt::fontGeneration() != before);

    EXPECT(std::fabs(widthNow() - fallbackWidth) > 0.5F);
  }
}

TEST(appkit_a_font_the_system_does_not_have_becomes_usable) {
  @autoreleasepool {
    const std::string path = pathOfKnownFamily();
    EXPECT(!path.empty());

    const std::filesystem::path copy =
        std::filesystem::temp_directory_path() / "basalt-appkit-renamed-font.ttf";
    const basalt::test::RenamedFont renamed = basalt::test::renameFontFile(
        path, kKnownFamily.UTF8String, copy.string());
    EXPECT(!renamed.path.empty());

    // Before: nobody has this family, so a paragraph asking for it by its own
    // name gets the system font. This is the assertion that makes the rest of
    // the file more than a lookup table test -- everything above registers a
    // face Core Text already had.
    EXPECT_EQ(std::string(familyLaidOutFor(renamed.family).UTF8String),
              std::string([NSFont systemFontOfSize:24].familyName.UTF8String));

    const std::string alias = "BasaltBundledAlias";
    EXPECT(basalt::registerFont(alias, renamed.path));
    EXPECT_EQ(basalt::resolveFontFamily(alias), renamed.family);

    // After: the app's name reaches it, and so does the family inside the file,
    // which is what `CTFontManagerRegisterFontsForURL` was called for.
    EXPECT_EQ(std::string(familyLaidOutFor(alias).UTF8String), renamed.family);
    EXPECT_EQ(std::string(familyLaidOutFor(renamed.family).UTF8String), renamed.family);

    std::filesystem::remove(copy);
  }
}
