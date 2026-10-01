// What this package owes a platform, stubbed, for core/portability_probe.cpp.
//
// The probe links basalt_core, which carries this package's core half --
// SubprocessModule.cpp -- so the seam that file calls has to resolve there too.
// See basalt-notifications/native/probe_stub.cpp, which is here for
// the same reason and explains the contract.
//
// `subprocessSupported` answering false is also what keeps the module from being
// offered in a build with no platform half, rather than offering one whose every
// call fails.

#include "Subprocess.h"

namespace basalt {

bool subprocessSupported() {
  return false;
}

void setSubprocessListeners(SubprocessOutputListener, SubprocessExitListener) {}

int spawnSubprocess(const SpawnRequest &, std::string &error) {
  error = "no platform in this build";
  return 0;
}

bool killSubprocess(int) {
  return false;
}

bool isSubprocessRunning(int) {
  return false;
}

} // namespace basalt
