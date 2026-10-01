/**
 * Turning a host binary into the thing each desktop calls an application.
 *
 * `--build` produces an executable, and an executable is not an application on
 * any of these three. What it costs to stay a bare binary is not cosmetic:
 *
 *   macOS    `UNUserNotificationCenter` does not merely fail for a process with
 *            no bundle identifier -- it raises `bundleProxyForCurrentProcess is
 *            nil` and terminates. So notifications are unreachable, and so are
 *            the Dock icon, the menu bar name, file associations and a
 *            registered URL scheme, all of which live in the same `Info.plist`.
 *
 *   Linux    Notifications work without one, because the D-Bus service takes
 *            the name in the call. What a `.desktop` file adds is identity: the
 *            app's icon beside its notifications, its name in the switcher, and
 *            `x-scheme-handler/` so a link opens it.
 *
 *   Windows  A toast needs an AppUserModelID and a Start Menu shortcut carrying
 *            it. Both are done by the host itself rather than from here -- a
 *            `.lnk` with that property set means `IPropertyStore`, which is
 *            COM, and doing it in C++ at startup is simpler than doing it in
 *            PowerShell from Node. See win32/Win32Packaging.h.
 *
 * So this file is macOS and Linux, and Windows is a no-op that says why.
 *
 * ## The macOS bundle is not only for release
 *
 * `NSBundle.mainBundle` comes from where the executable *is*, not from how it
 * was launched. An executable at `Foo.app/Contents/MacOS/Foo` is bundled even
 * when run straight from a shell -- so packaging on every run, including a
 * development one, is what makes `Notification` behave the same in both. The
 * copy is skipped when the binary has not changed, which is what keeps that
 * from costing forty megabytes a run.
 */


import * as fs from 'node:fs';
import * as path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createRequire} from 'node:module';

/** The three desktops this can package for. */
export type PackagePlatform = 'linux' | 'macos' | 'windows';

/**
 * What an app calls itself, gathered from whatever it already declares --
 * `app.json`, `package.json`, or an Expo config. Never invented beyond the
 * documented fallbacks.
 */
export type AppConfig = {
  name: string;
  slug: string;
  /** Reverse-DNS, which is what macOS and notifications need. */
  identifier: string;
  /** Custom URL schemes the app registers. */
  schemes: string[];
  version: string;
};

export type PackageRequest = {
  platform: PackagePlatform;
  hostBinary: string;
  outputDir: string;
  projectRoot: string;
  bundlePath?: string;
};

/**
 * What was produced.
 *
 * `launchPath` is the thing to run, which is not always the host binary: on
 * macOS it is the executable *inside* the bundle, because that is what makes
 * `NSBundle.mainBundle` the bundle.
 */
export type PackageResult = {
  launchPath: string;
  config: AppConfig;
  /** Where the identity file went, or null when there was no bundle to sit beside. */
  identityPath: string | null;
  /** macOS only: the `.app` that was built. */
  appPath?: string;
  /** Linux only: the `.desktop` entry that was written. */
  desktopPath?: string;
};

/** What each platform's packager returns, before the shared fields are added. */
type PackagedPaths = {
  launchPath: string;
  appPath?: string;
  desktopPath?: string;
};

/** An app.json or package.json, as far as this reads one. */
type Manifest = {
  name?: string;
  displayName?: string;
  version?: string;
  scheme?: string | string[];
  /** This platform's own escape hatch in app.json, for an app that names neither. */
  basalt?: {identifier?: string; scheme?: string | string[]};
  expo?: Record<string, unknown>;
  [key: string]: unknown;
};

/** The parts of an Expo config this uses. */
type ExpoConfig = {
  name?: string;
  slug?: string;
  version?: string;
  scheme?: string | string[];
  ios?: {bundleIdentifier?: string};
  android?: {package?: string};
  [key: string]: unknown;
};

/**
 * What an app calls itself, from whichever of the three places says so.
 *
 * Expo's config wins where there is one, because an Expo app's `app.json` is
 * read through `expo/config` and may be a function; `app.json` is the plain
 * React Native answer; `package.json` is the last resort so that this never
 * fails outright.
 */
