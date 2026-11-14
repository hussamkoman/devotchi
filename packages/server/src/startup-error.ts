/** A failure to start whose message is one line fit for the terminal. */
export class StartupError extends Error {
  override name = "StartupError";
}
