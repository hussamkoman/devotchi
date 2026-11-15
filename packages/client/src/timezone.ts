// Protocol 14.9: the CLI gives a room the PC's time zone while the room still
// has none (""), so a device shows local time without any setup. A zone the
// person chose (dashboard, another PC) is never overwritten.

import { canonicalTimeZone, processTimeZone } from "@devotchi/shared";
import type { ApiClient } from "./api.js";

/** Sets the room's time zone to `zone` when it is still ""; returns the zone it set, or null. */
export async function adoptTimeZone(api: Pick<ApiClient, "room" | "updateRoomSettings">, zone: string = processTimeZone()): Promise<string | null> {
  const canonical = canonicalTimeZone(zone);
  if (canonical === null) return null;
  const room = await api.room();
  // Servers without phase 2 have no timeZone setting at all.
  if (room.settings.timeZone !== "") return null;
  const settings = await api.updateRoomSettings({ timeZone: canonical });
  return settings.timeZone === canonical ? canonical : null;
}
