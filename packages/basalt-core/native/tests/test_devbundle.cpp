// Tests for telling the bundle request apart from every other request, and for
// what is kept when it fails.
//
// Core's, so compiled into both platforms' suites: which toolkit is drawing has
// nothing to do with what Metro answered.
//
// The predicate is the part worth proving. It decides whether a response body
// reaches the JS engine, so a false negative puts Metro's error page back in
// front of the compiler and a false positive would swallow the body of an app's
// own fetch -- a 404 from an API, which JavaScript is entitled to read.

#include "TestHarness.h"

#include "DevBundle.h"
#include "SourceCodeModule.h"

#include <sstream>
#include <string>

namespace {

const char *kOriginHost = "localhost";
constexpr uint32_t kOriginPort = 8081;

void withDevServer() {
  basalt::setDevServerOrigin(kOriginHost, kOriginPort);
}

} // namespace

TEST(the_bundle_request_is_recognised) {
  withDevServer();
  EXPECT(basalt::isDevBundleRequest(
      "GET",
      "http://localhost:8081/index.bundle?platform=android&dev=true&app=basalt-macos"));
  // Metro is also happy to serve it with no query at all.
  EXPECT(basalt::isDevBundleRequest("GET", "http://localhost:8081/index.bundle"));
  // Expo's entry point is not called index.
  EXPECT(basalt::isDevBundleRequest("GET", "http://localhost:8081/node_modules/expo/AppEntry.bundle?platform=ios"));
}

TEST(other_dev_server_traffic_is_not_the_bundle) {
  withDevServer();
  // Symbolication posts a stack; it is not a bundle and its body is wanted.
  EXPECT(!basalt::isDevBundleRequest("POST", "http://localhost:8081/symbolicate"));
  // Assets are served by Metro too, out of the same origin.
  EXPECT(!basalt::isDevBundleRequest("GET", "http://localhost:8081/assets/checker.png"));
  EXPECT(!basalt::isDevBundleRequest("GET", "http://localhost:8081/status"));
  // A source map sits next to the bundle and shares its name.
  EXPECT(!basalt::isDevBundleRequest("GET", "http://localhost:8081/index.map?platform=ios"));
}

TEST(an_apps_own_request_is_never_the_bundle) {
  withDevServer();
  // The point of anchoring on the origin: another server's `.bundle` is none of
  // this code's business, and its body belongs to whoever asked for it.
  EXPECT(!basalt::isDevBundleRequest("GET", "https://example.com/index.bundle"));
  EXPECT(!basalt::isDevBundleRequest("GET", "http://localhost:9000/index.bundle"));
  // A prefix match on the host alone would accept this one.
  EXPECT(!basalt::isDevBundleRequest("GET", "http://localhost:80811/index.bundle"));
}

TEST(metros_json_error_is_unwrapped) {
  withDevServer();
  basalt::recordDevBundleError(
      500,
      "http://localhost:8081/index.bundle",
      R"json({"type":"TransformError","message":"App.js: Unexpected token (12:4)","errors":[]})json");

  const auto error = basalt::devBundleError();
  EXPECT(error.has_value());
  EXPECT_EQ(static_cast<int>(error->status), 500);
  // The message, not the JSON around it: it is what the developer has to read.
  EXPECT_EQ(error->message, std::string("App.js: Unexpected token (12:4)"));
}

TEST(a_body_that_is_not_metros_is_kept_whole) {
  withDevServer();
  // A proxy, a tunnel's error page, a plain string. Unparseable is not a reason
  // to report nothing -- whatever answered is the only clue there is.
  basalt::recordDevBundleError(502, "http://localhost:8081/index.bundle", "<html>Bad Gateway</html>");

  const auto error = basalt::devBundleError();
  EXPECT(error.has_value());
  EXPECT_EQ(static_cast<int>(error->status), 502);
  EXPECT_EQ(error->message, std::string("<html>Bad Gateway</html>"));
}

TEST(a_dev_script_url_names_the_hosts_own_platform) {
  // HMRClient registers this URL with Metro for Fast Refresh, and Metro sends
  // updates for the platform it names. It used to say linux on every host, so
  // a Windows app registered a Linux module graph and no edit ever reached it.
  EXPECT_EQ(basalt::scriptURLFor("build/main.jsbundle.js", true, "localhost", 8081, "index", "windows"),
            std::string("http://localhost:8081/index.bundle?platform=windows&dev=true"));
  EXPECT_EQ(basalt::scriptURLFor("x.js", true, "127.0.0.1", 19000, "node_modules/expo/AppEntry", "macos"),
            std::string("http://127.0.0.1:19000/node_modules/expo/AppEntry.bundle?platform=macos&dev=true"));

  // Out of dev mode the platform plays no part: the answer is the file.
  const std::string file =
      basalt::scriptURLFor("build/main.jsbundle.js", false, "localhost", 8081, "index", "windows");
  EXPECT(file.rfind("file://", 0) == 0);
  EXPECT(file.find("main.jsbundle.js") != std::string::npos);
}
