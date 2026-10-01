// The bundle's own URL, which is also where every asset is looked for.
//
// Core's, so compiled into every platform's suite -- and the one thing here
// worth testing hard is the shape of the string rather than its contents,
// because React Native takes it apart with string operations that are silent
// when they fail.
//
// `resolveAssetSource` finds the directory beside the bundle with
// `scriptURL.lastIndexOf('/')`. A Windows path has no forward slash in it, so
// that is -1, the directory comes out empty, and every `require()`d image
// resolves to `file://assets/...` -- which opens nothing and says nothing. That
// is what these assertions are for.

#include "TestHarness.h"

#include "SourceCodeModule.h"

#include <sstream>
#include <string>

namespace {

// Where the directory ends, which is the only thing React Native asks of this
// string other than its `file://` prefix.
std::string directoryOf(const std::string &url) {
  const std::size_t slash = url.rfind('/');
  return slash == std::string::npos ? std::string() : url.substr(0, slash + 1);
}

} // namespace

TEST(sourcecode_a_bundle_url_is_a_file_url_with_a_directory_in_it) {
  const std::string url = basalt::scriptURLFor("build/main.jsbundle.js", false, "", 0, "", "linux");

  EXPECT(url.rfind("file:///", 0) == 0);
  // Three slashes and then something: `file:///` alone would mean the
  // directory is the filesystem root, which is what an empty path looks like.
  EXPECT(url.size() > std::string("file:///").size());
  // A directory that is more than the scheme. This is the assertion that fails
  // on a path with no forward slashes in it.
  EXPECT(directoryOf(url).size() > std::string("file:///").size());
  EXPECT(url.find("main.jsbundle.js") != std::string::npos);
  // No backslashes, on any platform: a Windows path reaches JavaScript as a URL
  // and is taken apart as one.
  EXPECT(url.find('\\') == std::string::npos);
}

TEST(sourcecode_a_relative_bundle_path_is_made_absolute) {
  const std::string url = basalt::scriptURLFor("main.jsbundle.js", false, "", 0, "", "macos");
  // `file://main.jsbundle.js` would be a URI with an authority and no path, and
  // the directory beside it would be nothing at all.
  EXPECT(url.rfind("file:///", 0) == 0);
  EXPECT(directoryOf(url).size() > std::string("file:///").size());
}

TEST(sourcecode_dev_mode_reports_the_server_rather_than_the_file) {
  const std::string url = basalt::scriptURLFor("build/main.jsbundle.js", true, "localhost", 8081, "index", "windows");
  // Shaped like the URL the dev server served, because React Native recovers
  // the server's address by stripping the path off this -- and assets then
  // resolve to that server, with nothing copied anywhere.
  EXPECT_EQ(url, std::string("http://localhost:8081/index.bundle?platform=windows&dev=true"));
}
