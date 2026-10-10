// That `globalThis.__turboModuleProxy` answers, and answers for React Native's
// own modules rather than only for this platform's.
//
// The second half is the point. A proxy that served only the modules this
// platform registers would answer null for PlatformConstants -- and on the
// versions this exists for, a null from the proxy does not fall through to
// NativeModules. So "it exists" is not the claim worth testing; "it answers for
// something React Native provides" is.
import React from 'react';
import {AppRegistry, StyleSheet, Text, View} from 'react-native';
import {messageOf} from './errors';

// The audit's own table, imported rather than copied: `scripts/audit_modules.py`
// reads the provider chains and writes down who answers what, and this is the
// half of the claim that reads it back off a *running* host. A chain is code
// rather than a note about code and still not the same thing as a runtime
// answering, which is what the entry in docs/backlog/modules.md asked for.
import triage from '../docs/platform-modules.json';

const proxy = global.__turboModuleProxy;
console.log(`turbo proxy: ${typeof proxy}`);
if (typeof proxy === 'function') {
  // This platform's own, from core/.
  const ours = proxy('BasaltWindows');
  console.log(`turbo proxy BasaltWindows: ${ours == null ? 'null' : 'found'}`);
  // React Native's own, which a proxy of our own making would not have.
  const theirs = proxy('PlatformConstants');
  console.log(`turbo proxy PlatformConstants: ${theirs == null ? 'null' : 'found'}`);
  // And a name nobody provides, which must come back empty rather than throw --
  // TurboModuleRegistry.get is allowed to return null and callers rely on it.
  let unknown = 'threw';
  try {
    unknown = proxy('NoSuchModuleAtAll') == null ? 'null' : 'found';
  } catch (error) {
    unknown = `threw: ${messageOf(error)}`;
  }
  console.log(`turbo proxy unknown name: ${unknown}`);

  // Every module the audit triaged, against what the host actually answers.
  //
  // `nobody` has to come back null and anything else has to come back found.
  //
  // The modules carrying a `conditional` are skipped, and the three of those
  // that belong to React Native rather than to this platform are why this check
  // exists: a provider chain says *that* a module is offered and not *when*.
  // `DevLoadingView` wants a dev UI delegate, `NativeViewTransitionCxx` a
  // feature flag that is off, and `ReactDevToolsRuntimeSettingsModule` a debug
  // macro -- all three of which the static audit reported as answered, and a
  // plain run does not answer. The file names each condition.
  let agreed = 0;
  let disagreed = 0;
  // The shape of one entry, which the JSON import gives as `unknown`: the file
  // is data rather than a module with a type, and asserting the two fields this
  // reads is honest about which ones it depends on.
  const modules = triage.modules as {
    [name: string]: {answeredBy: string; conditional?: string};
  };
  for (const [name, entry] of Object.entries(modules)) {
    if (entry.conditional != null) {
      continue;
    }
    const wanted = entry.answeredBy !== 'nobody';
    let found = false;
    try {
      found = proxy(name) != null;
    } catch (error) {
      console.log(`module ${name}: threw: ${messageOf(error)}`);
      disagreed++;
      continue;
    }
    if (found === wanted) {
      agreed++;
    } else {
      disagreed++;
      console.log(
        `module ${name}: the audit says ${entry.answeredBy} and the host ` +
          `${found ? 'answered' : 'did not answer'}`,
      );
    }
  }
  console.log(`modules: ${agreed} agreed, ${disagreed} disagreed`);
}

function App() {
  return (
    <View style={styles.page}>
      <Text style={styles.label}>turbomodule proxy</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#14151a', padding: 24},
  label: {color: '#e6e6e6', fontSize: 16},
});

AppRegistry.registerComponent('BasaltTurboModules', () => App);
