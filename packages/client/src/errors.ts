// Errors the CLI turns into one line on stderr and an exit code:
// 1 for runtime failures, 2 for usage and configuration problems.

export class CliError extends Error {
  override name = "CliError";

  constructor(
    message: string,
    readonly exitCode: 1 | 2 = 1,
    /** True when trying again later can succeed (server down, rate limited). */
    readonly retryable = false,
  ) {
    super(message);
  }
}

export class UsageError extends CliError {
  override name = "UsageError";

  constructor(message: string) {
    super(message, 2);
  }
}

export class ConfigError extends CliError {
  override name = "ConfigError";

  constructor(message: string) {
    super(message, 2);
  }
}

/** The server is a multi-user server and this client holds no credentials for it. */
export class LoginRequiredError extends CliError {
  override name = "LoginRequiredError";

  constructor(readonly serverUrl: string) {
    super(`${serverUrl} is a multi-user server: run \`devotchi login --server ${serverUrl}\` first`);
  }
}

export function errorMessage(err: unknown): string {
  return err instanceof Error ? err.message : String(err);
}
