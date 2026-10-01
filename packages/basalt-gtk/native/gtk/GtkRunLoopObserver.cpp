#include "GtkRunLoopObserver.h"

#include <react/utils/RunLoopObserverManager.h>

namespace basalt {

namespace {

struct BeatSource {
  GSource source;
  // A bare pointer into a shared_ptr held below. GSource is a C struct with no
  // destructor of its own, so the owning reference lives in a member that
  // finalize() destroys explicitly.
  std::shared_ptr<facebook::react::RunLoopObserverManager> *manager;
};

// Called once per main-loop iteration, before the loop polls. Returning FALSE
// with no timeout means "I am never ready", so this source never causes a
// wake-up and never spins: it only rides along on iterations that something
// else caused.
gboolean beatPrepare(GSource *source, gint *timeout) {
  auto *self = reinterpret_cast<BeatSource *>(source);
  if (self->manager != nullptr && *self->manager != nullptr) {
    (*self->manager)->onRender();
  }
  *timeout = -1;
  return FALSE;
}

gboolean beatCheck(GSource * /*source*/) {
  return FALSE;
}

gboolean beatDispatch(GSource * /*source*/, GSourceFunc /*callback*/, gpointer /*userData*/) {
  return G_SOURCE_CONTINUE;
}

void beatFinalize(GSource *source) {
  auto *self = reinterpret_cast<BeatSource *>(source);
  delete self->manager;
  self->manager = nullptr;
}

GSourceFuncs beatSourceFuncs = {
    .prepare = beatPrepare,
    .check = beatCheck,
    .dispatch = beatDispatch,
    .finalize = beatFinalize,
    .closure_callback = nullptr,
    .closure_marshal = nullptr,
};

} // namespace

GSource *installRunLoopObserver(std::shared_ptr<facebook::react::RunLoopObserverManager> manager) {
  GSource *source = g_source_new(&beatSourceFuncs, sizeof(BeatSource));
  auto *self = reinterpret_cast<BeatSource *>(source);
  self->manager = new std::shared_ptr<facebook::react::RunLoopObserverManager>(std::move(manager));

  // Below the default, so this runs after ordinary work has been queued rather
  // than ahead of it. prepare() is called regardless of priority, but keeping
  // it low makes the intent clear.
  g_source_set_priority(source, G_PRIORITY_DEFAULT_IDLE);
  g_source_set_name(source, "react-native event beat");
  g_source_attach(source, nullptr);
  return source;
}

void removeRunLoopObserver(GSource *source) {
  if (source == nullptr) {
    return;
  }
  g_source_destroy(source);
  g_source_unref(source);
}

} // namespace basalt
