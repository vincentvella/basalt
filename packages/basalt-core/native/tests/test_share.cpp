// The share picker's two answers, which are the only part of sharing with a
// result that can be wrong.
//
// Core's, so compiled into every platform's suite. What is *not* here is
// `presentAlert`: it reads BASALT_TEST_DIALOG once into a static, because an
// environment variable cannot change under a running process, and a test that
// wanted two different values could not have them. That path is covered by the
// end-to-end suite, which can run the host twice.

#include "TestHarness.h"

#include "ShareFallback.h"

#include <sstream>
#include <string>

namespace {

basalt::ShareRequest requestOf(const char *message, const char *url, const char *title = "") {
  basalt::ShareRequest request;
  request.message = message;
  request.url = url;
  request.title = title;
  return request;
}

} // namespace

TEST(share_the_clipboard_gets_both_the_message_and_the_link) {
  // A link with a sentence about it is what most calls to Share are, and
  // dropping either half would be dropping half of what the app asked for.
  EXPECT_EQ(basalt::shareClipboardText(requestOf("Look at this", "https://example.com")),
            std::string("Look at this\nhttps://example.com"));
}

TEST(share_the_clipboard_gets_whichever_half_was_given) {
  EXPECT_EQ(basalt::shareClipboardText(requestOf("Look at this", "")), std::string("Look at this"));
  EXPECT_EQ(basalt::shareClipboardText(requestOf("", "https://example.com")),
            std::string("https://example.com"));
  // React Native's own invariant refuses this before it reaches a platform, so
  // it is only here to say that nothing here falls over if it ever does.
  EXPECT_EQ(basalt::shareClipboardText(requestOf("", "")), std::string(""));
}

TEST(share_a_mailto_carries_the_body_and_the_subject) {
  const std::string url =
      basalt::shareMailtoUrl(requestOf("Look at this", "https://example.com", "A link"));
  EXPECT(url.rfind("mailto:?body=", 0) == 0);
  EXPECT(url.find("subject=A%20link") != std::string::npos);
  // The newline between the two halves, encoded rather than literal: a raw one
  // ends the URL as far as most mail clients are concerned.
  EXPECT(url.find("%0A") != std::string::npos);
}

TEST(share_a_mailto_encodes_what_would_otherwise_change_its_meaning) {
  const std::string url = basalt::shareMailtoUrl(requestOf("tea & biscuits?", ""));
  // The one that actually bites: an unencoded `&` ends the body and starts a
  // header, so half the message silently disappears from the draft.
  EXPECT(url.find("%26") != std::string::npos);
  EXPECT(url.find("&biscuits") == std::string::npos);
  // And `?`, which would otherwise start the query a second time.
  EXPECT(url.find("%3F") != std::string::npos);
}

TEST(share_a_mailto_leaves_the_unreserved_characters_alone) {
  // Encoding everything would be correct and unreadable. RFC 3986's unreserved
  // set is what a URL may carry as-is.
  const std::string url = basalt::shareMailtoUrl(requestOf("abcXYZ019-_.~", ""));
  EXPECT(url.find("abcXYZ019-_.~") != std::string::npos);
}

TEST(share_a_mailto_without_a_title_has_no_subject) {
  const std::string url = basalt::shareMailtoUrl(requestOf("no title here", ""));
  EXPECT(url.find("subject=") == std::string::npos);
}
