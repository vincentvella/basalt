/**
 * What turns the `.app` a development run builds into one that runs on
 * somebody else's Mac. `package-macos` (cli/desktop.ts) calls these in order.
 *
 * A development `.app` works only here, and not for a cosmetic reason: the host
 * links Hermes and jsi through an rpath into this machine's build directory and
 * glog, fmt, boost and OpenSSL by absolute Homebrew path, so on any other Mac it
 * dies in dyld before a line of JavaScript. It is also signed ad hoc, which
 * Gatekeeper refuses for anything downloaded.
 *
 * So, in order:
 *
 *   embedLibraries   every non-system dylib the executable reaches, copied into
 *                    Contents/Frameworks and referenced as @rpath/<name>
 *   copyResources    what the app declares in app.json `basalt.resources`
 *   signApp          inside out, with a Developer ID and the hardened runtime
 *                    when one is given
 *
 * The icon is not here: packageApp.ts makes it on every run, so that a
 * development run shows it too.
 */

import * as fs from 'node:fs';
import * as path from 'node:path';
import {spawnSync} from 'node:child_process';

function run(command: string, args: string[]): string {
  const result = spawnSync(command, args, {encoding: 'utf8'});
  if (result.status !== 0) {
    throw new Error(
      `${command} ${args.join(' ')} failed:\n${(result.stderr || result.error?.message || '').trim()}`,
    );
  }
  return result.stdout;
}

/** A library the system provides, which an app must not carry. */
export function isSystemLibrary(name: string): boolean {
  return name.startsWith('/usr/lib/') || name.startsWith('/System/');
}

/**
 * The install names `otool -L` lists for one file, minus the file's own: a
 * dylib's first entry is its own id, which is not a dependency.
 */