export function readAppConfig(projectRoot: string): AppConfig {
  let exp: ExpoConfig | null = null;
  try {
    const {getConfig} = createRequire(path.join(projectRoot, 'package.json'))(
      'expo/config',
    ) as {
      getConfig: (
        root: string,
        options: {isPublicConfig?: boolean; skipSDKVersionRequirement?: boolean},
      ) => {exp: ExpoConfig};
    };
    exp = getConfig(projectRoot, {
      skipSDKVersionRequirement: true,
      isPublicConfig: true,
    }).exp;
  } catch {
    // Not an Expo app. Not a failure: the two files below say the same things.
  }

  const readJson = (file: string): Manifest => {
    try {
      return JSON.parse(fs.readFileSync(path.join(projectRoot, file), 'utf8')) as Manifest;
    } catch {
      return {};
    }
  };
  const appJson = readJson('app.json');
  const packageJson = readJson('package.json');

  const name = exp?.name ?? appJson.displayName ?? appJson.name ?? packageJson.name ?? 'App';
  const slug = exp?.slug ?? appJson.name ?? packageJson.name ?? 'app';

  // A reverse-DNS identifier, from whatever the app already declares for a
  // phone. Inventing one is the last resort and is deliberately visible in the
  // string, because an app that ships with `com.basalt.<slug>` should be able
  // to tell that nobody chose it.
  const identifier =
    appJson.basalt?.identifier ??
    exp?.ios?.bundleIdentifier ??
    exp?.android?.package ??
    `com.basalt.${String(slug).replace(/[^A-Za-z0-9-]/g, '-').toLowerCase()}`;

  // `expo.scheme` is a string or a list of them. Everything downstream wants a
  // list, so it becomes one here.
  const scheme = exp?.scheme ?? appJson.basalt?.scheme ?? null;
  const schemes = scheme == null ? [] : Array.isArray(scheme) ? scheme : [scheme];

  return {
    name,
    slug,
    identifier,
    schemes,
    version: exp?.version ?? appJson.version ?? packageJson.version ?? '1.0.0',
  };
}

