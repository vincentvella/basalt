/**
 * `decelerationRate`, for a desktop React Native platform.
 *
 * Not a shim, and the same failure as `<Button>`'s: React Native's own file is
 * two `Platform.select({ios, android})` tables with no `default`, so on
 * `linux`, `macos` or `windows` both of the prop's documented spellings --
 * `'normal'` and `'fast'` -- resolve to `undefined`. `BaseScrollViewProps`
 * then keeps its own default of 0.998, so `decelerationRate="fast"` scrolled
 * at the normal rate and nothing said otherwise. Only a raw number worked.
 *
 * iOS's pair, because they are the pair this platform's own fling already
 * uses: `core/ScrollMomentum.h` names 0.998 and 0.99 as what `'normal'` and
 * `'fast'` mean, and a per-millisecond friction factor is the same model
 * UIScrollView and every browser use. Android's 0.985 and 0.9 are a different
 * curve's constants and would make a fling stop in a third of the distance.
 *
 * @format
 */

export default function processDecelerationRate(
  decelerationRate: number | 'normal' | 'fast',
): number {
  if (decelerationRate === 'normal') {
    return 0.998;
  }
  if (decelerationRate === 'fast') {
    return 0.99;
  }
  return decelerationRate;
}
