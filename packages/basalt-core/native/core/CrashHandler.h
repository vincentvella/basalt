// What the host says when it dies.
//
// Written because it had nothing to say. On 2026-10-05 a reload teardown took
// SIGSEGV on CI's Mac, twice, and three attempts produced no address, no stack
// and no thread -- only the harness noticing the process was gone and reporting
// "host exited -11". The bug was found by reading `ReactHost` instead, and the
// next one will not be as guessable.
//
// Deliberately not glog's `InstallFailureSignalHandler`. That would be one line,
// and its signal list is fixed and includes SIGTERM -- which is how the harness
// ends a host that is working. A stack trace and a changed exit status there
// would break the scenarios that read both. See docs/ci-performance.md on
// BASALT_TEST_QUIT_FILE for what SIGTERM is load-bearing for.
//
// ## What it prints
//
// To stderr, which is where the harness is already looking:
//
//     *** basalt: SIGSEGV at 0x0, thread 0x16dd57000 ***
//     0   basalt_appkit   0x0000000102a4c1b8 _ZN6basalt19AppKitMountingM...
//     1   basalt_appkit   0x0000000102a4b9e4 ...
//
// The names are mangled, and stay that way: demangling means
// `abi::__cxa_demangle`, which allocates, and a handler that needs the heap says
// nothing exactly when the heap is what broke. Pipe the log through `c++filt`.
//
// On Windows there are no symbols, for want of a .pdb in a release build, so it
// prints the module base and the return addresses: subtract the one from the
// others and the offsets resolve against the binary.
//
// ## What it may call
//
// Almost nothing. A signal handler may only use async-signal-safe functions, so
// there is no `LOG()`, no `std::string`, no formatting and no allocation here:
// glog takes a lock to write a line and a handler that waits for a lock held by
// the thread it interrupted never returns. `write`, `backtrace_symbols_fd`,
// `raise` and integer arithmetic are the whole vocabulary.
//
// `backtrace` is the exception: on glibc its first call dlopens libgcc, which
// allocates, so `installCrashHandler` calls it once while that is still safe.
//
// @format
#pragma once

namespace basalt {

// Installs handlers for the signals that mean the process is broken: SIGSEGV,
// SIGBUS, SIGILL, SIGFPE and SIGABRT. Not SIGTERM and not SIGINT, which mean
// somebody asked it to stop.
//
// Each handler prints, restores whatever was there before, and re-raises, so the
// exit status is still the signal and anything the runtime installed for its own
// purposes still runs. Call once, early, after logging is up.
void installCrashHandler();

} // namespace basalt
