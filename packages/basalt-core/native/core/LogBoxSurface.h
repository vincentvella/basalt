// React Native's error inspector, which is a second surface.
//
// The red box is not a native window that a host draws. It is React Native's
// own JavaScript -- `LogBoxInspectorContainer` -- registered by AppRegistry
// under the name `"LogBox"`, exactly as an app registers its own component. So
// a host does not implement an error overlay; it starts a second surface for a
// component it did not write, on top of the first.
//
// That is easy to miss, and missing it is why the inspector did not appear on
// this platform while the *toasts* did: the notification strip is rendered
// inside the app's own surface by `AppContainer`, so it has worked since
// `<View>` and `<Text>` did. Only the full-screen inspector needs a second
// surface, and the only thing that asks for one is the `LogBox` TurboModule --
// which ReactCxxPlatform already implements, and only provides when a host
// hands `ReactHost` a `SurfaceDelegate`. Passing null, as this project did,
// leaves `NativeLogBox.show()` a call into nothing.
//
// ## What is portable about a surface delegate
//
// Everything except making a root and starting a surface. The interface has six
// methods and four of them are bookkeeping; the two that are not are called
// from the JavaScript thread and have to reach the thread that owns widgets.
// Getting that wrong is a mounting-manager assertion on a good day. So the
// bookkeeping and the thread hop are here, once, and a host supplies two
// functions.

#pragma once

#include <react/renderer/scheduler/SurfaceDelegate.h>

#include <functional>
#include <string>

namespace basalt {

class LogBoxSurfaceDelegate final : public facebook::react::SurfaceDelegate {
 public:
  // `show` is handed the app key React Native registered the inspector under,
  // which is always "LogBox" but is passed rather than assumed -- it is the
  // module that decides, and a host should not have to know the string.
  using Show = std::function<void(const std::string &appKey)>;
  using Hide = std::function<void()>;

  LogBoxSurfaceDelegate(Show show, Hide hide);

  // --- SurfaceDelegate ------------------------------------------------------
  //
  // Called from the JavaScript thread: `createContentView` from the module's
  // constructor, `show` and `hide` from LogBoxData's setTimeout. So the two
  // that touch a surface hop to the UI thread and the rest only record.
  void createContentView(std::string appKey) override;
  bool isContentViewReady() override;
  void destroyContentView() override;
  void show() override;
  void hide() override;
  bool isShowing() override;

 private:
  Show show_;
  Hide hide_;
  std::string appKey_;
  // Written on the JavaScript thread and read there too -- every caller of this
  // interface is that thread -- so this needs no lock. The UI thread sees only
  // the two callbacks, and never this.
  bool showing_{false};
};

} // namespace basalt
