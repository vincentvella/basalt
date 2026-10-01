// react-native-reanimated, on top of core/WorkletsModule.h.
//
// The same arrangement again, and by now the pattern is the point: the package
// ships a large body of portable C++ -- the animation clock, the shadow-tree
// cloning that applies an animated style without a React re-render, the CSS
// engine, the registries -- and a small per-platform half that wires it to a
// host. This is that half.
//
// It is a thicker seam than worklets' because Reanimated reaches further into
// the renderer. Three things have to be found for it:
//
//   - The **UI worklet runtime** and its scheduler, which worklets installed on
//     the JavaScript global and which Reanimated reads back from there. That is
//     Reanimated's own arrangement, not something invented here.
//   - The **UIManager**, because Reanimated commits to the shadow tree itself.
//     ReactCxxPlatform hands one to the mounting manager, which is where this
//     gets it; see core/UIManagerAccess.h.
//   - A **frame**, on which its operations are flushed.
//
// What is deliberately not here is `PlatformDepMethodsHolder`'s long tail:
// sensors, keyboard events, screen snapshots and pseudo-selector observers are
// no-ops, because a desktop has no accelerometer and no soft keyboard, and the
// other two are iOS's answers to iOS problems. Each one is a field in a struct
// rather than a silent omission, which is the useful thing about that struct.

#pragma once

#include <ReactCommon/TurboModule.h>

#include <memory>

namespace basalt {

// Whether this build has react-native-reanimated compiled into it.
bool hasReanimated();

#ifdef BASALT_HAS_REANIMATED

class DesktopReanimatedModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "ReanimatedModule";

  explicit DesktopReanimatedModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~DesktopReanimatedModule() override;

 private:
  static facebook::jsi::Value installTurboModule(facebook::jsi::Runtime &runtime,
                                                 facebook::react::TurboModule &module,
                                                 const facebook::jsi::Value *args,
                                                 size_t count);
};

#endif

} // namespace basalt
