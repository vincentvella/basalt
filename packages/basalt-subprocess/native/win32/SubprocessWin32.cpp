// Spawning and watching a child on Windows.
//
// The same promises as the POSIX file next door, kept with different calls:
// `CreateProcessW` for the fork and exec, anonymous pipes for the output, a job
// object for "killing it kills what it started", and `WaitForSingleObject` plus
// `GetExitCodeProcess` in place of waitpid.
//
// **cmd.exe, and what that means for the command.** The command is given to
// `cmd.exe /d /s /c`, which is this desktop's shell the way `$SHELL -l -c` is
// other's. It is not the same language: `FOO=bar prog` sets nothing here and
// `exec` does not exist. That is why the environment is a parameter of
// SpawnRequest rather than part of the command string -- with it out of the
// string, `"node" "script.js" --port 4790` is a command both shells run.
//
// **One thread per child, as on POSIX, and for the same reason**: the exit must
// not overtake the output. The difference is that a Windows anonymous pipe has
// no `poll`. Two blocking reads on two threads would need a third to wait, and
// then the ordering problem returns. So the pipes are opened for overlapped I/O
// and the one thread waits on both reads and the process handle together, which
// is what `WaitForMultipleObjects` is for.
//
// **Not run on the machine that wrote it.** This file was written against the
// Win32 documentation and is compiled and exercised by this project's Windows CI
// -- which is the reason it lives in basalt rather than in an app, where nothing
// would ever have built it. See docs/TESTING.md.

#include "../Subprocess.h"

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

namespace basalt {

namespace {

struct Child {
  HANDLE process{nullptr};
  HANDLE job{nullptr};
};

struct Listeners {
  SubprocessOutputListener onOutput;
  SubprocessExitListener onExit;
};

std::mutex &lock() {
  static std::mutex mutex;
  return mutex;
}

Listeners &listeners() {
  static Listeners held;
  return held;
}

std::map<int, Child> &children() {
  static std::map<int, Child> running;
  return running;
}

void reportOutput(int pid, const std::string &data, bool isStandardError) {
  SubprocessOutputListener listener;
  {
    std::lock_guard<std::mutex> guard(lock());
    listener = listeners().onOutput;
  }
  if (listener) {
    listener(pid, data, isStandardError);
  }
}

void reportExit(int pid, int code) {
  SubprocessExitListener listener;
  Child child;
  {
    std::lock_guard<std::mutex> guard(lock());
    const auto found = children().find(pid);
    if (found != children().end()) {
      child = found->second;
      children().erase(found);
    }
    listener = listeners().onExit;
  }
  if (child.process != nullptr) {
    CloseHandle(child.process);
  }
  if (child.job != nullptr) {
    // Closing the job is what kills anything still in it, because of
    // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE below.
    CloseHandle(child.job);
  }
  if (listener) {
    listener(pid, code);
  }
}

std::wstring widen(const std::string &text) {
  if (text.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), length);
  return wide;
}

std::string narrow(const wchar_t *text, size_t length) {
  if (length == 0) {
    return {};
  }
  const int bytes = WideCharToMultiByte(
      CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) {
    return {};
  }
  std::string narrowed(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, text, static_cast<int>(length), narrowed.data(), bytes, nullptr, nullptr);
  return narrowed;
}

std::string lastErrorMessage(DWORD code) {
  LPWSTR buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      code,
      0,
      reinterpret_cast<LPWSTR>(&buffer),
      0,
      nullptr);
  if (length == 0 || buffer == nullptr) {
    return "error " + std::to_string(code);
  }
  std::string message = narrow(buffer, length);
  LocalFree(buffer);
  while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
    message.pop_back();
  }
  return message;
}

