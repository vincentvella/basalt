// Which bytes of an image fetch are worth telling JavaScript about.
//
// `<Image onProgress>` is a prop an app writes to draw a bar, and the bytes
// behind it arrive on libcurl's clock: `CURLOPT_XFERINFOFUNCTION` is called
// roughly every time a socket read returns, which for a megabyte over a fast
// link is thousands of times. Every one of those would be a Fabric event, a
// JavaScript call and a React render, for a bar whose width is measured in
// pixels and cannot show the difference.
//
// So the ticks are gated here, on whole percentages: a hundred events over a
// download, which is more than a bar can resolve and few enough to be free.
// The gate is in core rather than in each host's mounting manager because all
// three mean the same thing by it, and because this way it is a thing a test
// can say something about -- the hosts' half is a lookup and a lambda, and the
// arithmetic is here.
//
// Two cases that are not a percentage, and both are real:
//
// **No `Content-Length`.** A chunked response has no total, which is not an
// error: libcurl reports a growing `loaded` against a zero `total` and the
// event carries both, so an app can show bytes where it cannot show a
// fraction. There is nothing to throttle against, so every tick is reported.
// Turning it into a fraction here -- against the bytes so far, say -- would be
// inventing a denominator and would read as a bar that is always nearly full.
//
// **libcurl's opening tick.** The callback fires once before the response
// headers have been read, with nothing loaded and nothing known. That one says
// neither how much has arrived nor how much will, so it is not an event; an app
// that sees it would show a zero-of-zero bar before the request had even been
// answered.

#pragma once

#include <optional>

namespace basalt {

// One fetch's worth of gating. Belongs to the load it was made for, and is
// called only from the thread doing that fetch -- which is why it needs no
// lock: libcurl calls its progress function from the thread inside
// `curl_easy_perform`, and that is one thread per handle.
class ImageProgressTicker {
 public:
  // The fraction to report, or nothing when this tick is not worth an event.
  //
  // `loaded` and `total` are bytes, as libcurl counts them. A `total` of zero
  // or less means the server did not say; see the note above.
  std::optional<double> tick(long long loaded, long long total) {
    if (loaded <= 0 && total <= 0) {
      // Nothing arrived and nothing is known. See the note above.
      return std::nullopt;
    }
    if (total <= 0) {
      // Indeterminate: the event still carries `loaded` and a zero `total`,
      // and the fraction is the only thing that cannot be answered.
      return 0.0;
    }
    if (loaded < 0) {
      loaded = 0;
    }
    if (loaded > total) {
      // A server that sent more than it promised. Clamped rather than passed
      // on, so an app drawing `progress * width` cannot overrun its bar.
      loaded = total;
    }

    const int percent = static_cast<int>(loaded * 100 / total);
    if (percent == reported_) {
      return std::nullopt;
    }
    reported_ = percent;
    return static_cast<double>(loaded) / static_cast<double>(total);
  }

  // Which whole percentage was last reported, or -1 before the first tick.
  // For the tests; the hosts have no use for it.
  int reportedPercent() const {
    return reported_;
  }

 private:
  int reported_{-1};
};

} // namespace basalt
