/**
 * Where the files the build put beside this application are.
 *
 * `Foo.app/Contents/Resources` on macOS, and beside the executable on Linux and
 * Windows, which have no separate resources directory. A development run, where
 * the host starts out of a build directory, answers that directory.
 *
 * Here rather than worked out by each app: this project's packager is what
 * decides the layout, so an app deriving it would be an app that has to know
 * the shape of a bundle it did not build. See native/core/AppPaths.h.
 *
 * Empty when the running executable's path cannot be determined, which no
 * supported desktop does.
 *
 * @format
 */

import {TurboModuleRegistry} from 'react-native';
import type {TurboModule} from 'react-native';

type NativeAppModule = TurboModule & {
  getResourcePath(): string;
};

const NativeApp = TurboModuleRegistry.get<NativeAppModule>('BasaltApp');

let cached: string | null = null;

export function resourcePath(): string {
  // Cached because it cannot change while the process runs, and an app reading
  // it per file would otherwise cross the bridge each time.
  if (cached == null) {
    cached = NativeApp?.getResourcePath() ?? '';
  }
  return cached;
}
