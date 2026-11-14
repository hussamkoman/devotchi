// `devotchi login` and `devotchi logout` (multi-user servers).

import { createInterface } from "node:readline";
import { ApiClient, ApiError } from "../api.js";
import { CLI_TOKEN_LABEL } from "../credentials.js";
import { ConfigStore } from "../config.js";
import type { Io } from "../context.js";
import { CliError, UsageError, errorMessage } from "../errors.js";

export interface PromptIo {
  input: NodeJS.ReadStream;
  output: NodeJS.WriteStream;
}

/** One line from a pipe, or typed on a terminal without echo. */
export function readLine(prompt: string, io: PromptIo, secret: boolean): Promise<string> {
  const { input, output } = io;
  if (!input.isTTY) {
    return new Promise((resolve, reject) => {
      const rl = createInterface({ input, terminal: false });
      let done = false;
      rl.once("line", (line) => {
        done = true;
        rl.close();
        resolve(line);
      });
      rl.once("close", () => {
        if (!done) reject(new UsageError(`no ${secret ? "password" : "input"} on standard input`));
      });
    });
  }
  output.write(prompt);
  if (!secret) {
    return new Promise((resolve) => {
      const rl = createInterface({ input, output, terminal: true });
      rl.once("line", (line) => {
        rl.close();
        resolve(line);
      });
    });
  }
  return new Promise((resolve, reject) => {
    let value = "";
    input.setRawMode(true);
    input.resume();
    input.setEncoding("utf8");
    const finish = (err?: Error) => {
      input.setRawMode(false);
      input.pause();
      input.off("data", onData);
      output.write("\n");
      if (err !== undefined) reject(err);
      else resolve(value);
    };
    const onData = (chunk: string) => {
      for (const ch of chunk) {
        if (ch === "\r" || ch === "\n") return finish();
        if (ch === "\u0003") return finish(new CliError("cancelled"));
        if (ch === "\u007f" || ch === "\b") value = value.slice(0, -1);
        else if (ch >= " ") value += ch;
      }
    };
    input.on("data", onData);
  });
}

export interface LoginOptions {
  server?: string;
  email?: string;
}

export async function login(options: LoginOptions, io: Io, prompt: PromptIo): Promise<number> {
  const store = new ConfigStore(io.env);
  const { url } = await store.resolveServer(options.server);
  const api = new ApiClient(url);
  const health = await api.health();
  if (health.mode !== "multi-user") {
    throw new UsageError(`${url} is a single-user server; it needs no login (devotchi setup creates a token)`);
  }
  const email = (options.email ?? (await readLine("Email: ", prompt, false))).trim();
  if (email === "") throw new UsageError("an email address is required");
  const password = await readLine("Password: ", prompt, true);

  let session: string;
  try {
    session = (await api.login(email, password)).sessionToken;
  } catch (err) {
    if (err instanceof ApiError && err.status === 401) throw new CliError("wrong email or password");
    if (err instanceof ApiError && err.status === 429) {
      throw new CliError(`too many login attempts; wait ${err.retryAfterS ?? 60} s and try again`);
    }
    throw err;
  }
  const withSession = api.withToken(session);
  try {
    const minted = await withSession.mintToken(CLI_TOKEN_LABEL);
    await store.setCredentials(url, { token: minted.token, roomId: minted.roomId });
    io.out(`Logged in to ${url} as ${email}; this computer now has its own token for room ${minted.roomId}.`);
  } finally {
    await withSession.logout().catch((err: unknown) => io.note(`could not end the login session: ${errorMessage(err)}`));
  }
  return 0;
}

export interface LogoutOptions {
  server?: string;
  revoke?: boolean;
}

export async function logout(options: LogoutOptions, io: Io): Promise<number> {
  const store = new ConfigStore(io.env);
  const { url } = await store.resolveServer(options.server);
  const creds = await store.getCredentials(url);
  if (creds === null) {
    io.out(`No credentials stored for ${url}; nothing to do.`);
    return 0;
  }
  if (options.revoke === true) {
    const api = new ApiClient(url, { token: creds.token });
    try {
      const me = await api.me();
      if (me.kind === "token") await api.deleteToken(me.tokenId);
      io.out(`Revoked the token on ${url}.`);
    } catch (err) {
      if (!(err instanceof ApiError && err.status === 401)) {
        throw new CliError(`could not revoke the token: ${errorMessage(err)}; nothing was removed`);
      }
      io.out(`${url} no longer accepts the stored token.`);
    }
  }
  await store.removeCredentials(url);
  io.out(`Removed the credentials for ${url} from ${store.path}.`);
  return 0;
}
