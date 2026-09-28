// Spawning and watching a child on macOS and Linux.
//
// One file for both, because a subprocess is the one part of a desktop that is
// not a toolkit: fork, exec, pipe, poll and waitpid are the same call on each.
//
// **One thread per child, not three.** The obvious shape -- a thread reading
// stdout, another reading stderr, a third in waitpid -- gets the ordering wrong:
// the exit can overtake the last of the output, and a log then reads "exited
// with code 0" above the line saying why. `poll` on both pipes in one thread,
// and `waitpid` only once both have reached end of file, puts every byte the
// child wrote before the exit it reports.

#include "../Subprocess.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" char **environ;

namespace basalt {

namespace {

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

// The children this process started, and only those; see isSubprocessRunning.
std::map<int, bool> &children() {
  static std::map<int, bool> running;
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
  {
    std::lock_guard<std::mutex> guard(lock());
    children().erase(pid);
    listener = listeners().onExit;
  }
  if (listener) {
    listener(pid, code);
  }
}

// The shell that knows the user's PATH. `$SHELL` is the person's own, and `-l`
// makes it read the profile that puts asdf, nvm or Homebrew on the path -- which
// is the whole reason a shell is involved; see the header.
//
// Without `$SHELL` this falls back to `/bin/sh` and drops the `-l`: a login
// `/bin/sh` is dash on Debian and Ubuntu, whose `-l` is not the same flag, and
// failing on the flag would be a worse answer than running with the PATH that
// was inherited.
std::vector<std::string> shellCommand(const std::string &command) {
  const char *shell = std::getenv("SHELL");
  if (shell != nullptr && shell[0] != '\0') {
    return {shell, "-l", "-c", command};
  }
  return {"/bin/sh", "-c", command};
}

// This process's environment with the overrides applied, as execve wants it.
//
// Built in the parent, deliberately: `setenv` between fork and exec would be a
// call into the allocator, which is not async-signal-safe and is exactly the
// kind of thing that deadlocks a forked child of a threaded process.
std::vector<std::string> environmentWith(const EnvironmentOverrides &overrides) {
  std::vector<std::string> entries;
  for (char **entry = environ; entry != nullptr && *entry != nullptr; entry++) {
    const std::string text = *entry;
    const size_t equals = text.find('=');
    const std::string name = equals == std::string::npos ? text : text.substr(0, equals);
    bool overridden = false;
    for (const auto &[key, value] : overrides) {
      if (key == name) {
        overridden = true;
        break;
      }
    }
    if (!overridden) {
      entries.push_back(text);
    }
  }
  for (const auto &[key, value] : overrides) {
    entries.push_back(key + "=" + value);
  }
  return entries;
}

// Reads both pipes until each reaches end of file, then reaps. Detached: a
// daemon outliving the window that started it is its normal life.
void watch(int pid, int outFd, int errFd) {
  struct pollfd fds[2] = {{outFd, POLLIN, 0}, {errFd, POLLIN, 0}};
  bool open[2] = {true, true};
  char buffer[4096];

  while (open[0] || open[1]) {
    for (int i = 0; i < 2; i++) {
      fds[i].events = open[i] ? POLLIN : 0;
      fds[i].fd = open[i] ? fds[i].fd : -1;
    }
    if (::poll(fds, 2, -1) < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    for (int i = 0; i < 2; i++) {
      if (!open[i] || (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        continue;
      }
      const ssize_t read = ::read(fds[i].fd, buffer, sizeof(buffer));
      if (read > 0) {
        reportOutput(pid, std::string(buffer, static_cast<size_t>(read)), i == 1);
      } else if (read == 0 || (read < 0 && errno != EINTR && errno != EAGAIN)) {
        // End of file: the child closed the stream, almost always by exiting.
        open[i] = false;
      }
    }
  }

  ::close(outFd);
  ::close(errFd);

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    // Interrupted, not finished.
  }

  int code = 0;
  if (WIFEXITED(status)) {
    code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    // What a shell prints for a killed child, and what the header promises.
    code = 128 + WTERMSIG(status);
  }
  reportExit(pid, code);
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
  int outPipe[2] = {-1, -1};
  int errPipe[2] = {-1, -1};
  if (::pipe(outPipe) != 0 || ::pipe(errPipe) != 0) {
    error = std::string("could not make a pipe for the child's output: ") + std::strerror(errno);
    for (const int fd : {outPipe[0], outPipe[1], errPipe[0], errPipe[1]}) {
      if (fd >= 0) {
        ::close(fd);
      }
    }
    return 0;
  }

  const std::vector<std::string> parts = shellCommand(request.command);
  std::vector<char *> argv;
  argv.reserve(parts.size() + 1);
  for (const std::string &part : parts) {
    argv.push_back(const_cast<char *>(part.c_str()));
  }
  argv.push_back(nullptr);

  const std::vector<std::string> environment = environmentWith(request.env);
  std::vector<char *> envp;
  envp.reserve(environment.size() + 1);
  for (const std::string &entry : environment) {
    envp.push_back(const_cast<char *>(entry.c_str()));
  }
  envp.push_back(nullptr);

  // fork and exec rather than posix_spawn, for the working directory and the
  // process group. Both have spawn attributes for those, under names that differ
  // between the two desktops and that macOS 26 has already deprecated one of; a
  // child that does it itself between the fork and the exec needs no feature
  // test. Everything it does before exec is async-signal-safe -- setpgid, chdir,
  // dup2, close, execve -- which is the rule that makes forking a threaded
  // process sound.
  const pid_t pid = ::fork();
  if (pid < 0) {
    error = std::string("could not fork: ") + std::strerror(errno);
    for (const int fd : {outPipe[0], outPipe[1], errPipe[0], errPipe[1]}) {
      ::close(fd);
    }
    return 0;
  }

  if (pid == 0) {
    // Its own process group, so that killing it kills what it started; see the
    // header. Called in the child rather than the parent so that it has happened
    // before the exec, whichever runs first after the fork.
    ::setpgid(0, 0);

    ::close(outPipe[0]);
    ::close(errPipe[0]);
    if (::dup2(outPipe[1], STDOUT_FILENO) < 0 || ::dup2(errPipe[1], STDERR_FILENO) < 0) {
      ::_exit(127);
    }
    ::close(outPipe[1]);
    ::close(errPipe[1]);
    if (!request.cwd.empty() && ::chdir(request.cwd.c_str()) != 0) {
      ::_exit(127);
    }
    ::execve(argv[0], argv.data(), envp.data());
    // Only reachable when exec failed. 127 is what a shell reports for a command
    // it could not run, and it arrives through the exit listener like any other.
    ::_exit(127);
  }

  // And in the parent too, because which of the two runs first is not decided:
  // whichever loses finds the group already set, which is not an error worth
  // acting on.
  ::setpgid(pid, pid);

  ::close(outPipe[1]);
  ::close(errPipe[1]);

  {
    std::lock_guard<std::mutex> guard(lock());
    children()[static_cast<int>(pid)] = true;
  }
  std::thread(watch, static_cast<int>(pid), outPipe[0], errPipe[0]).detach();
  return static_cast<int>(pid);
}

bool killSubprocess(int pid) {
  {
    std::lock_guard<std::mutex> guard(lock());
    if (children().find(pid) == children().end()) {
      return false;
    }
  }
  // The group, not the process: the child is a shell, and what an app wants
  // stopped is what the shell started. SIGTERM rather than SIGKILL, so that a
  // server can close its listening socket -- one that is killed outright leaves
  // the port bound long enough for the next start to fail on it.
  if (::killpg(static_cast<pid_t>(pid), SIGTERM) == 0) {
    return true;
  }
  // A group that has already gone, or was never made because the exec beat the
  // setpgid. The process itself is still worth asking.
  return ::kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

bool isSubprocessRunning(int pid) {
  std::lock_guard<std::mutex> guard(lock());
  return children().find(pid) != children().end();
}

} // namespace basalt
