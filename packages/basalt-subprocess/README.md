# basalt-subprocess

Running a command and watching its output, for [Basalt](https://basaltjs.dev)
apps on Linux, macOS and Windows.

```bash
npm install basalt-subprocess
```

```js
import {spawn, kill, addOutputListener, addExitListener} from 'basalt-subprocess';

const pid = await spawn({
  command: '"node" "server.js" --port 4790',
  cwd: projectRoot,
  env: {FFMPEG: ffmpegPath},
});

const output = addOutputListener(e => console.log(e.pid, e.stream, e.data));
const exit = addExitListener(e => console.log(e.pid, 'exited', e.code));
```

| | |
| --- | --- |
| `spawn(options)` | starts the command, resolves to a pid |
| `kill(pid)` | `boolean` |
| `isRunning(pid)` | `boolean` |
| `isSupported()` | whether the native half is present |
| `addOutputListener(fn)` | `{pid, stream, data}` |
| `addExitListener(fn)` | `{pid, code}` |

[Full reference](https://basaltjs.dev/docs/reference/subprocess).

## Two things worth knowing

**The command goes to the platform's own shell.** That is what gets a
packaged application the `PATH` a person's login shell sets up, so `node`,
`ffmpeg` and anything installed by asdf, nvm or Homebrew are found. Quote your
arguments, as above.

**`spawn` rejects only when the process could not be started at all.** A
command that starts and then fails is not a rejection: it reports through the
exit listener, which is where an exit code belongs.

## Why it is a separate package

It is arbitrary code execution. A desktop front end for something else, a
video editor driving `ffmpeg` or an editor running a language server, needs
it, and an app that does not should not be shipping it. Installing it is the
decision.

Its C++ reaches the host build through
[`basalt.native`](https://basaltjs.dev/docs/reference/app-json#basaltnative-in-a-package),
the same seam any package can use, so nothing in Basalt keeps a list of
package names.

React Native has no API for this on any platform, which is why there is
nothing here to be compatible with.

## Licence

MIT.
