// The six answers Skia asks this platform for. See BasaltSkPlatformContext.h.
//
// Each one is the kind of thing that compiles whatever it returns, so each is
// called rather than only linked: a font manager with no families, a surface
// that draws nothing and a stream operation whose callback never arrives are
// all indistinguishable from working, from the outside, until an app uses
// them.

#include "TestHarness.h"

#include "BasaltSkPlatformContext.h"
#include "JsRuntimeAccess.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#pragma clang diagnostic pop

#include <chrono>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

basalt::BasaltSkPlatformContext makeContext() {
  return basalt::BasaltSkPlatformContext(basalt::jsCallInvoker());
}

// What a stream operation answered, or nothing if it never answered. Bounded,
// because the failure this is most likely to catch is a callback that is
// never called -- and a test that hangs says that badly.
std::unique_ptr<SkStreamAsset> readUri(basalt::BasaltSkPlatformContext &context,
                                       const std::string &uri) {
  std::promise<std::unique_ptr<SkStreamAsset>> answered;
  std::future<std::unique_ptr<SkStreamAsset>> stream = answered.get_future();
  context.performStreamOperation(
      uri, [&answered](std::unique_ptr<SkStreamAsset> result) {
        answered.set_value(std::move(result));
      });
  if (stream.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
    return nullptr;
  }
  return stream.get();
}

} // namespace

TEST(skia_context_answers_with_the_system_font_manager) {
  basalt::BasaltSkPlatformContext context = makeContext();
  sk_sp<SkFontMgr> fonts = context.createFontMgr();
  EXPECT(fonts != nullptr);
  // A font manager that knows no families is what a wrongly linked Skia
  // gives: it constructs and answers nothing.
  EXPECT(fonts != nullptr && fonts->countFamilies() > 0);
}

TEST(skia_context_makes_a_surface_that_draws) {
  basalt::BasaltSkPlatformContext context = makeContext();
  sk_sp<SkSurface> surface = context.makeOffscreenSurface(16, 16);
  EXPECT(surface != nullptr);
  if (surface == nullptr) {
    return;
  }
  EXPECT_EQ(surface->width(), 16);

  SkPaint paint;
  paint.setColor(SK_ColorGREEN);
  surface->getCanvas()->clear(SK_ColorTRANSPARENT);
  surface->getCanvas()->drawRect(SkRect::MakeWH(16, 16), paint);

  SkPixmap pixels;
  EXPECT(surface->peekPixels(&pixels));
  if (pixels.addr() != nullptr) {
    EXPECT_EQ(static_cast<int>(SkColorGetG(pixels.getColor(8, 8))), 255);
  }

  // A size no surface can have, which Skia would otherwise answer with an
  // assertion in a debug build.
  EXPECT(context.makeOffscreenSurface(0, 0) == nullptr);
}

TEST(skia_context_reads_a_uri_the_way_an_image_source_is_read) {
  basalt::BasaltSkPlatformContext context = makeContext();
  // `core/ImageBytes.h`'s own vocabulary, which is the point of going through
  // it: whatever `<Image source>` accepts, a Skia `require()` accepts.
  std::unique_ptr<SkStreamAsset> stream =
      readUri(context, "data:text/plain;base64,aGVsbG8gc2tpYQ==");
  EXPECT(stream != nullptr);
  if (stream == nullptr) {
    return;
  }
  EXPECT_EQ(static_cast<int>(stream->getLength()), 10);
  std::string read(10, '\0');
  stream->read(read.data(), read.size());
  EXPECT_EQ(read, std::string("hello skia"));
}

TEST(skia_context_answers_a_uri_it_cannot_read_rather_than_going_quiet) {
  basalt::BasaltSkPlatformContext context = makeContext();
  // The failure that costs an app a hung promise rather than an error: Skia's
  // callers check for a null stream, and a callback that is never called is
  // indistinguishable from one that is still loading.
  std::unique_ptr<SkStreamAsset> stream =
      readUri(context, "file:///nowhere/at/all/basalt-skia.png");
  EXPECT(stream == nullptr);
}

TEST(skia_context_has_no_view_to_photograph_yet) {
  basalt::BasaltSkPlatformContext context = makeContext();
  // Null rather than an empty image, and a test rather than a comment,
  // because this is the one answer here that is a placeholder: it changes the
  // day a `<Canvas>` mounts.
  EXPECT(context.takeScreenshotFromViewTag(1) == nullptr);
}

TEST(skia_context_reports_an_error_without_unwinding_through_skia) {
  basalt::BasaltSkPlatformContext context = makeContext();
  // Skia's own comment says raising need not stop execution and that callers
  // must return afterwards, so throwing here would unwind through C++ that
  // does not expect it.
  bool threw = false;
  try {
    context.raiseError(std::runtime_error("a Skia error, reported"));
  } catch (...) {
    threw = true;
  }
  EXPECT(!threw);
}
