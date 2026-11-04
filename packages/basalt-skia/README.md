# basalt-skia

React Native Skia on the desktop, for [Basalt](https://basaltjs.dev) apps.

```bash
npm install basalt-skia
```

That is the whole setup. There is no JavaScript API here and nothing to
import: you write react-native-skia, and this package supplies the platform
half that react-native-skia leaves for a platform to write. The C++ above it
is react-native-skia's own, unpatched.

```jsx
import {Canvas, Path, Skia} from '@shopify/react-native-skia';
```

Both halves of the API work: the imperative one (`Skia.Path`, image decoding,
typefaces) and `<Canvas>`.

## Which line of Skia

Whichever one your app installed.

| | |
| --- | --- |
| `@shopify/react-native-skia` **2.x**, Ganesh | Linux, macOS and Windows. Metal on macOS, a CPU surface on the other two |
| `react-native-skia` **3.x**, Graphite over Dawn | macOS and Windows. Linux needs a prebuilt Graphite Skia, which nothing builds yet |

On Linux and Windows, 2.x builds Skia from source through
`scripts/build_skia_linux.sh` and its Windows counterpart.

[Where each library stands](https://basaltjs.dev/docs/libraries).

## What it is not

Not a fork and not a patch. react-native-skia already separates the part that
is Skia from the part that is a platform, and this is the second part for
three desktops. Nothing in `node_modules` is rewritten.

Its C++ reaches the host build through
[`basalt.native`](https://basaltjs.dev/docs/reference/app-json#basaltnative-in-a-package),
the same seam any package can use.

## Licence

MIT.