// This process's environment with the overrides applied, as CreateProcessW wants
// it: name=value runs separated by NUL, the block ended by a second NUL.
//
// Sorted case-insensitively, which the documentation asks for and which nothing
// enforces -- an unsorted block works until something reads it with a binary
// search.
std::wstring environmentBlockWith(const EnvironmentOverrides &overrides) {
  std::vector<std::wstring> entries;

  LPWCH existing = GetEnvironmentStringsW();
  if (existing != nullptr) {
    for (LPWCH entry = existing; *entry != L'\0';) {
      const std::wstring text = entry;
      entry += text.size() + 1;
      // A leading '=' is a drive's current directory (`=C:=C:\path`), which is
      // not a variable and must be kept as it is.
      const size_t equals = text.find(L'=', 1);
      const std::wstring name = equals == std::wstring::npos ? text : text.substr(0, equals);
      bool overridden = false;
      for (const auto &[key, value] : overrides) {
        if (_wcsicmp(widen(key).c_str(), name.c_str()) == 0) {
          overridden = true;
          break;
        }
      }
      if (!overridden) {
        entries.push_back(text);
      }
    }
    FreeEnvironmentStringsW(existing);
  }

  for (const auto &[key, value] : overrides) {
    entries.push_back(widen(key) + L"=" + widen(value));
  }

  std::sort(entries.begin(), entries.end(), [](const std::wstring &a, const std::wstring &b) {
    return _wcsicmp(a.c_str(), b.c_str()) < 0;
  });

  std::wstring block;
  for (const std::wstring &entry : entries) {
    block.append(entry);
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
}

// One overlapped read in flight per pipe, with its own event. `pending` is false
// between a completion and the next read being issued.
struct PipeReader {
  HANDLE pipe{nullptr};
  HANDLE event{nullptr};
  OVERLAPPED overlapped{};
  char buffer[4096]{};
  bool open{false};
  bool pending{false};
};

// Issues the next read, and answers false when the pipe has ended.
bool beginRead(PipeReader &reader) {
  if (!reader.open) {
    return false;
  }
  reader.overlapped = OVERLAPPED{};
  reader.overlapped.hEvent = reader.event;
  if (ReadFile(reader.pipe, reader.buffer, sizeof(reader.buffer), nullptr, &reader.overlapped)) {
    // Completed inline; the event is still signalled, so it is collected the
    // same way as one that did not.
    reader.pending = true;
    return true;
  }
  if (GetLastError() == ERROR_IO_PENDING) {
    reader.pending = true;
    return true;
  }
  // ERROR_BROKEN_PIPE is the child closing the stream, which is the ordinary
  // end; anything else is a pipe that cannot be read from again either way.
  reader.open = false;
  reader.pending = false;
  return false;
}

void closeReader(PipeReader &reader) {
  if (reader.pipe != nullptr) {
    CancelIo(reader.pipe);
    CloseHandle(reader.pipe);
    reader.pipe = nullptr;
  }
  if (reader.event != nullptr) {
    CloseHandle(reader.event);
    reader.event = nullptr;
  }
  reader.open = false;
}

void watch(int pid, HANDLE process, PipeReader out, PipeReader err) {
  PipeReader *readers[2] = {&out, &err};
  beginRead(out);
  beginRead(err);

  while (out.open || err.open) {
    HANDLE waits[2];
    int waited[2];
    DWORD count = 0;
    for (int i = 0; i < 2; i++) {
      if (readers[i]->open && readers[i]->pending) {
        waited[count] = i;
        waits[count] = readers[i]->event;
        count++;
      }
    }
    if (count == 0) {
      break;
    }

    const DWORD signalled = WaitForMultipleObjects(count, waits, FALSE, INFINITE);
    if (signalled < WAIT_OBJECT_0 || signalled >= WAIT_OBJECT_0 + count) {
      break;
    }
    PipeReader &reader = *readers[waited[signalled - WAIT_OBJECT_0]];

    DWORD read = 0;
    if (!GetOverlappedResult(reader.pipe, &reader.overlapped, &read, FALSE) || read == 0) {
      reader.open = false;
      reader.pending = false;
      continue;
    }
    reader.pending = false;
    reportOutput(pid, std::string(reader.buffer, read), &reader == &err);
    beginRead(reader);
  }

  closeReader(out);
  closeReader(err);

  // Only once both pipes have ended, so that every byte the child wrote is
  // reported before the exit; see the header.
  WaitForSingleObject(process, INFINITE);
  DWORD code = 0;
  if (!GetExitCodeProcess(process, &code)) {
    code = 0;
  }
  reportExit(pid, static_cast<int>(code));
}

} // namespace

bool subprocessSupported() {
  return true;
}

void setSubprocessListeners(SubprocessOutputListener onOutput, SubprocessExitListener onExit) {
  std::lock_guard<std::mutex> guard(lock());
  listeners().onOutput = std::move(onOutput);
  listeners().onExit = std::move(onExit);
}

