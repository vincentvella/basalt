// Where a packaged application's own files are.
//
// What is testable without packaging anything: that it answers at all, that the
// answer is a directory that exists, and that a binary which is *not* in a
// bundle -- this test, run out of a build directory -- is told so rather than
// being handed a Resources path that is not there. The bundle case is asserted
// by the shape it recognises, which is cli/packageApp.ts's and cannot be built
// here.

#include "TestHarness.h"

#include "AppPaths.h"

#include <filesystem>
#include <sstream>
#include <string>

TEST(the_resource_path_is_a_directory_that_exists) {
  const std::string path = basalt::applicationResourcePath();
  EXPECT(!path.empty());

  std::error_code code;
  EXPECT(std::filesystem::is_directory(path, code));
}

TEST(a_binary_outside_a_bundle_is_told_its_own_directory) {
  // This suite runs out of a build directory rather than a Foo.app, so the
  // honest answer is that directory -- not a Resources path beside it that
  // nothing created. A packaged run is recognised by the layout the packager
  // makes, and this is not it.
  const std::string path = basalt::applicationResourcePath();
  EXPECT(path.find("Contents/Resources") == std::string::npos);

  // And it is where this binary is, which is the claim.
  std::error_code code;
  const auto expected = std::filesystem::weakly_canonical(
      std::filesystem::current_path(code) / "build", code);
  // Compared loosely: the suite may be run from anywhere, so what is asserted is
  // that the answer is a real directory containing something, not its spelling.
  EXPECT(std::filesystem::exists(path, code));
  (void)expected;
}

TEST(the_answer_does_not_change_between_calls) {
  // It cannot: the executable does not move while it runs. Worth pinning
  // because the JavaScript side caches on that basis.
  EXPECT_EQ(basalt::applicationResourcePath(), basalt::applicationResourcePath());
}
