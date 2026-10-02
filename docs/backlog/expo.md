# Expo, beyond the template

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (11):**

1. expo-image's decorative props
2. More Expo views
3. A URL delivered to a *running* app
4. The clipboard's image and URL types
5. A frame source the animation systems can share
6. `core/ReanimatedCompat.h` has never been compiled
7. Reanimated's layout animations and shared element transitions
8. `synchronouslyUpdateUIProps`
9. The gestures a cursor cannot make
10. The rest of RNGH's relation graph
11. A visible error when the bundle throws

Measured in phase 34 against a real dependency set, on both desktops.

- **expo-image's decorative props**: placeholder, transition, blurhash, cache
  policy. Left out of the view config in phase 36, so they are dropped in
  JavaScript and an app gets an image without them.
- **More Expo views.** The seam exists now (phase 36), so expo-linear-gradient,
  expo-blur and the rest are each a props class, a descriptor and a mounting
  peer rather than a new mechanism.
- **A URL delivered to a *running* app.** `Linking.getInitialURL()` works on all
  three now: a desktop hands a URL over on the command line, so each host
  records whichever argument carried a scheme. What is still missing is the
  second delivery, to an application that is already open, and that is where the
  three desktops diverge: a GApplication with `G_APPLICATION_HANDLES_OPEN` on
  Linux, an Apple Event handler and a registered scheme on macOS, a named pipe
  and a shell association on Windows. `ExpoLinking.getLinkingURL` is the same
  question and would come with it.
- **The clipboard's image and URL types**, which are separate pasteboard types
  on each platform. Absent rather than stubbed, so expo-clipboard reports them
  as unavailable by name.
- **A frame source the animation systems can share.** Worklets and Reanimated
  both run on a sixteen-millisecond timer, because each platform's display link
  belongs to React Native's `AnimationChoreographer` and pauses whenever React
  Native has no animation of its own.
- **`core/ReanimatedCompat.h` has never been compiled.** It exists for a
  platform that is neither Android nor Apple; the Linux host here is built on
  macOS. The first real Linux build is its first test.
- **Reanimated's layout animations and shared element transitions** compile and
  have never been run.
- **`synchronouslyUpdateUIProps`**, Reanimated's direct-to-view path, is a
  no-op: the mounting managers accept mutations only from a Fabric transaction.
- **The gestures a cursor cannot make**: pinch, rotation and force touch begin
  and fail, because there is no second finger and no pressure. Two-finger
  trackpad gestures exist on both desktops and are not wired to anything.
- **The rest of RNGH's relation graph**: `blocksHandlers`, and a `waitFor` that
  resolves across detectors rather than within one gesture.
- **A visible error when the bundle throws.** Today the window stays empty and
  the only evidence is a line in the host's log. Every failure above, and every
  future one, is invisible to whoever is running the app.
