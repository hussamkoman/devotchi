# TODO

## MVP (current)

- [ ] Deploy broker to a remote server (Railway/Render/Fly.io)
- [ ] Test with broker on remote URL instead of localhost
- [ ] Add LICENSE file (MIT)
- [ ] Add CONTRIBUTING.md
- [ ] ESP32 firmware — first prototype with OLED + 2 buttons

## Soon

- [ ] Simulator auto-reconnect when broker restarts
- [ ] Structured logging in broker (timestamps, JSON output)
- [ ] Rate limiting on HTTP hook endpoints
- [ ] Max connection limit on broker
- [ ] Cryptographically secure pairing code generation (`crypto.getRandomValues`)
- [ ] CLI tool: `devotchi pair` / `devotchi config` / `devotchi test`
- [ ] Setup script that generates settings.local.json from config.json automatically

## Later

- [ ] Authentication tokens (beyond pairing code)
- [ ] TLS/WSS support for broker
- [ ] Phone PWA as BLE bridge to ESP32
- [ ] Broker clustering / horizontal scaling
- [ ] `ask_question` tool — freeform text input from device
- [ ] Notification sounds/vibration patterns per event type
