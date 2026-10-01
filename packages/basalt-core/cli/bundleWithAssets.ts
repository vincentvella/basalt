/**
 * Bundling for linux, assets included.
 *
 * React Native's own CLI does this and takes `--assets-dest`, so a React Native
 * app is served by `run-linux`. An Expo app is not: its CLI is `expo`, which
 * does not know this platform, and Metro's own `build` command has no asset
 * option at all. Without one, `require('./logo.png')` resolves to a path beside
 * the bundle that nothing ever wrote.
 *
 * So this is the same two steps React Native's CLI performs internally: build
 * the bundle, then ask Metro which assets the build referenced and copy each
 * one where resolution will look for it.
 *
 * @format
 */

import * as fs from 'node:fs';
import * as path from 'node:path';
import {createRequire} from 'node:module';

/**
 * One asset, as Metro's `Server.getAssets` reports it. `scales` and `files` are
 * parallel: one file per scale.
 */
export type MetroAsset = {
  name: string;
  type: string;
  httpServerLocation: string;
  scales: number[];
  files: string[];
};

export type BundleOptions = {
  projectRoot: string;
  entryFile: string;
  bundleOutput: string;
  assetsDest?: string;
  platform?: string;
  dev?: boolean;
  minify?: boolean;
  configPath?: string;
};

export type BundleResult = {
  assets: number;
  files: number;
  assetsDest: string;
  expoConfig: boolean;
};

/**
 * Metro, as far as this file calls it.
 *
 * Metro ships no types for `metro/private/*`, which is how its own
 * package.json exposes the internals React Native's CLI uses. Describing the
 * three calls made here is narrower, and more honest, than pretending to know
 * the rest.
 */
type MetroModule = {
  loadConfig(options: {cwd: string; config?: string}): Promise<unknown>;
  runBuild(
    config: unknown,
    options: {entry: string; out: string; platform: string; dev: boolean; minify: boolean},
  ): Promise<unknown>;
};

type MetroServer = {
  getAssets(options: Record<string, unknown>): Promise<MetroAsset[]>;
  end(): void;
};

type MetroServerClass = {
  new (config: unknown): MetroServer;
  DEFAULT_BUNDLE_OPTIONS: Record<string, unknown>;
};

/**
 * Metro, and the pieces of it that have no public export.
 *
 * `metro/private/*` is how Metro's own package.json exposes its internals, and
 * `Server.getAssets` is what React Native's CLI uses for exactly this. Resolved
 * from the project rather than from here, so an app gets the Metro it installed
 * and not a second copy.
 */
function loadMetro(projectRoot: string): {metro: MetroModule; Server: MetroServerClass} {
  const resolvers = [createRequire(path.join(projectRoot, 'package.json'))];

  // Then from React Native's own location. An installed app has Metro hoisted
  // into its node_modules and the first resolver finds it; a checkout, a
  // workspace or a linked package may not, and Metro is always beside React
  // Native. Looked up second rather than first so an app that pins its own
  // Metro gets the one it pinned.
  try {
    resolvers.push(createRequire(resolvers[0].resolve('react-native/package.json')));
  } catch {
    // No react-native from the project. The error below will be the useful one.
  }

  for (const resolve of resolvers) {
    try {
      const metro = resolve('metro');
      const serverModule = resolve('metro/private/Server');
      return {metro, Server: serverModule.default ?? serverModule};
    } catch {
      // Try the next one.
    }
  }

  throw new Error(
    `could not find Metro from ${projectRoot} or from its react-native. ` +
      'Bundling needs it; it is normally installed with react-native.',
  );
}

/**
 * Where one asset file belongs, relative to the assets directory.
 *
 * This has to agree exactly with what React Native computes at runtime in
 * `AssetSourceResolver.scaledAssetPath`, or assets land somewhere resolution
 * does not look and the failure is a missing file with no explanation. The
 * `../` replacement is React Native's: an asset can sit outside the project
 * root, and its path must not escape the assets directory.
 */
