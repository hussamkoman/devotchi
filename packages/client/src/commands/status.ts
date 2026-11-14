// `devotchi status`: is the server reachable, is the token valid, and what
// do the room and its devices look like right now.

import type { HealthResponse, MeResponse, Room } from "@devotchi/shared";
import { ApiClient } from "../api.js";
import { connectServer, printJson, type Io } from "../context.js";
import { deviceLine, deviceLiveness, lastRequestLine } from "../format.js";
import { ConfigStore } from "../config.js";

export interface StatusOptions {
  server?: string;
  json?: boolean;
}

export async function runStatus(options: StatusOptions, io: Io): Promise<number> {
  const { url } = await new ConfigStore(io.env).resolveServer(options.server);
  const health: HealthResponse = await new ApiClient(url).health();
  const ctx = await connectServer(url, io);
  const me: MeResponse = await ctx.api.me();
  const room: Room = await ctx.api.room();
  const now = Date.now();

  if (options.json === true) {
    printJson(io, {
      server: { url, version: health.version, proto: health.proto, mode: health.mode, uptimeS: health.uptimeS },
      token: { valid: true, kind: me.kind, roomId: me.roomId, source: ctx.creds.source },
      peers: room.peers,
      room: { mode: room.settings.mode, holdMs: room.settings.holdMs, shareText: room.settings.shareText },
      pending: room.pending.length,
      devices: room.devices.map((device) => deviceLiveness(device, now)),
      lastRequest: room.lastRequest,
      diagnostics: room.diagnostics,
    });
    return 0;
  }

  io.out(`Server:   ${url} (version ${health.version}, protocol ${health.proto}, ${health.mode})`);
  io.out(`Token:    valid, room ${me.roomId}${me.kind === "session" ? ` (${me.email})` : ""}`);
  io.out(
    `Peers:    ${room.peers.consumers} consumer(s) incl. ${room.peers.devices} device(s), ${room.peers.producers} producer(s)`,
  );
  io.out(`Mode:     ${room.settings.mode}, hold time ${room.settings.holdMs / 1000} s`);
  io.out(`Pending:  ${room.pending.length} request(s)`);
  io.out(`Last request: ${lastRequestLine(room.lastRequest, now)}`);
  if (room.devices.length === 0) io.out("Devices:  none paired (devotchi pair <CODE>)");
  else {
    io.out("Devices:");
    for (const device of room.devices) io.out(`  ${deviceLine(device, now)}`);
  }
  return 0;
}
