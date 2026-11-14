# Ideas & Notes

Longer-form ideas. The approved/deferred backlog with one line per item is in [TODO.md](TODO.md).

## Desktop Devotchi

A larger touchscreen version that sits on your desk. Unlike the portable pocket device (2.0" TFT LCD, 3 buttons), this would have a 3.5"–5" touchscreen with a UI similar to the web dashboard.

**Multi-session management:** The killer feature. Power users run multiple Claude Code sessions across different projects. The desktop device shows all active sessions at once — each project gets its own pet with mood and status. Tap a project to see details, approve/reject pending requests.

The server already supports multiple consumers per room and multiple rooms simultaneously. The desktop device could connect as a consumer to multiple rooms at once, or we could add a "multi-room consumer" mode.

The web dashboard is essentially a prototype of this UI — start from that codebase.

**Hardware ideas:**
- ESP32-S3 with a 3.5" or 5" capacitive touchscreen (ILI9488 or similar)
- Powered via USB-C (desk device, no battery needed)
- Optional: small speaker for notification sounds
- Could run LVGL like the pocket device, or a web view of the dashboard on a built-in display

---

## Session Awareness

Requests and status lines carry a short session id and the project name. Git branch detection (SessionStart hook) and a session registry are in the backlog in `TODO.md`.

---

## Approval Queue

The device and the dashboard already keep a queue of pending requests (up to 4 on the device, 8 per room). Batch approval or prioritising stays an idea; Devotchi will never approve anything without a press.

---

## Mood History / Activity Graph

Track mood changes over time and show a small graph on the device — like a heartbeat monitor for your project. Green = tests passing, red = failures. Could be a fun visual for the desktop version.

---

## Community Pet Skins

Let users customize the Tamagotchi character. The current emoji-based display is functional but pixel art sprites would be much more charming. Could have community-contributed sprite sets.

---

## Webhook Integration

Besides Claude Code hooks, support generic webhooks from CI/CD (GitHub Actions, etc.) so the device shows build/deploy status even when no agent is running. Scripts can already use `POST /api/notify` or `devotchi notify`; a GitHub webhook adapter is in the backlog in `TODO.md`.