int spawnSubprocess(const SpawnRequest &request, std::string &error) {
  // Named pipes rather than CreatePipe, because an anonymous pipe cannot be read
  // with overlapped I/O and overlapped is what lets one thread wait on both. The
  // name is unique per call; `\\.\pipe\` is the only namespace there is.
  static std::atomic<unsigned long> counter{0};
  const unsigned long serial = counter.fetch_add(1);

  PipeReader out;
  PipeReader err;
  HANDLE childOut = INVALID_HANDLE_VALUE;
  HANDLE childErr = INVALID_HANDLE_VALUE;

  SECURITY_ATTRIBUTES inheritable{};
  inheritable.nLength = sizeof(inheritable);
  inheritable.bInheritHandle = TRUE;

  const auto makePipe = [&](PipeReader &reader, HANDLE &childEnd, const wchar_t *which) -> bool {
    wchar_t name[128];
    swprintf(
        name,
        sizeof(name) / sizeof(name[0]),
        L"\\\\.\\pipe\\basalt-subprocess-%lu-%lu-%s",
        static_cast<unsigned long>(GetCurrentProcessId()),
        serial,
        which);
    reader.pipe = CreateNamedPipeW(
        name,
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1,
        4096,
        4096,
        0,
        nullptr);
    if (reader.pipe == INVALID_HANDLE_VALUE) {
      reader.pipe = nullptr;
      return false;
    }
    reader.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (reader.event == nullptr) {
      return false;
    }
    reader.open = true;
    // The child's end, which it inherits as its stdout or stderr.
    childEnd = CreateFileW(
        name,
        GENERIC_WRITE,
        0,
        &inheritable,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    return childEnd != INVALID_HANDLE_VALUE;
  };

  if (!makePipe(out, childOut, L"out") || !makePipe(err, childErr, L"err")) {
    error = "could not make a pipe for the child's output: " + lastErrorMessage(GetLastError());
    closeReader(out);
    closeReader(err);
    if (childOut != INVALID_HANDLE_VALUE) {
      CloseHandle(childOut);
    }
    if (childErr != INVALID_HANDLE_VALUE) {
      CloseHandle(childErr);
    }
    return 0;
  }

  // The job the child goes into, so that killing it kills what it started. The
  // limit is what makes closing the handle enough; see the header.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job != nullptr) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(
        job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = childOut;
  startup.hStdError = childErr;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  // `cmd.exe /d /s /c "<command>"`, this desktop's shell, with the two flags
  // and the outer quotes that make the quoting predictable.
  //
  // Left to itself, cmd.exe strips the first and last quote of what follows
  // /c when the line begins and ends with one -- and a command like
  //
  //     "C:/.../node.exe" "C:/.../daemon.cjs" --default-project "C:/.../hello"
  //
  // does begin and end with one. Removing those two leaves the quote after
  // node.exe and the quote before the project unbalanced, and cmd fails with
  // "The filename, directory name, or volume label syntax is incorrect."
  // before anything runs. Quoting the whole command gives that rule a pair of
  // quotes of its own to consume, so what survives it is the command as
  // written. /s makes the stripping unconditional rather than contingent on
  // how many quotes the command happens to contain, and /d skips the AutoRun
  // commands a machine may keep in its registry, which a child of this host
  // has no reason to inherit. It is the line Node's own child_process uses.
  std::wstring commandLine =
      L"cmd.exe /d /s /c \"" + widen(request.command) + L"\"";
  std::wstring environment = environmentBlockWith(request.env);
  const std::wstring cwd = widen(request.cwd);

  PROCESS_INFORMATION process{};
  const BOOL started = CreateProcessW(
      nullptr,
      commandLine.data(),
      nullptr,
      nullptr,
      TRUE,
      CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED,
      environment.data(),
      cwd.empty() ? nullptr : cwd.c_str(),
      &startup,
      &process);

  // The child's ends belong to the child now, and this process must not keep
  // them: a write end still open here means the read never sees end of file.
  CloseHandle(childOut);
  CloseHandle(childErr);

  if (started == FALSE) {
    error = "could not start cmd.exe: " + lastErrorMessage(GetLastError());
    closeReader(out);
    closeReader(err);
    if (job != nullptr) {
      CloseHandle(job);
    }
    return 0;
  }

  // Assigned while suspended, so that nothing it starts can escape the job by
  // being started before the assignment.
  if (job != nullptr) {
    AssignProcessToJobObject(job, process.hProcess);
  }
  ResumeThread(process.hThread);
  CloseHandle(process.hThread);

  const int pid = static_cast<int>(process.dwProcessId);
  {
    std::lock_guard<std::mutex> guard(lock());
    children()[pid] = Child{process.hProcess, job};
  }
  std::thread(watch, pid, process.hProcess, out, err).detach();
  return pid;
}

bool killSubprocess(int pid) {
  HANDLE job = nullptr;
  HANDLE process = nullptr;
  {
    std::lock_guard<std::mutex> guard(lock());
    const auto found = children().find(pid);
    if (found == children().end()) {
      return false;
    }
    job = found->second.job;
    process = found->second.process;
  }
  // The job, so that what the shell started goes too -- the same promise
  // killpg keeps on POSIX. Terminating the job rather than closing it, because
  // the handle is closed when the exit is reported and closing it here would
  // report the exit from inside a kill.
  if (job != nullptr && TerminateJobObject(job, 1) != 0) {
    return true;
  }
  return process != nullptr && TerminateProcess(process, 1) != 0;
}

bool isSubprocessRunning(int pid) {
  std::lock_guard<std::mutex> guard(lock());
  return children().find(pid) != children().end();
}

} // namespace basalt
