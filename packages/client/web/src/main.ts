// Dashboard wiring: socket -> reducer -> views, plus the side effects the
// reducer must not do itself (holding frames, title, notifications, REST).

import {
  buildHolding,
  buildPermissionDefer,
  buildPermissionResponse,
  parseServerMessage,
  serialize,
  type Device,
  type PairConfirmResponse,
  type Room,
  type RoomSettings,
} from "@devotchi/shared";
import { api, sessionSecret } from "./api.js";
import { byId, h, setText } from "./dom.js";
import { HoldingDriver } from "./holding.js";
import { initialState, reduce, type Action, type Answer, type DashboardConfig, type State, type Toast } from "./store.js";
import { DevicesPanel } from "./views/devices.js";
import { updateHeader } from "./views/header.js";
import { updateLog } from "./views/log.js";
import { updatePet } from "./views/pet.js";
import { RequestList } from "./views/requests.js";
import { SettingsPanel } from "./views/settings.js";
import { WsClient } from "./ws-client.js";
import "./style.css";

const TITLE = "Devotchi";
const TICK_MS = 500;
const ROOM_REFRESH_MS = 10_000;

let state: State = initialState();
let unseen = 0;
let renderedToasts: readonly Toast[] | null = null;

const holding = new HoldingDriver({
  send: (ids) => void client.send(serialize(buildHolding(ids))),
  visible: () => document.visibilityState === "visible",
});

const client = new WsClient({
  url: () => {
    const scheme = location.protocol === "https:" ? "wss" : "ws";
    return `${scheme}://${location.host}/ws?s=${encodeURIComponent(sessionSecret())}`;
  },
  onFrame,
  onState: (link) => {
    if (link.phase !== "connecting") holding.reset();
    dispatch({ type: "connection", ...link, now: Date.now() });
  },
});

const requests = new RequestList(byId("request-list"), byId("requests-empty"), { answer });
const devices = new DevicesPanel(byId("device-list"), byId("devices-empty"), {
  rename: async (id, name) => {
    await api<Device>("PATCH", `/api/devices/${encodeURIComponent(id)}`, { name });
    await refreshRoom();
  },
  identify: async (id) => {
    await api("POST", `/api/devices/${encodeURIComponent(id)}/identify`, {});
  },
  unpair: async (id) => {
    await api("DELETE", `/api/devices/${encodeURIComponent(id)}`);
    await refreshRoom();
  },
});
const settings = new SettingsPanel({
  update: async (changes) => {
    dispatch({ type: "settings", settings: await api<RoomSettings>("PUT", "/api/room/settings", changes) });
  },
  enableNotifications: async () => {
    if (!("Notification" in window)) return;
    await Notification.requestPermission();
    render();
  },
});

function dispatch(action: Action): void {
  state = reduce(state, action);
  holding.sync(state.requests.map((r) => r.id));
  render();
}

function onFrame(raw: string): void {
  const msg = parseServerMessage(raw);
  if (msg === null) {
    console.debug("devotchi: ignored an invalid frame", raw.slice(0, 200));
    return;
  }
  const isNew = msg.type === "permission" && !state.requests.some((r) => r.id === msg.id);
  dispatch({ type: "frame", msg, now: Date.now() });
  if (msg.type === "permission" && isNew) attention(msg.tool, msg.summary, msg.id);
  if (msg.type === "register_ack" || msg.type === "peers") void refreshRoom();
}

function answer(id: string, choice: Answer): void {
  const frame = choice === "defer" ? buildPermissionDefer(id) : buildPermissionResponse(id, choice === "allow");
  if (client.send(serialize(frame))) dispatch({ type: "send", id, answer: choice, now: Date.now() });
  else dispatch({ type: "toast", text: "Not connected; the answer was not sent", now: Date.now() });
}

function attention(tool: string, summary: string, id: string): void {
  setText(byId("announcer"), `New ${tool} request: ${summary}`);
  if (document.visibilityState === "visible") return;
  unseen += 1;
  document.title = `(${unseen}) ${TITLE}`;
  if ("Notification" in window && Notification.permission === "granted") {
    new Notification(`${TITLE}: ${tool}`, { body: summary, tag: id });
  }
}

async function refreshRoom(): Promise<void> {
  try {
    dispatch({ type: "room", room: await api<Room>("GET", "/api/room"), now: Date.now() });
  } catch (err) {
    dispatch({ type: "toast", text: `Could not load the room: ${err instanceof Error ? err.message : String(err)}`, now: Date.now() });
  }
}

function notificationState(): string {
  if (!("Notification" in window)) return "not supported by this browser";
  if (Notification.permission === "granted") return "on";
  if (Notification.permission === "denied") return "blocked in the browser settings";
  return "off";
}

function render(): void {
  const now = Date.now();
  updateHeader(state, now);
  updatePet(state);
  requests.update(state, now);
  devices.update(state, now);
  settings.update(state, notificationState());
  updateLog(byId("log-list"), state.log);
  if (state.toasts !== renderedToasts) {
    renderedToasts = state.toasts;
    byId("toasts").replaceChildren(...state.toasts.map((t) => h("div", { class: "toast" }, t.text)));
  }
}

byId<HTMLFormElement>("pair-form").addEventListener("submit", (e) => {
  e.preventDefault();
  const input = byId<HTMLInputElement>("pair-code");
  const result = byId("pair-result");
  const code = input.value.trim().toUpperCase();
  if (code === "") return;
  setText(result, "Pairing…");
  result.classList.remove("error");
  api<PairConfirmResponse>("POST", "/api/pair/confirm", { code })
    .then((paired) => {
      setText(result, `Paired ${paired.name} (${paired.deviceId})`);
      input.value = "";
      return refreshRoom();
    })
    .catch((err: unknown) => {
      result.classList.add("error");
      setText(result, err instanceof Error ? err.message : String(err));
    });
});

document.addEventListener("keydown", (e) => {
  const target = e.target as HTMLElement | null;
  if (e.ctrlKey || e.metaKey || e.altKey || target?.closest("input, select, textarea") != null) return;
  if (requests.keydown(e)) e.preventDefault();
});
document.addEventListener("keyup", (e) => requests.keyup(e));
document.addEventListener("visibilitychange", () => {
  holding.visibilityChanged();
  if (document.visibilityState === "visible") {
    unseen = 0;
    document.title = TITLE;
  }
});

setInterval(() => dispatch({ type: "tick", now: Date.now() }), TICK_MS);
setInterval(() => void refreshRoom(), ROOM_REFRESH_MS);
api<DashboardConfig>("GET", "/api/config")
  .then((config) => dispatch({ type: "config", config }))
  .catch((err: unknown) => dispatch({ type: "toast", text: `Could not load the configuration: ${err instanceof Error ? err.message : String(err)}`, now: Date.now() }));
client.connect();
render();
