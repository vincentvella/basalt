/**
 * Running a command, and watching it.
 *
 * A desktop front end for something else -- a video editor driving ffmpeg, an
 * editor running a language server, anything with a daemon -- needs this, and
 * React Native has no API for it because a phone has no such thing. So this
 * shape is react-native-basalt's own rather than a port of something.
 *
 * The command is given to the platform's own shell, which is what gets a
 * packaged application the PATH a person's login shell sets up: `node`, `ffmpeg`
 * and anything installed by asdf, nvm or Homebrew. See native/Subprocess.h for
 * the rest of the reasoning, including why the environment is a parameter rather
 * than part of the command.
 *
 *     const pid = await spawn({
 *       command: '"node" "server.js" --port 4790',
 *       cwd: projectRoot,
 *       env: {FFMPEG: ffmpegPath},
 *     });
 *     const output = addOutputListener(e => console.log(e.pid, e.stream, e.data));
 *     const exit = addExitListener(e => console.log(e.pid, 'exited', e.code));
 *
 * @format
 */

import {DeviceEventEmitter, TurboModuleRegistry} from 'react-native';
import type {EmitterSubscription, TurboModule} from 'react-native';

export interface SpawnOptions {
  /** A command for this desktop's shell. */
  command: string;
  /** Where to run it. Omitted means the application's own directory. */
  cwd?: string;
  /**
   * Variables to add to the ones the application already has. A value replaces
   * one of the same name; nothing is removed.
   *
   * Here rather than in `command` because `FOO=bar cmd` is POSIX shell syntax
   * that cmd.exe does not have -- a command carrying its own variables is one
   * that runs on two of the three desktops.
   */
  env?: Record<string, string>;
}

export interface OutputEvent {
  pid: number;
  /** A chunk as it was read, not a line: where the lines are is yours to decide. */
  data: string;
  stream: 'stdout' | 'stderr';
}

export interface ExitEvent {
  pid: number;
  /** The code it chose, or 128 plus the signal for one that was killed. */
  code: number;
}

type SubprocessModule = TurboModule & {
  spawn(options: SpawnOptions): Promise<number>;
  kill(pid: number): boolean;
  isRunning(pid: number): boolean;
};

// `get` rather than `getEnforcing`: this is a capability package, so the module
// is absent in an application that did not install it, and "is it there" is a
// question worth being able to ask rather than a crash at import.
const native = TurboModuleRegistry.get<SubprocessModule>('BasaltSubprocess');

const OUTPUT_EVENT = 'basaltSubprocessOutput';
const EXIT_EVENT = 'basaltSubprocessExit';

/** Whether this build can run commands at all. */
export function isSupported(): boolean {
  return native != null;
}

function required(): SubprocessModule {
  if (native == null) {
    throw new Error(
      'react-native-basalt-subprocess: the native module is missing. It is a ' +
        'capability package, so it has to be a dependency of the app being ' +
        'built -- see its README -- and the host rebuilt afterwards.',
    );
  }
  return native;
}

/**
 * Starts `options.command` and answers the process id.
 *
 * Rejects when it could not be started at all. A command that starts and then
 * fails is not a rejection: it reports through the exit listener, which is what
 * a shell does too.
 */
export function spawn(options: SpawnOptions): Promise<number> {
  return required().spawn(options);
}

/**
 * Asks a process this application started to stop, and everything it started
 * with it -- the whole process group, or the whole job on Windows. The polite
 * signal, not the fatal one, so a server can close its sockets.
 *
 * False for a process this application did not start, or one already gone.
 */
export function kill(pid: number): boolean {
  return required().kill(pid);
}

/**
 * Whether a process this application started is still running. False for one it
 * did not start: a recycled id would otherwise answer yes about a stranger.
 */
export function isRunning(pid: number): boolean {
  return required().isRunning(pid);
}

export function addOutputListener(
  listener: (event: OutputEvent) => void,
): EmitterSubscription {
  return DeviceEventEmitter.addListener(OUTPUT_EVENT, listener);
}

export function addExitListener(listener: (event: ExitEvent) => void): EmitterSubscription {
  return DeviceEventEmitter.addListener(EXIT_EVENT, listener);
}
