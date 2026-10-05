/**
 * What a caught thing says about itself.
 *
 * `catch` gives `unknown` under `strict`, and these apps log what they caught
 * rather than handling it, so every one of them wants the same two sentences
 * about narrowing. They live here instead.
 *
 * The strings are what the scenarios in `scripts/integration_test.py` assert
 * on, so these return exactly what `error.message` and
 * `error.constructor.name` returned before any of this was typed.
 *
 * @format
 */

/** `error.message`, for something that may not be an Error at all. */
export function messageOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

/** `error.constructor.name`, which is how one scenario tells apart two failures. */
export function nameOf(error: unknown): string {
  if (error instanceof Error) {
    return error.constructor.name;
  }
  return typeof error;
}
