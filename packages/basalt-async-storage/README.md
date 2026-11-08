# basalt-async-storage

`@react-native-async-storage/async-storage` on Linux, macOS and Windows, for
[Basalt](https://basaltjs.dev) apps.

```bash
npm install @react-native-async-storage/async-storage basalt-async-storage
```

**This package has no JavaScript API.** You import async-storage exactly as you
do on a phone, and this supplies the native half it is missing on a desktop:

```js
import AsyncStorage from '@react-native-async-storage/async-storage';

await AsyncStorage.setItem('theme', 'dark');
const theme = await AsyncStorage.getItem('theme');
```

## What it stores, and where

One JSON file per database, under the application's own data directory:

| | |
| --- | --- |
| macOS | `~/Library/Application Support/<identifier>/async-storage` |
| Linux | `$XDG_DATA_HOME/<identifier>/async-storage`, else `~/.local/share/...` |
| Windows | `%LOCALAPPDATA%\<identifier>\async-storage` |

`<identifier>` is the reverse-DNS identifier the rest of the platform already
uses, the one in the `Info.plist`, the `.desktop` file and the
AppUserModelID. See
[`app.json`](https://basaltjs.dev/docs/reference/app-json#basaltidentifier).

A file rather than SQLite, which is what async-storage uses on Android. A few
megabytes of settings on a desktop is a file, and a file is something the
person whose computer it is can read, back up and delete. That is the wrong
answer for a large store, and the time to bring a database in is when
somebody's store is big enough to measure.

Writes go to a temporary beside the file and are renamed over it, so an
interrupted write leaves the previous store rather than half of this one.

## What porting this cost, since that was the open question

async-storage ships a TypeScript spec and a `codegenConfig`. React Native's
codegen turns that into `NativeAsyncStorageCxxSpec<T>`, a header-only base
class whose module name is already the one the JavaScript asks for, and the
module here derives from it. Basalt's build runs that codegen over the app's
own dependencies, so the same path is open to any module that ships a spec.

What had to be written by hand was the implementation and one bridging
specialisation, because async-storage's spec uses inline object types and
codegen generates no struct for those. Nothing was forked and nothing in
`node_modules` was patched.

The whole story, as a recipe for the next module:
[`docs/PORTING.md`](https://github.com/vincentvella/basalt/blob/main/docs/PORTING.md).

## Licence

MIT.
