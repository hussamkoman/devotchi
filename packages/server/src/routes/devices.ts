// Device registry routes (protocol 7.5). `:id` always refers to the caller's
// room, so another room's device is 404.

import Router from "@koa/router";
import {
  DEFAULT_IDENTIFY_MS,
  IDENTIFY_MAX_MS,
  IDENTIFY_MIN_MS,
  type IdentifyDeviceResponse,
  type ListDevicesResponse,
} from "@devotchi/shared";
import type { DeviceService } from "../devices/service.js";
import { authOf, requireAuth, requireCli, type Authenticator } from "../http/auth.js";
import { HttpError, badRequest, notFound } from "../http/errors.js";
import { objectBody, parseDeviceName } from "../http/fields.js";

export interface DeviceRouteDeps {
  auth: Authenticator;
  devices: DeviceService;
}

const noDevice = () => notFound("No such device");

export function deviceRoutes(deps: DeviceRouteDeps): Router {
  const { auth, devices } = deps;
  const router = new Router();
  const cli = [requireAuth(auth), requireCli()];

  router.get("/api/devices", ...cli, (ctx) => {
    const body: ListDevicesResponse = { devices: devices.list(authOf(ctx).roomId) };
    ctx.body = body;
  });

  router.patch("/api/devices/:id", ...cli, (ctx) => {
    const name = parseDeviceName(objectBody(ctx.request.body).name);
    const device = devices.rename(authOf(ctx).roomId, ctx.params.id, name);
    if (!device) throw noDevice();
    ctx.body = device;
  });

  router.delete("/api/devices/:id", ...cli, (ctx) => {
    if (!devices.remove(authOf(ctx).roomId, ctx.params.id)) throw noDevice();
    ctx.status = 204;
  });

  router.post("/api/devices/:id/identify", ...cli, (ctx) => {
    const given = objectBody(ctx.request.body, true).durationMs;
    const durationMs = given === undefined ? DEFAULT_IDENTIFY_MS : given;
    if (typeof durationMs !== "number" || !Number.isInteger(durationMs) || durationMs < IDENTIFY_MIN_MS || durationMs > IDENTIFY_MAX_MS) {
      throw badRequest(`durationMs must be an integer in ${IDENTIFY_MIN_MS}..${IDENTIFY_MAX_MS}`);
    }
    const result = devices.identify(authOf(ctx).roomId, ctx.params.id, durationMs);
    if (result === "unknown") throw noDevice();
    if (result === "offline") throw new HttpError(409, "conflict", "Device is offline");
    const body: IdentifyDeviceResponse = { sent: true };
    ctx.status = 202;
    ctx.body = body;
  });

  return router;
}