/** XML text, escaped. A plist is XML and an app called `Ben & Co` is legal. */
function xml(text: string): string {
  return String(text)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

export function infoPlist(config: AppConfig, executableName: string): string {
  const urlTypes =
    config.schemes.length === 0
      ? ''
      : `
	<key>CFBundleURLTypes</key>
	<array>
		<dict>
			<key>CFBundleURLName</key>
			<string>${xml(config.identifier)}</string>
			<key>CFBundleURLSchemes</key>
			<array>
${config.schemes.map(s => `				<string>${xml(s)}</string>`).join('\n')}
			</array>
		</dict>
	</array>`;

  return `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleExecutable</key>
	<string>${xml(executableName)}</string>
	<key>CFBundleIdentifier</key>
	<string>${xml(config.identifier)}</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>${xml(config.name)}</string>
	<key>CFBundleDisplayName</key>
	<string>${xml(config.name)}</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>${xml(config.version)}</string>
	<key>CFBundleVersion</key>
	<string>${xml(config.version)}</string>
	<key>LSMinimumSystemVersion</key>
	<string>11.0</string>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>NSSupportsAutomaticGraphicsSwitching</key>
	<true/>${urlTypes}
</dict>
</plist>
`;
}

/** Whether `source` is newer than `destination`, or the destination is absent. */
function isStale(source: string, destination: string): boolean {
  try {
    return fs.statSync(source).mtimeMs > fs.statSync(destination).mtimeMs;
  } catch {
    return true;
  }
}

/**
 * Builds `<name>.app` around the host binary and returns the executable inside
 * it -- which is what should be launched, because that is what makes
 * `NSBundle.mainBundle` the bundle.
 */
function packageMacos(hostBinary: string, outputDir: string, config: AppConfig): PackagedPaths {
  const appDir = path.join(outputDir, `${config.name}.app`);
  const macosDir = path.join(appDir, 'Contents', 'MacOS');
  const resourcesDir = path.join(appDir, 'Contents', 'Resources');
  fs.mkdirSync(macosDir, {recursive: true});
  fs.mkdirSync(resourcesDir, {recursive: true});

  // The executable keeps the host's name rather than the app's. It is what
  // shows in `ps` and in a crash report, and "basalt_appkit" there is more
  // useful than the app's name, which is already on the bundle.
  const executableName = path.basename(hostBinary);
  const executable = path.join(macosDir, executableName);
  const signed = isStale(hostBinary, executable);
  if (signed) {
    fs.copyFileSync(hostBinary, executable);
    fs.chmodSync(executable, 0o755);
  }

  const plistPath = path.join(appDir, 'Contents', 'Info.plist');
  const plist = infoPlist(config, executableName);
  const plistChanged = !fs.existsSync(plistPath) || fs.readFileSync(plistPath, 'utf8') !== plist;
  if (plistChanged) {
    fs.writeFileSync(plistPath, plist);
  }
  // Launch Services caches a bundle's plist aggressively, and an app whose
  // identifier changed under it keeps the old one until something touches the
  // directory. Touching it is cheap and the alternative is a stale identity
  // that looks like a bug in the plist.
  fs.utimesSync(appDir, new Date(), new Date());

  // Signed ad hoc, and this is not optional.
  //
  // An unsigned bundle has an identifier and still cannot notify: macOS
  // answers `requestAuthorization` with UNErrorDomain error 1, "notifications
  // are not allowed", because the identity it would attach the permission to
  // is not stable. `--sign -` is the ad-hoc identity, needs no developer
  // account and no keychain, and is enough -- found by the notification
  // arriving the moment it was applied and not before.
  //
  // Only when something changed, because signing walks the whole binary and
  // the host is tens of megabytes.
  if (signed || plistChanged) {
    const result = spawnSync('codesign', ['--force', '--sign', '-', appDir], {encoding: 'utf8'});
    if (result.status !== 0) {
      // Not fatal. Everything except notifications works unsigned, and a build
      // that stopped here would be worse than one that says what is missing.
      console.warn(
        '==> could not sign the app bundle; notifications will be refused by macOS\n' +
          `    ${(result.stderr ?? result.error?.message ?? '').trim()}`,
      );
    }
  }

  return {launchPath: executable, appPath: appDir};
}

/**
 * Writes a `.desktop` file for the app and returns where it went.
 *
 * Not installed, only written. Putting a file into
 * `~/.local/share/applications` is a change to the user's session that a build
 * command should not make on its own; the caller prints the one-line copy that
 * does it.
 */
function packageLinux(hostBinary: string, outputDir: string, config: AppConfig): PackagedPaths {
  const fileName = `${config.identifier}.desktop`;
  const destination = path.join(outputDir, fileName);

  const mime = config.schemes.map(s => `x-scheme-handler/${s};`).join('');
  const entry =
    `[Desktop Entry]\n` +
    `Type=Application\n` +
    `Name=${config.name}\n` +
    `Exec=${hostBinary} %U\n` +
    `Icon=${config.identifier}\n` +
    `Terminal=false\n` +
    `Categories=Utility;\n` +
    `StartupNotify=true\n` +
    // The window's own application id, which is what lets the desktop match a
    // running window to this file -- and so what puts the right name and icon
    // on its notifications.
    `StartupWMClass=${config.identifier}\n` +
    (mime === '' ? '' : `MimeType=${mime}\n`);

  fs.writeFileSync(destination, entry);
  return {launchPath: hostBinary, desktopPath: destination};
}

/**
 * Packages `hostBinary` for `platform`, into `outputDir`.
 *
 * Returns `{launchPath, ...}`: what to run. On Linux and Windows that is the
 * binary it was given, because neither makes the application out of where the
 * executable sits; on macOS it is the copy inside the bundle.
 */
/**
 * Writes what the app calls itself beside the bundle, for the host to read.
 *
 * Beside the bundle rather than beside the executable, because the executable
 * is shared by every app on the machine; and written rather than passed as an
 * argument, because a Start Menu shortcut clicked in a month carries no
 * environment. See native/core/AppIdentity.h.
 */
export function writeIdentity(
  bundlePath: string | undefined,
  config: AppConfig,
): string | null {
  if (bundlePath == null) {
    return null;
  }
  const destination = path.join(path.dirname(bundlePath), 'app.identity.json');
  fs.mkdirSync(path.dirname(destination), {recursive: true});
  fs.writeFileSync(
    destination,
    JSON.stringify(
      {
        name: config.name,
        identifier: config.identifier,
        schemes: config.schemes,
        version: config.version,
      },
      null,
      2,
    ),
  );
  return destination;
}

export function packageApp({
  platform,
  hostBinary,
  outputDir,
  projectRoot,
  bundlePath,
}: PackageRequest): PackageResult {
  const config = readAppConfig(projectRoot);
  fs.mkdirSync(outputDir, {recursive: true});
  const identityPath = writeIdentity(bundlePath, config);

  if (platform === 'macos') {
    return {config, identityPath, ...packageMacos(hostBinary, outputDir, config)};
  }
  if (platform === 'linux') {
    return {config, identityPath, ...packageLinux(hostBinary, outputDir, config)};
  }
  // Windows does its own, in the host: a Start Menu shortcut carrying an
  // AppUserModelID needs IPropertyStore, and doing that in C++ at startup beats
  // doing it in PowerShell from here. See win32/Win32Packaging.h.
  return {config, identityPath, launchPath: hostBinary};
}
