// Account routes of multi-user mode (protocol 7.3) and the /account page
// (architecture 2.5). Not mounted in single-user mode, so they answer 404 there.

import { createHash } from "node:crypto";
import Router from "@koa/router";
import type { LoginResponse, RegisterResponse } from "@devotchi/shared";
import { EmailTakenError, isAcceptablePassword, normaliseEmail, type AccountService } from "../accounts/service.js";
import type { ResolvedConfig } from "../config.js";
import { authOf, requireAuth, requireSession, secretMatches, type Authenticator } from "../http/auth.js";
import { HttpError, badRequest, forbidden, rateLimited, unauthorized } from "../http/errors.js";
import { objectBody, optionalString, requiredString } from "../http/fields.js";
import { byIp, type RateLimits } from "../http/rate-limit.js";

export interface AccountRouteDeps {
  config: ResolvedConfig;
  auth: Authenticator;
  limits: RateLimits;
  accounts: AccountService;
  /** Contents of public/account.html. */
  accountPage: string;
}

const cspHash = (source: string) => `'sha256-${createHash("sha256").update(source).digest("base64")}'`;

/**
 * The page's inline script and style are allowed by hash and nothing else
 * is: no other script, no framing, requests only to this origin.
 */
export function accountPageCsp(html: string): string {
  const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => cspHash(m[1]));
  const styles = [...html.matchAll(/<style>([\s\S]*?)<\/style>/g)].map((m) => cspHash(m[1]));
  return [
    "default-src 'none'",
    `script-src ${scripts.join(" ") || "'none'"}`,
    `style-src ${styles.join(" ") || "'none'"}`,
    "connect-src 'self'",
    "base-uri 'none'",
    "form-action 'none'",
    "frame-ancestors 'none'",
  ].join("; ");
}

export function accountRoutes(deps: AccountRouteDeps): Router {
  const { config, auth, limits, accounts, accountPage } = deps;
  const router = new Router();
  const loginFailures = limits.create(5, 15);
  const csp = accountPageCsp(accountPage);

  router.get("/account", (ctx) => {
    ctx.set("Content-Security-Policy", csp);
    ctx.type = "text/html; charset=utf-8";
    ctx.body = accountPage;
  });

  router.post("/api/auth/register", limits.middleware(5, byIp), async (ctx) => {
    if (config.registration === "closed") throw forbidden("Registration is closed");
    const fields = objectBody(ctx.request.body);
    const rawEmail = requiredString(fields, "email");
    const password = requiredString(fields, "password");
    const inviteCode = optionalString(fields, "inviteCode");
    if (config.registration === "invite") {
      if (inviteCode === undefined || !secretMatches(inviteCode, config.inviteCode ?? "")) {
        throw forbidden("A valid invite code is required");
      }
    }
    const email = normaliseEmail(rawEmail);
    if (email === null) throw badRequest("email is not a valid address");
    if (!isAcceptablePassword(password)) throw badRequest("password must be 10..72 bytes");
    try {
      const body: RegisterResponse = { userId: await accounts.register(email, password) };
      ctx.status = 201;
      ctx.body = body;
    } catch (err) {
      if (err instanceof EmailTakenError) throw new HttpError(409, "conflict", "An account with this email exists");
      throw err;
    }
  });

  router.post("/api/auth/login", limits.middleware(10, byIp), async (ctx) => {
    const fields = objectBody(ctx.request.body);
    const rawEmail = requiredString(fields, "email");
    const password = requiredString(fields, "password");
    const email = normaliseEmail(rawEmail);
    const key = `email:${email ?? rawEmail.trim().toLowerCase()}`;
    const wait = loginFailures.retryAfter(key, Date.now());
    if (wait > 0) throw rateLimited(wait);
    const session = await accounts.login(email, password);
    if (!session) {
      loginFailures.record(key, Date.now());
      throw unauthorized("Wrong email or password");
    }
    const body: LoginResponse = session;
    ctx.body = body;
  });

  router.post("/api/auth/logout", requireAuth(auth), requireSession(), (ctx) => {
    accounts.logout(authOf(ctx).tokenId);
    ctx.status = 204;
  });

  return router;
}
