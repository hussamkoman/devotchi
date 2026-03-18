import {
  type DevotchiMessage,
  type PetMood,
  type StatusMood,
  isValidPairingCode,
} from "@devotchi/shared";
import "./style.css";

// --- DOM Elements ---

const statusDot = document.getElementById("status-dot")!;
const pairingInput = document.getElementById("pairing-input") as HTMLInputElement;
const connectBtn = document.getElementById("connect-btn")!;
const petCharacter = document.getElementById("pet-character")!;
const petMoodEl = document.getElementById("pet-mood")!;
const statusText = document.getElementById("status-text")!;
const modal = document.getElementById("modal")!;
const modalType = document.getElementById("modal-type")!;
const modalDesc = document.getElementById("modal-desc")!;
const modalDetails = document.getElementById("modal-details")!;
const modalTimer = document.getElementById("modal-timer")!;
const modalApprove = document.getElementById("modal-approve")!;
const modalReject = document.getElementById("modal-reject")!;
const logList = document.getElementById("log-list")!;

// --- State ---

let ws: WebSocket | null = null;
let pendingRequestId: string | null = null;
let pendingRequestType: "approval" | "permission" | null = null;
let countdownInterval: ReturnType<typeof setInterval> | null = null;

// --- Mood Emoji Map ---

const MOOD_EMOJI: Record<string, string> = {
  happy: "😊",
  sad: "😢",
  excited: "🤩",
  anxious: "😰",
  sleeping: "😴",
  angry: "😡",
  proud: "😎",
  idle: "🫥",
  focused: "🔍",
  working: "⚡",
  thinking: "🤔",
};

// --- Logging ---

function escapeHtml(str: string): string {
  const div = document.createElement("div");
  div.textContent = str;
  return div.innerHTML;
}

function addLog(type: string, message: string): void {
  const li = document.createElement("li");
  const now = new Date().toLocaleTimeString();
  li.innerHTML = `<span class="log-time">${escapeHtml(now)}</span><span class="log-type">[${escapeHtml(type)}]</span>${escapeHtml(message)}`;
  logList.prepend(li);

  // Keep last 10
  while (logList.children.length > 10) {
    logList.removeChild(logList.lastChild!);
  }
}

// --- Pet Display ---

function updatePet(mood: PetMood | StatusMood): void {
  petCharacter.textContent = MOOD_EMOJI[mood] || "🫥";
  petMoodEl.textContent = mood;

  // Bounce animation
  petCharacter.classList.remove("bounce");
  void petCharacter.offsetWidth; // force reflow
  petCharacter.classList.add("bounce");
}

// --- Modal ---

function showModal(
  type: "approval" | "permission",
  id: string,
  desc: string,
  details: string | undefined,
  timeout: number
): void {
  pendingRequestId = id;
  pendingRequestType = type;

  modalType.textContent = type === "approval" ? "APPROVAL REQUEST" : "PERMISSION REQUEST";
  modalDesc.textContent = desc;
  modalDetails.textContent = details || "";
  modalApprove.textContent = type === "approval" ? "Approve" : "Grant";

  modal.classList.remove("hidden");
  modal.classList.add("flash");
  setTimeout(() => modal.classList.remove("flash"), 300);

  // Countdown
  let remaining = Math.ceil(timeout / 1000);
  modalTimer.textContent = formatSeconds(remaining);

  if (countdownInterval) clearInterval(countdownInterval);
  countdownInterval = setInterval(() => {
    remaining--;
    modalTimer.textContent = formatSeconds(remaining);
    if (remaining <= 0) {
      hideModal();
    }
  }, 1000);
}

function hideModal(): void {
  modal.classList.add("hidden");
  pendingRequestId = null;
  pendingRequestType = null;
  if (countdownInterval) {
    clearInterval(countdownInterval);
    countdownInterval = null;
  }
}

