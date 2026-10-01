// What a command checked, and how it says so.
//
// `init` and `doctor` are the same checks with writing on and off, so they
// report the same way: a list of named steps, each already right, changed, or
// needing a person. The summary is written once at the end rather than printed
// as it goes, because a run that refuses halfway should not have narrated four
// successes first.
//
// This is shared rather than duplicated for the reason the two commands exist
// separately at all: `doctor` is `init` with its hands behind its back, plus
// the checks that have no fix. If they drifted into reporting differently, the
// second would stop being a straight answer about the first.

/**
 * Already right, changed by this run, needs a person, or could not be asked.
 *
 * `unchecked` is the one that earns its place. Whether GTK's headers are
 * installed is not a question a Mac can answer, and reporting it as passing
 * would make the command worse than not running it -- while reporting it as
 * needing a person would send somebody to install GTK on a Mac. It is neither
 * a failure nor a success, so it is counted as neither.
 */
export type StepState = 'done' | 'changed' | 'blocked' | 'unchecked';

export type Step = {
  state: StepState;
  message: string;
};

export const DONE: StepState = 'done';
export const CHANGED: StepState = 'changed';
export const BLOCKED: StepState = 'blocked';
export const UNCHECKED: StepState = 'unchecked';

export function step(state: StepState, message: string): Step {
  return {state, message};
}

/** A step and the name it is reported under. */
export type NamedStep = [string, Step];

/**
 * Prints the steps and answers with the exit code.
 *
 * `=` already right, `+` this run changed it, `!` somebody has to, `?` could
 * not be asked from here. One character rather than a word so that a column of
 * them reads at a glance, which is the whole reason for reporting steps
 * instead of just failing.
 */
export function report(
  steps: readonly NamedStep[],
  out: (line: string) => void,
): {changed: number; blocked: number} {
  let changed = 0;
  let blocked = 0;
  const width = steps.reduce((longest, [name]) => Math.max(longest, name.length), 0);

  for (const [name, outcome] of steps) {
    const mark =
      outcome.state === DONE
        ? '='
        : outcome.state === CHANGED
          ? '+'
          : outcome.state === UNCHECKED
            ? '?'
            : '!';
    out(`  ${mark} ${name.padEnd(width)}  ${outcome.message}`);
    if (outcome.state === CHANGED) changed++;
    if (outcome.state === BLOCKED) blocked++;
  }
  return {changed, blocked};
}
