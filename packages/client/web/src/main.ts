// Dashboard wiring: socket -> reducer -> views, plus the side effects the
// reducer must not do itself (holding frames, title, notifications, REST).

import {
  buildDecide,
  buildExtend,
  buildHolding,
  buildPermissionDefer,
  buildPermissionResponse,
  buildQuestionResponse,
  parseServerMessage,
  serialize,
  type ConsumerToServerMessage,
  type Device,
  type PairConfirmResponse,
  type Room,
  type RoomRequestDetail,
  type RoomSettings,
} from "@devotchi/shared";
import { api, sessionSecret } from "./api.js";
import { byId, h, setText } from "./dom.js";
import { HoldingDriver } from "./holding.js";
import {
  extendDue,
  initialState,
  isPresentable,
  needsFullText,
  reduce,
  type Action,
  type DashboardConfig,
  type Reply,
  type RequestCard,
  type State,
  type Toast,
} from "./store.js";
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

const requests = new RequestList(byId("request-list"), byId("requests-empty"), { reply, touch });
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
  browserTimeZone,
  enableNotifications: async () => {
    if (!("Notification" in window)) return;
    await Notification.requestPermission();
    render();
  },
});

function dispatch(action: Action): void {
  const before = new Set(state.requests.filter(isPresentable).map((r) => r.id));
  state = reduce(state, action);
  const shown = state.requests.filter(isPresentable);
  holding.sync(shown.map((r) => r.id));
  for (const card of state.requests) if (needsFullText(card) && card.full.state === "idle") loadFullText(card.id);
  render();
  for (const card of shown) if (!before.has(card.id) && action.type === "frame") attention(card);
  if (action.type === "room" || action.type === "settings") adoptBrowserTimeZone();
}

function onFrame(raw: string): void {
  const msg = parseServerMessage(raw);
  if (msg === null) {
    console.debug("devotchi: ignored an invalid frame", raw.slice(0, 200));
    return;
  }
  dispatch({ type: "frame", msg, now: Date.now() });
  if (msg.type === "register_ack" || msg.type === "peers") void refreshRoom();
}

function wire(id: string, choice: Reply): ConsumerToServerMessage {
  switch (choice.kind) {
    case "allow":
    case "deny":
      return buildPermissionResponse(id, choice.kind === "allow");
    case "defer":
      return buildPermissionDefer(id);
    case "decide":
      return buildDecide(id, choice.choice);
    case "answers":
      return buildQuestionResponse(id, choice.answers);
  }
}

function reply(id: string, choice: Reply): void {
  if (client.send(serialize(wire(id, choice)))) dispatch({ type: "send", id, reply: choice, now: Date.now() });
  else dispatch({ type: "toast", text: "Not connected; the answer was not sent", now: Date.now() });
}

/** Protocol 14.6: a person working on a question or plan keeps it alive. */
function touch(id: string): void {
  const card = state.requests.find((r) => r.id === id);
  const now = Date.now();
  if (card === undefined || !extendDue(card, now)) return;
  if (client.send(serialize(buildExtend(id)))) dispatch({ type: "extend", id, now });
}

function loadFullText(id: string): void {
  state = reduce(state, { type: "detail", id, full: { state: "loading" } });
  api<RoomRequestDetail>("GET", `/api/room/requests/${encodeURIComponent(id)}`)
    .then((detail) => dispatch({ type: "detail", id, full: { state: "loaded", toolInput: detail.toolInput } }))
    .catch(() => dispatch({ type: "detail", id, full: { state: "failed" } }));
}

function describe(card: RequestCard): string {
  switch (card.kind) {
    case "permission":
      return card.cmd ?? card.summary;
    case "question":
      return card.questions[0]?.text ?? "";
    case "plan":
      return card.title;
  }
}

function attention(card: RequestCard): void {
  const what = describe(card);
  setText(byId("announcer"), `New ${card.label} request: ${what}`);
  if (document.visibilityState === "visible") return;
  unseen += 1;
  document.title = `(${unseen}) ${TITLE}`;
  if ("Notification" in window && Notification.permission === "granted") {
    new Notification(`${TITLE}: ${card.label}`, { body: what, tag: card.id });
  }
}

function browserTimeZone(): string {
  return new Intl.DateTimeFormat().resolvedOptions().timeZone;
}

let zoneAdopted = false;

/** Protocol 14.9: a room without a time zone takes this browser's, once per page load. */
function adoptBrowserTimeZone(): void {
  if (zoneAdopted || state.settings?.timeZone !== "") return;
  zoneAdopted = true;
  const timeZone = browserTimeZone();
  if (timeZone === "") return;
  api<RoomSettings>("PUT", "/api/room/settings", { timeZone })
    .then((settings) => dispatch({ type: "settings", settings }))
    .catch(() => undefined);
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
