#include "ShareFallback.h"

#include "TestDialog.h"

#include <array>
#include <string>

namespace basalt {

namespace {

// RFC 3986's unreserved set, plus nothing. A `mailto:` is built by
// concatenation, so anything else has to be encoded or it changes the meaning
// of the URL rather than appearing in it -- an `&` in a message being the case
// that actually bites, because it ends the body and starts a header.
bool isUnreserved(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
      c == '_' || c == '.' || c == '~';
}

std::string percentEncode(const std::string &text) {
  static constexpr std::array<char, 16> kHex{
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  std::string out;
  out.reserve(text.size());
  for (const char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    if (isUnreserved(c)) {
      out.push_back(raw);
      continue;
    }
    out.push_back('%');
    out.push_back(kHex[c >> 4U]);
    out.push_back(kHex[c & 0x0FU]);
  }
  return out;
}

// The two buttons and the way out, in the order the alert shows them. React
// Native's alerts put the cancelling one last on both desktops, so this does
// too.
constexpr int kCopyButton = 0;
constexpr int kEmailButton = 1;

} // namespace

std::string shareClipboardText(const ShareRequest &request) {
  // Both, when an app gave both: a link with a sentence explaining it is what
  // most calls to Share are, and dropping either would be dropping half of what
  // was asked for.
  if (request.message.empty()) {
    return request.url;
  }
  if (request.url.empty()) {
    return request.message;
  }
  return request.message + "\n" + request.url;
}

std::string shareMailtoUrl(const ShareRequest &request) {
  std::string url = "mailto:?body=" + percentEncode(shareClipboardText(request));
  if (!request.title.empty()) {
    url += "&subject=" + percentEncode(request.title);
  }
  return url;
}

void shareThroughFallbackPicker(const ShareRequest &request, ShareCallback onDone) {
  AlertRequest alert;
  // The app's own words for the picker when it gave any, which is what
  // `options.dialogTitle` is for.
  alert.title = request.dialogTitle.empty() ? "Share" : request.dialogTitle;
  // The content, so a person can see what they are about to send. A share sheet
  // shows a preview and this is the plainest form of one.
  alert.message = shareClipboardText(request);
  alert.buttons = {"Copy", "Email", "Cancel"};

  presentAlert(alert, [request, onDone = std::move(onDone)](int button, const std::string &) {
    switch (button) {
      case kCopyButton:
        setClipboardText(shareClipboardText(request));
        onDone(ShareOutcome::Shared, "");
        return;
      case kEmailButton:
        if (openUrl(shareMailtoUrl(request))) {
          onDone(ShareOutcome::Shared, "");
        } else {
          // No mail client, or nothing registered for `mailto:`. A failure
          // rather than a dismissal: the person chose to send it and it did not
          // go, which is not the same as changing their mind.
          onDone(ShareOutcome::Failed, "no application is registered for mailto:");
        }
        return;
      default:
        onDone(ShareOutcome::Dismissed, "");
        return;
    }
  });
}

} // namespace basalt