export function parseLinkedLibraries(otoolOutput: string, ownId: string | null): string[] {
  return otoolOutput
    .split('\n')
    .slice(1)
    .map(line => line.trim().replace(/\s+\(compatibility version.*$/, ''))
    .filter(name => name !== '' && name !== ownId);
}

/** The LC_RPATH entries `otool -l` lists, in order. */
export function parseRpaths(otoolLoadCommands: string): string[] {
  const rpaths: string[] = [];
  const lines = otoolLoadCommands.split('\n');
  for (let i = 0; i < lines.length; i++) {
    if (lines[i].trim() === 'cmd LC_RPATH') {
      const match = /^\s*path (.+?) \(offset \d+\)/.exec(lines[i + 2] ?? '');
      if (match) {
        rpaths.push(match[1]);
      }
    }
  }
  return rpaths;
}

function dylibId(file: string): string | null {
  const lines = run('otool', ['-D', file]).trim().split('\n');
  return lines.length > 1 ? lines[1].trim() : null;
}

/**
 * Where a load command's name points on this machine, or null when nothing
 * there exists. `@rpath` is tried against each rpath the referencing file
 * carries, as dyld would.
 */
function resolveReference(name: string, referrer: string, executable: string): string | null {
  const expand = (p: string): string =>
    p
      .replace('@executable_path', path.dirname(executable))
      .replace('@loader_path', path.dirname(referrer));
  const candidates = name.startsWith('@rpath/')
    ? parseRpaths(run('otool', ['-l', referrer])).map(r =>
        path.join(expand(r), name.slice('@rpath/'.length)),
      )
    : [expand(name)];
  return candidates.find(c => fs.existsSync(c)) ?? null;
}

/**
 * Copies every non-system dylib `executable` reaches, transitively, into
 * `<app>/Contents/Frameworks`, and rewrites each reference to `@rpath/<name>`.
 * The executable's rpaths into build directories are replaced by one,
 * `@executable_path/../Frameworks`. Returns the libraries embedded.
 */
export function embedLibraries(appDir: string, executable: string): string[] {
  const frameworks = path.join(appDir, 'Contents', 'Frameworks');
  fs.mkdirSync(frameworks, {recursive: true});
  const embedded = new Map<string, string>();
  const queue = [executable];

  while (queue.length > 0) {
    const file = queue.shift() as string;
    const ownId = file === executable ? null : dylibId(file);
    for (const name of parseLinkedLibraries(run('otool', ['-L', file]), ownId)) {
      if (isSystemLibrary(name)) {
        continue;
      }
      const base = path.basename(name);
      const destination = path.join(frameworks, base);
      if (!embedded.has(base)) {
        const source = resolveReference(name, file, executable);
        if (source == null) {
          throw new Error(`${path.basename(file)} links ${name}, which is not on this machine`);
        }
        if (path.resolve(source) !== path.resolve(destination)) {
          fs.copyFileSync(fs.realpathSync(source), destination);
          fs.chmodSync(destination, 0o755);
        }
        run('install_name_tool', ['-id', `@rpath/${base}`, destination]);
        embedded.set(base, destination);
        queue.push(destination);
      }
      if (name !== `@rpath/${base}`) {
        run('install_name_tool', ['-change', name, `@rpath/${base}`, file]);
      }
    }
  }

  // The executable finds them through one rpath; each embedded library finds
  // its neighbours through its own directory.
  const rpaths = parseRpaths(run('otool', ['-l', executable]));
  for (const rpath of rpaths) {
    if (!rpath.startsWith('@')) {
      run('install_name_tool', ['-delete_rpath', rpath, executable]);
    }
  }
  if (!rpaths.includes('@executable_path/../Frameworks')) {
    run('install_name_tool', ['-add_rpath', '@executable_path/../Frameworks', executable]);
  }
  for (const library of embedded.values()) {
    if (!parseRpaths(run('otool', ['-l', library])).includes('@loader_path')) {
      run('install_name_tool', ['-add_rpath', '@loader_path', library]);
    }
  }
  return [...embedded.keys()];
}

/**
 * Copies what app.json declares under `basalt.resources` -- `{"<path in
 * Resources>": "<path in the project>"}`, a file or a directory -- into the
 * bundle's resources. Executable bits survive, which is what lets an app carry
 * a binary it runs.
 */
export function copyResources(
  projectRoot: string,
  resources: Record<string, string>,
  resourcesDir: string,
): string[] {
  const copied: string[] = [];
  for (const [destination, source] of Object.entries(resources)) {
    const from = path.resolve(projectRoot, source);
    const to = path.resolve(resourcesDir, destination);
    if (!to.startsWith(path.resolve(resourcesDir) + path.sep)) {
      throw new Error(`basalt.resources: ${destination} is outside the app's resources`);
    }
    if (!fs.existsSync(from)) {
      throw new Error(`basalt.resources: ${source} does not exist`);
    }
    fs.mkdirSync(path.dirname(to), {recursive: true});
    fs.cpSync(from, to, {recursive: true, dereference: true});
    copied.push(destination);
  }
  return copied;
}

/** Whether a file is Mach-O, thin or universal, by its first four bytes. */
function isMachO(file: string): boolean {
  const handle = fs.openSync(file, 'r');
  try {
    const magic = Buffer.alloc(4);
    if (fs.readSync(handle, magic, 0, 4, 0) < 4) {
      return false;
    }
    return ['cffaedfe', 'cefaedfe', 'feedfacf', 'feedface', 'cafebabe'].includes(
      magic.toString('hex'),
    );
  } finally {
    fs.closeSync(handle);
  }
}

function walk(directory: string): string[] {
  if (!fs.existsSync(directory)) {
    return [];
  }
  return fs.readdirSync(directory, {withFileTypes: true}).flatMap(entry => {
    const full = path.join(directory, entry.name);
    return entry.isDirectory() ? walk(full) : entry.isFile() ? [full] : [];
  });
}

export type SignOptions = {
  /** A codesigning identity, e.g. "Developer ID Application: …". Ad hoc when absent. */
  identity?: string;
  /** An entitlements plist, applied to every executable (not to libraries). */
  entitlements?: string;
};

/**
 * Signs the bundle inside out: every Mach-O file under Frameworks and
 * Resources first, then the app. With an identity, each signature carries the
 * hardened runtime and a secure timestamp, which notarization requires; ad hoc
 * gets neither, because neither means anything without an identity.
 */
export function signApp(appDir: string, {identity, entitlements}: SignOptions): void {
  const real = identity != null && identity !== '-';
  const base = ['--force', '--sign', real ? identity : '-', ...(real ? ['--options', 'runtime', '--timestamp'] : [])];
  const nested = [
    ...walk(path.join(appDir, 'Contents', 'Frameworks')),
    ...walk(path.join(appDir, 'Contents', 'Resources')),
  ].filter(isMachO);
  for (const file of nested) {
    const isExecutable = (fs.statSync(file).mode & 0o111) !== 0 && !file.endsWith('.dylib');
    const extra = isExecutable && entitlements ? ['--entitlements', entitlements] : [];
    run('codesign', [...base, ...extra, file]);
  }
  run('codesign', [...base, ...(entitlements ? ['--entitlements', entitlements] : []), appDir]);
  run('codesign', ['--verify', '--deep', '--strict', appDir]);
}
