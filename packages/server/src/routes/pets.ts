// Pets (protocol 14.8). Reads take a cli or a device token; uploads, deletes
// and device commands a cli token. `:id` and every pack are scoped to the
// caller's room, so another room's pack is 404.

import Router from "@koa/router";
import { PET_ID_RE, type DevicePetCommandResponse, type ListPetsResponse } from "@devotchi/shared";
import { authOf, requireAuth, requireCli, type Authenticator } from "../http/auth.js";
import { badRequest, notFound } from "../http/errors.js";
import { byToken, type RateLimits } from "../http/rate-limit.js";
import { parsePetCommand, type PetService } from "../pets/service.js";

export interface PetRouteDeps {
  auth: Authenticator;
  limits: RateLimits;
  pets: PetService;
}

export function petRoutes(deps: PetRouteDeps): Router {
  const { auth, limits, pets } = deps;
  const router = new Router();
  const read = [requireAuth(auth), limits.middleware(120, byToken)];
  const write = [requireAuth(auth), requireCli(), limits.middleware(30, byToken)];

  router.get("/api/pets", ...read, (ctx) => {
    const body: ListPetsResponse = { pets: pets.list(authOf(ctx).roomId) };
    ctx.body = body;
  });

  router.get("/api/pets/:id/pack", ...read, (ctx) => {
    const id = ctx.params.id;
    const pet = PET_ID_RE.test(id) ? pets.find(authOf(ctx).roomId, id) : undefined;
    if (!pet) throw notFound("No such pet");
    ctx.set("ETag", `"${pet.info.crc32}"`);
    ctx.set("Cache-Control", "no-cache");
    ctx.type = "application/octet-stream";
    ctx.body = Buffer.from(pet.bytes);
  });

  router.post("/api/pets", ...write, (ctx) => {
    const raw = ctx.request.rawBody;
    const json = ctx.request.body;
    if (raw === undefined && json === undefined) {
      throw badRequest("Send a pack (application/octet-stream) or its JSON source (application/json)");
    }
    const { pet, created } = pets.upload(authOf(ctx).roomId, raw !== undefined ? { pack: new Uint8Array(raw) } : { source: json });
    ctx.status = created ? 201 : 200;
    ctx.body = pet;
  });

  router.delete("/api/pets/:id", ...write, (ctx) => {
    pets.remove(authOf(ctx).roomId, ctx.params.id);
    ctx.status = 204;
  });

  router.post("/api/devices/:id/pet", ...write, (ctx) => {
    const cmd = parsePetCommand(ctx.request.body);
    const at = pets.command(authOf(ctx).roomId, ctx.params.id, cmd);
    const body: DevicePetCommandResponse = { sent: true, at: new Date(at).toISOString() };
    ctx.status = 202;
    ctx.body = body;
  });

  return router;
}