function formatSeconds(s: number): string {
  const m = Math.floor(s / 60);
  const sec = s % 60;
  return `${m}:${sec.toString().padStart(2, "0")}`;
}

function sendResponse(approved: boolean): void {
  if (!ws || !pendingRequestId || !pendingRequestType) return;

  const msg: DevotchiMessage =
    pendingRequestType === "approval"
      ? {
          type: "approval_response",
          id: pendingRequestId,
          approved,
          timestamp: new Date().toISOString(),
        }
      : {
          type: "permission_response",
          id: pendingRequestId,
          granted: approved,
          timestamp: new Date().toISOString(),
        };

  try {
    ws.send(JSON.stringify(msg));
  } catch {
    addLog("error", "Failed to send response");
  }
  addLog("response", `${pendingRequestType} ${approved ? "approved" : "rejected"}`);
  hideModal();
}

// --- WebSocket ---

function connect(pairingCode: string): void {
  if (ws) {
    ws.close();
  }

  const brokerUrl = `ws://${window.location.hostname}:8080`;
  ws = new WebSocket(brokerUrl);

  ws.onopen = () => {
    statusDot.className = "dot connected";
    addLog("system", "Connected to broker");

    ws!.send(
      JSON.stringify({
        type: "register",
        role: "consumer",
        pairingCode: pairingCode.toUpperCase(),
      })
    );
  };

  ws.onmessage = (event) => {
    let msg: DevotchiMessage;
    try {
      msg = JSON.parse(event.data);
    } catch {
      return;
    }

    handleMessage(msg);
  };

  ws.onclose = () => {
    statusDot.className = "dot disconnected";
    addLog("system", "Disconnected");
  };

  ws.onerror = () => {
    statusDot.className = "dot disconnected";
    addLog("system", "Connection error");
  };
}

function handleMessage(msg: DevotchiMessage): void {
  switch (msg.type) {
    case "ping":
      try { ws?.send(JSON.stringify({ type: "pong" })); } catch {}
      break;

    case "register_ack":
      addLog("system", `Registered as ${msg.role}, ${msg.connectedPeers} peer(s)`);
      statusText.textContent = "Connected — waiting for agent...";
      break;

    case "status":
      // If a status update arrives while a modal is open, the permission
      // was already handled via CLI — the tool ran, so dismiss the modal
      if (pendingRequestId) {
        hideModal();
      }
      statusText.textContent = msg.msg;
      if (msg.mood) updatePet(msg.mood);
      addLog("status", msg.msg);
      break;

    case "approval":
      updatePet("anxious");
      showModal("approval", msg.id, msg.desc, msg.details, msg.timeout);
      addLog("approval", msg.desc);
      break;

    case "permission":
      updatePet("thinking");
      showModal("permission", msg.id, msg.action, msg.details, msg.timeout);
      addLog("permission", msg.action);
      break;

    case "notify":
      updatePet(msg.mood);
      statusText.textContent = msg.detail || msg.event;
      addLog("notify", `${msg.event}${msg.detail ? ": " + msg.detail : ""}`);
      break;

    case "mood":
      updatePet(msg.mood);
      if (msg.reason) statusText.textContent = msg.reason;
      addLog("mood", `${msg.mood}${msg.reason ? " — " + msg.reason : ""}`);
      break;

    case "dismiss":
      if (pendingRequestId === msg.id) {
        hideModal();
        updatePet("idle");
        addLog("system", "Request handled elsewhere");
      }
      break;

    case "error":
      addLog("error", `${msg.message} (${msg.code})`);
      break;
  }
}

// --- Event Listeners ---

connectBtn.addEventListener("click", () => {
  const code = pairingInput.value.trim().toUpperCase();
  if (isValidPairingCode(code)) {
    connect(code);
  } else {
    addLog("error", "Invalid pairing code");
  }
});

pairingInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") {
    connectBtn.click();
  }
});

modalApprove.addEventListener("click", () => sendResponse(true));
modalReject.addEventListener("click", () => sendResponse(false));
