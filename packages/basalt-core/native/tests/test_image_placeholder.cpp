// Which of the two placeholder props wins, decided once.
//
// `core/ImagePlaceholder.h` exists so the three hosts cannot disagree about a
// question that is not theirs: an app names `defaultSource` or
// `loadingIndicatorSource`, and what a host does with the name is its own
// business. The drawing rule the hosts share is in that header's comment and
// tested where it happens, against real pixels.

#include "TestHarness.h"

#include "ImagePlaceholder.h"

#include <sstream>
#include <string>

using facebook::react::ImageProps;

TEST(image_placeholder_reads_default_source) {
  ImageProps props;
  props.defaultSource.uri = "file:///placeholder.png";
  EXPECT_EQ(basalt::imagePlaceholderUri(props), std::string("file:///placeholder.png"));
}

TEST(image_placeholder_reads_androids_spelling_too) {
  // `loadingIndicatorSource` is what `Image.android.js` sends, as
  // `loadingIndicatorSrc`, and this platform takes Android's Image.js.
  ImageProps props;
  props.loadingIndicatorSource.uri = "file:///spinner.png";
  EXPECT_EQ(basalt::imagePlaceholderUri(props), std::string("file:///spinner.png"));
}

TEST(image_placeholder_prefers_the_documented_one) {
  // An app that sets both has been told by Android's JavaScript that it may
  // not -- it throws with a message of its own -- so this only decides what
  // happens if one arrives anyway. `defaultSource` is the one React Native
  // documents for every platform.
  ImageProps props;
  props.defaultSource.uri = "file:///placeholder.png";
  props.loadingIndicatorSource.uri = "file:///spinner.png";
  EXPECT_EQ(basalt::imagePlaceholderUri(props), std::string("file:///placeholder.png"));
}

TEST(image_placeholder_is_empty_when_an_app_names_none) {
  ImageProps props;
  EXPECT(basalt::imagePlaceholderUri(props).empty());
}
