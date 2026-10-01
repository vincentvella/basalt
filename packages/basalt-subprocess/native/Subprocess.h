// Running a command, and watching it.
//
// A desktop application that is a front end for something else -- a video
// editor driving ffmpeg, an editor running a language server, anything with a
// daemon -- needs this, and React Native has no API for it because a phone has
// no such thing. So there is nothing to port and the shape is this project's to
// choose.
//
// **A package rather than core**, for the reason docs/DECISIONS.md gives for
// notifications and then some: this is arbitrary code execution. An app that
// does not spawn processes should not have the ability compiled into its
// binary, and `capabilityPackages` exists so that it is not.
//
// --- The shape, and why -------------------------------------------------------
//
// **A shell command, not an argv.** The command is given to the platform's own
// shell -- `$SHELL -l -c` where there is one, `cmd.exe /c` on Windows. That
// costs the safety an argv would give and buys the thing an app actually needs:
// the PATH a person's login shell sets up. A packaged application launched from
// the Finder or the Start menu inherits almost none of it, and `node`, `ffmpeg`
// and anything installed by asdf, nvm or Homebrew are exactly what an app of
// this kind wants to run. An argv form can be added beside this; it cannot
// replace it.
//
// **The environment is a parameter, not part of the string.** `FOO=bar cmd` is
// POSIX shell syntax that cmd.exe does not have, so a command carrying its own
// variables is a command that only runs on two of the three desktops. Passing
// them separately is what makes one command string work everywhere, and it is
// the reason this seam has an `env` at all.
//
// **A child is its own process group, and killing kills the group.** Otherwise
// `kill` reaches the shell and leaves the program it started running -- which is
// the bug `exec` in a command string exists to work around, and which an app
// should not have to know about. POSIX puts the child in a new process group and
// signals the group; Windows puts it in a job object and terminates the job.
// Same promise on both: nothing the command started outlives it.
//
// Nothing here is JavaScript-aware. SubprocessModule.cpp is the boundary, and
// keeps unistd.h and windows.h out of the file with jsi in it.

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace basalt {

// Variables to add to the ones this process already has. A value replaces an
// existing variable of the same name; nothing is removed.
using EnvironmentOverrides = std::vector<std::pair<std::string, std::string>>;

struct SpawnRequest {
  // A command for this desktop's shell.
  std::string command;
  // Where to run it. Empty means this process's own directory.
  std::string cwd;
  EnvironmentOverrides env;
};

// A chunk of a child's output, as it was read -- not split into lines, because
// where the lines are is the reader's business and a chunk boundary is not one.
using SubprocessOutputListener =
    std::function<void(int pid, const std::string &data, bool isStandardError)>;

// A child that has finished, with the status a shell would report: the exit
// code, or 128 plus the signal for one that was killed.
using SubprocessExitListener = std::function<void(int pid, int code)>;

// Set once, before anything is spawned. Called from the thread watching a child
// rather than from the JavaScript thread.
void setSubprocessListeners(SubprocessOutputListener onOutput, SubprocessExitListener onExit);

// Starts `request`, and answers the child's process id.
//
// Zero and a filled `error` when it could not be started at all. A command that
// starts and then fails is not this function's failure: it reports through the
// exit listener, which is what a shell does too.
int spawnSubprocess(const SpawnRequest &request, std::string &error);

// Asks a child this process started to stop, and everything it started with it.
// The polite signal, not the fatal one, so that a server can close its sockets.
// False for a process this one did not start, or one already gone.
bool killSubprocess(int pid);

// Whether a child this process started is still running. False for a pid this
// process never started: a recycled pid would otherwise answer yes about a
// stranger.
bool isSubprocessRunning(int pid);

// Whether this build can spawn at all. False only in the portability probe,
// which links no platform; every desktop this project supports implements it.
bool subprocessSupported();

} // namespace basalt
