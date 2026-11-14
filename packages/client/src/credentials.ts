// Getting a usable API token for one server: DEVOTCHI_TOKEN, the stored
// credentials, or (single-user servers only) a newly minted cli token.

import type { ServerCredentials } from "@devotchi/shared";
import { ApiClient, ApiError } from "./api.js";
import { normaliseServerUrl, type ConfigStore } from "./config.js";
import { CliError, LoginRequiredError } from "./errors.js";

export const CLI_TOKEN_LABEL = "cli";

export interface ResolvedCredentials extends ServerCredentials {
  source: "env" | "config" | "provisioned";
}

/** What a long-lived connection needs: a token now, and a fresh one after a 401. */
export interface CredentialSource {
  get(): Promise<ResolvedCredentials>;
  renew(rejectedToken: string): Promise<ResolvedCredentials>;
}

export class ServerCredentialSource implements CredentialSource {
  readonly serverUrl: string;
  private readonly api: ApiClient;

  constructor(
    private readonly store: ConfigStore,
    serverUrl: string,
    private readonly log: (line: string) => void = () => undefined,
  ) {
    this.serverUrl = normaliseServerUrl(serverUrl);
    this.api = new ApiClient(this.serverUrl);
  }

  /** ensureToken: the env token, the stored one, or a newly provisioned one. */
  async get(): Promise<ResolvedCredentials> {
    const envToken = this.store.envToken;
    if (envToken !== undefined) return { token: envToken, roomId: "", source: "env" };
    const stored = await this.store.getCredentials(this.serverUrl);
    if (stored !== null) return { ...stored, source: "config" };
    return this.provision(null);
  }

  /** Called once after the server answered 401 to `rejectedToken`. */
  async renew(rejectedToken: string): Promise<ResolvedCredentials> {
    if (this.store.envToken !== undefined) {
      throw new CliError(`${this.serverUrl} rejected the token in DEVOTCHI_TOKEN`);
    }
    return this.provision(rejectedToken);
  }

  /** GET /api/me with the token; a stored token the server rejects is replaced once (single-user). */
  async verify(creds: ResolvedCredentials): Promise<ResolvedCredentials> {
    try {
      await this.api.withToken(creds.token).me();
      return creds;
    } catch (err) {
      if (!(err instanceof ApiError) || err.status !== 401 || creds.source === "env") throw err;
    }
    const renewed = await this.renew(creds.token);
    await this.api.withToken(renewed.token).me();
    return renewed;
  }

  /**
   * Under the config lock: if another process stored usable credentials in
   * the meantime they are used, otherwise a token is minted. Only
   * single-user servers mint anonymously.
   */
  private async provision(rejectedToken: string | null): Promise<ResolvedCredentials> {
    return this.store.withLock(async (tx) => {
      const config = await tx.read();
      const current = config.servers[this.serverUrl];
      if (current !== undefined && current.token !== rejectedToken) return { ...current, source: "config" };

      const health = await this.api.health();
      if (health.mode === "multi-user") throw new LoginRequiredError(this.serverUrl);

      let minted;
      try {
        minted = await this.api.mintToken(CLI_TOKEN_LABEL);
      } catch (err) {
        if (err instanceof ApiError && err.status === 401) {
          throw new CliError(
            `${this.serverUrl} requires the admin secret to create tokens; mint one as described in docs/deploy.md and set DEVOTCHI_TOKEN`,
          );
        }
        if (err instanceof ApiError && err.status === 429) {
          throw new ApiError(
            `${this.serverUrl} is rate limiting token requests; retry in ${err.retryAfterS ?? 60} s`,
            err.status,
            err.code,
            err.retryAfterS,
          );
        }
        throw err;
      }
      config.servers[this.serverUrl] = { token: minted.token, roomId: minted.roomId };
      await tx.write(config);
      if (rejectedToken !== null) {
        this.log(`the server rejected the stored token; provisioned a new one (room ${minted.roomId})`);
      } else {
        this.log(`provisioned a token for ${this.serverUrl} (room ${minted.roomId})`);
      }
      return { token: minted.token, roomId: minted.roomId, source: "provisioned" };
    });
  }
}
