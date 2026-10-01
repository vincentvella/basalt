/**
 * Starting Metro, or noticing that it is already running.
 *
 * Deliberately not a Metro API call. `run-android` and `run-ios` both shell out
 * to `react-native start` in a detached process for the same reason: the
 * packager outlives the run command, and an app is normally launched several
 * times against one Metro.
 *
 * @format
 */

import type {ChildProcess} from 'node:child_process';
import {spawn} from 'node:child_process';
import * as fs from 'node:fs';
import * as net from 'node:net';
import * as path from 'node:path';

/**
 * What React Native's CLI hands a command. Only the two fields this file uses:
 * the rest of the context is large, changes between versions, and typing what
 * is not read would be claiming more than is known.
 */
export type MetroContext = {
  root: string;
  reactNativePath: string;
};

export type StartedMetro = {
  child: ChildProcess;
  logPath: string;
};

/**
 * Whether something is already listening.
 *
 * Only that something is listening, not that it is Metro: a port in use by
 * anything else is a problem the developer has to see, and starting a second
 * Metro on top of it would hide it.
 */
export function isPortTaken(port: number, host = 'localhost'): Promise<boolean> {
  return new Promise<boolean>(resolve => {
    const socket = net.connect({port, host});
    const done = (taken: boolean): void => {
      socket.destroy();
      resolve(taken);
    };
    socket.setTimeout(1500);
    socket.once('connect', () => done(true));
    socket.once('timeout', () => done(false));
    socket.once('error', () => done(false));
  });
}

export async function waitForPort(port: number, timeoutMs: number): Promise<boolean> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await isPortTaken(port)) {
      return true;
    }
    await new Promise(resolve => setTimeout(resolve, 500));
  }
  return false;
}

/**
 * Starts Metro and returns once it is listening.
 *
 * Detached and unref'd, so quitting the app does not kill the packager and the
 * next `run-linux` reuses it.
 *
 * Its output goes to a file rather than to this terminal, which looks like the
 * unfriendly choice and is not. A detached child that inherits stdout holds the
 * pipe open after this command exits, so anything reading `run-linux`'s output
 * waits forever for a packager that is meant to outlive it. This repository's
 * own CI hit the same shape and its workflow carries the same warning. The path
 * is printed, and `--no-packager` with a packager of your own is the answer if
 * you want it on screen.
 */
export async function startMetro(context: MetroContext, port: number): Promise<StartedMetro> {
  const cli = path.join(context.reactNativePath, 'cli.js');

  const logDir = path.join(context.root, '.basalt');
  fs.mkdirSync(logDir, {recursive: true});
  const logPath = path.join(logDir, 'metro.log');
  const log = fs.openSync(logPath, 'a');

  const child = spawn(process.execPath, [cli, 'start', '--port', String(port)], {
    cwd: context.root,
    detached: true,
    stdio: ['ignore', log, log],
  });
  child.unref();
  fs.closeSync(log);

  // A Metro that fails does so at once -- a config it cannot load, a package
  // it cannot resolve -- and waiting out the port timeout after it had already
  // exited turned a one-line error into ninety seconds of silence and "see the
  // log". So the wait races the child's exit, and an exit before the port
  // opens is reported with the end of the log, which is where Metro says what
  // went wrong.
  type Exit = {
    code?: number | null;
    signal?: NodeJS.Signals | null;
    error?: Error;
  };
  const exited = new Promise<Exit>(resolve => {
    child.once('exit', (code, signal) => resolve({code, signal}));
    child.once('error', (error: Error) => resolve({error}));
  });
  const outcome = await Promise.race<{listening?: boolean; exit?: Exit}>([
    waitForPort(port, 90000).then(listening => ({listening})),
    exited.then(exit => ({exit})),
  ]);

  if (outcome.exit) {
    const {code, signal, error} = outcome.exit;
    const how = error
      ? `could not be started (${error.message})`
      : `exited before listening (${signal ?? `code ${code}`})`;
    throw new Error(`Metro ${how}. The end of ${logPath}:\n\n${tail(logPath, 12)}`);
  }
  if (!outcome.listening) {
    throw new Error(`Metro did not start listening on port ${port}; see ${logPath}`);
  }
  return {child, logPath};
}

/**
 * The last lines of a file, without terminal colour codes, for an error
 * message. Empty when it cannot be read: the message is still worth showing.
 */
function tail(file: string, lines: number): string {
  try {
    return fs
      .readFileSync(file, 'utf8')
      .replace(/\x1b\[[0-9;]*[A-Za-z]/g, '')
      .trimEnd()
      .split(/\r?\n/)
      .slice(-lines)
      .join('\n');
  } catch {
    return '';
  }
}
