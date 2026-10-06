#include "CrashHandler.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <execinfo.h>
#include <pthread.h>
#include <unistd.h>
#endif

namespace basalt {

namespace {

#if !defined(_WIN32)

constexpr int kFatalSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
constexpr std::size_t kFatalSignalCount = sizeof(kFatalSignals) / sizeof(kFatalSignals[0]);

// What was installed before, so each handler can put it back and re-raise. An
// empty slot is one this never touched.
struct sigaction gPrevious[kFatalSignalCount];

int slotFor(int signalNumber) {
  for (std::size_t i = 0; i < kFatalSignalCount; i++) {
    if (kFatalSignals[i] == signalNumber) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Everything below runs in a signal handler, so everything below is
// async-signal-safe. No allocation, no locks, no formatting.

void writeText(const char *text) {
  if (text == nullptr) {
    return;
  }
  std::size_t length = 0;
  while (text[length] != '\0') {
    length++;
  }
  // The result is deliberately ignored: there is nothing useful to do about a
  // failed write from here, and a partial one still says more than nothing.
  const ssize_t written = ::write(STDERR_FILENO, text, length);
  (void)written;
}

void writeHex(std::uintptr_t value) {
  char digits[] = "0123456789abcdef";
  // 16 nibbles, a "0x", and the terminator.
  char buffer[19];
  buffer[0] = '0';
  buffer[1] = 'x';
  std::size_t at = 2;
  bool leading = true;
  for (int shift = 60; shift >= 0; shift -= 4) {
    const unsigned nibble = static_cast<unsigned>((value >> shift) & 0xF);
    if (nibble == 0 && leading && shift != 0) {
      continue;
    }
    leading = false;
    buffer[at++] = digits[nibble];
  }
  buffer[at] = '\0';
  writeText(buffer);
}

const char *nameFor(int signalNumber) {
  switch (signalNumber) {
    case SIGSEGV:
      return "SIGSEGV";
    case SIGBUS:
      return "SIGBUS";
    case SIGILL:
      return "SIGILL";
    case SIGFPE:
      return "SIGFPE";
    case SIGABRT:
      return "SIGABRT";
    default:
      return "signal";
  }
}

void onFatalSignal(int signalNumber, siginfo_t *info, void * /*context*/) {
  writeText("\n*** basalt: ");
  writeText(nameFor(signalNumber));
  if (info != nullptr) {
    writeText(" at ");
    writeHex(reinterpret_cast<std::uintptr_t>(info->si_addr));
  }
  writeText(", thread ");
  // The id rather than the name: `pthread_getname_np` takes no lock on either
  // platform but is not promised to be safe here, and the id is what the glog
  // lines around this already print.
  writeHex(static_cast<std::uintptr_t>(
      reinterpret_cast<std::uintptr_t>(pthread_self())));
  writeText(" ***\n");

  // 64 frames: deep enough for a mounting walk inside a dispatch block inside a
  // run loop, which is the stack this was written for.
  void *frames[64];
  const int count = ::backtrace(frames, 64);
  // Writes straight to the descriptor and allocates nothing, unlike
  // `backtrace_symbols`, which is why it is this one.
  ::backtrace_symbols_fd(frames, count, STDERR_FILENO);
  writeText("\n");

  // Back to whatever was there, then re-raise. The exit status stays the signal,
  // which is what the harness reports, and a handler the runtime installed for
  // its own purposes still gets its turn.
  const int slot = slotFor(signalNumber);
  if (slot >= 0) {
    ::sigaction(signalNumber, &gPrevious[slot], nullptr);
  } else {
    ::signal(signalNumber, SIG_DFL);
  }
  ::raise(signalNumber);
}

#else

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS *info) {
  // Not a signal handler, so this is allowed more than its POSIX counterpart.
  // It stays just as narrow anyway: the process is already broken, and a handler
  // that needs the heap to say so is a handler that says nothing when the heap
  // is what broke.
  char line[128];
  const DWORD code =
      info != nullptr && info->ExceptionRecord != nullptr
      ? info->ExceptionRecord->ExceptionCode
      : 0;
  const void *at = info != nullptr && info->ExceptionRecord != nullptr
      ? info->ExceptionRecord->ExceptionAddress
      : nullptr;

  int length = wsprintfA(line,
                         "\n*** basalt: exception 0x%08lx at %p, thread %lu ***\n",
                         static_cast<unsigned long>(code),
                         at,
                         GetCurrentThreadId());
  DWORD written = 0;
  WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, static_cast<DWORD>(length), &written, nullptr);

  // No symbols: a release build ships no .pdb, so dbghelp would resolve nothing
  // and linking it would buy an address it already has. The module base is here
  // so the frames below can be turned into offsets against the binary.
  length = wsprintfA(line, "module %p\n", static_cast<void *>(GetModuleHandleW(nullptr)));
  WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, static_cast<DWORD>(length), &written, nullptr);

  void *frames[64];
  const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
  for (USHORT i = 0; i < count; i++) {
    length = wsprintfA(line, "%2u  %p\n", static_cast<unsigned>(i), frames[i]);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, static_cast<DWORD>(length), &written, nullptr);
  }

  // On to whatever Windows would have done, so the exit status and any crash
  // reporting behave as before.
  return EXCEPTION_CONTINUE_SEARCH;
}

#endif

} // namespace

namespace {

// BASALT_TEST_CRASH, which exists so that the handler can be tested at all.
//
// A handler is only ever exercised on the worst day, so one that was never run
// is a guess. This makes the worst day reproducible: the scenario in
// scripts/integration_test.py sets it and asserts the marker, a frame list and an
// exit status that is still the signal.
//
// `raise` rather than writing through a null pointer: a deliberate null
// dereference is undefined behaviour and a compiler is within its rights to
// delete it, which would make the test pass by not crashing.
void crashIfAsked() {
  const char *value = std::getenv("BASALT_TEST_CRASH");
  if (value == nullptr || *value == '\0') {
    return;
  }
#if defined(_WIN32)
  // An access violation, since Windows has no raise(SIGSEGV) worth the name: the
  // unhandled-exception filter is what is being tested, and only a real exception
  // reaches it.
  RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#else
  ::raise(SIGSEGV);
#endif
}

} // namespace

void installCrashHandler() {
#if defined(_WIN32)
  SetUnhandledExceptionFilter(onUnhandledException);
  crashIfAsked();
#else
  // Once, while allocating is still allowed: glibc's first `backtrace` dlopens
  // libgcc, and a handler is not the place to find that out.
  void *warm[1];
  (void)::backtrace(warm, 1);

  struct sigaction action = {};
  action.sa_sigaction = onFatalSignal;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&action.sa_mask);

  for (std::size_t i = 0; i < kFatalSignalCount; i++) {
    ::sigaction(kFatalSignals[i], &action, &gPrevious[i]);
  }
  crashIfAsked();
#endif
}

} // namespace basalt