export function destinationFor(asset: MetroAsset, scale: number): string {
  const suffix = scale === 1 ? '' : `@${scale}x`;
  const name = `${asset.name}${suffix}.${asset.type}`;
  return path.join(asset.httpServerLocation.replace(/^\//, '').replace(/\.\.\//g, '_'), name);
}

async function copyAssets(assets: MetroAsset[], assetsDest: string): Promise<number> {
  let copied = 0;
  for (const asset of assets) {
    // `scales` and `files` are parallel arrays: one file per scale.
    for (let index = 0; index < asset.scales.length; index++) {
      const source = asset.files[index];
      if (source == null) {
        continue;
      }
      const destination = path.join(assetsDest, destinationFor(asset, asset.scales[index]));
      await fs.promises.mkdir(path.dirname(destination), {recursive: true});
      await fs.promises.copyFile(source, destination);
      copied++;
    }
  }
  return copied;
}

/**
 * Writes the app's resolved Expo config beside the bundle, as `app.config.json`.
 *
 * This is what `Constants.expoConfig` is, and expo-linking reads it to find the
 * app's URI scheme. iOS and Android embed it into the app during the native
 * build, from a script expo-constants contributes; this platform's hosts are
 * generic binaries with no build step of their own, so bundling is the moment
 * that has both the project and a place to put the answer.
 *
 * `isPublicConfig` is Expo's own flag for "this will be readable by anything
 * that can read the app", and drops the keys that are not meant to be --
 * `hooks`, and EAS credentials under `extra`. Embedding the private config
 * would be a change in what an app ships, made silently by a bundler.
 *
 * Returns false when the project is not an Expo app, which is not a failure:
 * `expo/config` is simply not there to resolve.
 */
export async function writeExpoAppConfig(
  projectRoot: string,
  bundleOutput: string,
): Promise<boolean> {
  // Expo's own options, as expo/config declares them; `skipSDKVersionRequirement`
  // is the one that lets this read a config for an SDK this tool does not pin.
  type GetConfig = (
    root: string,
    options: {isPublicConfig?: boolean; skipSDKVersionRequirement?: boolean},
  ) => {exp: unknown};

  let getConfig: GetConfig;
  try {
    getConfig = createRequire(path.join(projectRoot, 'package.json'))('expo/config')
      .getConfig as GetConfig;
  } catch {
    return false;
  }

  // Past this point the project *is* an Expo app, so a failure is a real one
  // and silence would leave Constants.expoConfig mysteriously null.
  const {exp} = getConfig(projectRoot, {
    skipSDKVersionRequirement: true,
    isPublicConfig: true,
  });
  const destination = path.join(path.dirname(bundleOutput), 'app.config.json');
  await fs.promises.writeFile(destination, JSON.stringify(exp, null, 2));
  return true;
}

/**
 * Builds `entryFile` to `bundleOutput`, and writes its assets beside it.
 *
 * Beside it by default, and that default is not arbitrary: React Native
 * resolves an asset relative to the script's own location, so anywhere else is
 * wrong unless the script is somewhere else too.
 */
export async function bundleWithAssets({
  projectRoot,
  entryFile,
  bundleOutput,
  assetsDest,
  platform = 'linux',
  dev = false,
  minify = !dev,
  configPath,
}: BundleOptions): Promise<BundleResult> {
  const {metro, Server} = loadMetro(projectRoot);

  const config = await metro.loadConfig({cwd: projectRoot, config: configPath});
  const destination = assetsDest ?? path.dirname(bundleOutput);

  await fs.promises.mkdir(path.dirname(bundleOutput), {recursive: true});
  await metro.runBuild(config, {
    entry: entryFile,
    out: bundleOutput,
    platform,
    dev,
    minify,
  });

  // A second Metro over the same config. Wasteful-looking and not: the build
  // above discards the graph, and this is the only public-ish way to ask which
  // assets it contained.
  const server = new Server(config);
  try {
    const assets = await server.getAssets({
      ...Server.DEFAULT_BUNDLE_OPTIONS,
      entryFile,
      platform,
      dev,
      minify,
      bundleType: 'todo',
    });
    const copied = await copyAssets(assets, destination);
    const expoConfig = await writeExpoAppConfig(projectRoot, bundleOutput);
    return {assets: assets.length, files: copied, assetsDest: destination, expoConfig};
  } finally {
    server.end();
  }
}
