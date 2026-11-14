# Deploying the Devotchi server

The server (`packages/server`) is one Node.js process with a SQLite file. It
speaks plain HTTP and WebSocket on one port. There are two ways to run it:

- **LAN self-hosting**: the server on a machine in your network, devices and
  PCs connect with `ws://` and `http://`. Single-user mode.
- **Hosted**: the server behind a TLS-terminating reverse proxy on a public
  host name (`wss://`, `https://`), usually in multi-user mode. This
  repository ships a Dockerfile and a Docker Compose stack with Caddy for it.

The variables are listed in `docs/architecture.md` section 2.6; the REST and
WebSocket contract is `docs/protocol.md`.

## LAN self-hosting (plain ws)

Requirements: Node.js 24 (22.13 or newer works) and pnpm 10.

```bash
pnpm install
pnpm run build
DEVOTCHI_HOST=0.0.0.0 DEVOTCHI_PORT=8080 node packages/server/dist/index.js
```

- `DEVOTCHI_HOST` is the bind address. `0.0.0.0` (the default of the
  standalone server) accepts connections from the LAN; `127.0.0.1` keeps the
  server local to the machine.
- `DEVOTCHI_DB_PATH` defaults to `~/.devotchi/devotchi.db`.
- Single-user mode lets anyone who can reach the port mint an API token
  (`POST /api/auth/token`), and every token gets its own room. On a shared
  network set `DEVOTCHI_ADMIN_SECRET`: minting then requires the
  `X-Devotchi-Admin: <secret>` header, for example
  `curl -X POST -H 'X-Devotchi-Admin: <secret>' -H 'Content-Type: application/json' -d '{"label":"cli"}' http://<host>:8080/api/auth/token`.
- The server writes one JSON log line per event to stdout. It stops cleanly
  on SIGINT/SIGTERM: held permission requests get the neutral reply (the
  terminal prompt takes over), sockets close with 1001.
- Exit codes: 0 clean stop, 1 startup or runtime failure (for example the
  port is in use), 2 invalid configuration. Errors are one line on stderr.

`devotchi start --local` embeds the same server in the client process
(bound to `127.0.0.1`, or the LAN with `--lan`) for a one-machine setup.

To keep it running, use the init system of the machine, for example a
systemd user unit with `ExecStart=/usr/bin/node /path/to/devotchi/packages/server/dist/index.js`
and `Environment=DEVOTCHI_HOST=0.0.0.0`.

## Hosted: Docker Compose with Caddy

`docker-compose.yml` runs two services:

| Service | What it does |
|---|---|
| `server` | The image built from `Dockerfile`: `node:24-slim`, runs as the unprivileged `node` user, database at `/data/devotchi.db` on the `devotchi-data` volume, `HEALTHCHECK` on `/health`. Not published; only Caddy reaches it. |
| `caddy` | Terminates TLS for `DEVOTCHI_DOMAIN` with an automatically obtained and renewed certificate (Let's Encrypt or ZeroSSL), redirects HTTP to HTTPS and proxies HTTP and WebSocket to `server:8080`. Certificates live on the `caddy-data` volume. |

Steps:

1. Point a DNS record for the host name at the machine. Ports 80 and 443
   must be reachable from the internet (the certificate challenge uses them).
2. Create `.env` next to `docker-compose.yml`:

   ```ini
   DEVOTCHI_DOMAIN=devotchi.example.com
   DEVOTCHI_MODE=multi-user
   DEVOTCHI_REGISTRATION=invite
   DEVOTCHI_INVITE_CODE=<a long random string>
   ```

3. `docker compose up -d --build`, then `docker compose logs -f server`.
   The server logs `listening`; `https://devotchi.example.com/health`
   answers `{"status":"ok",...,"proto":2,"mode":"multi-user"}`.

The compose file sets `DEVOTCHI_TRUST_PROXY=1`, so rate limits are keyed by
the client address Caddy puts in `X-Forwarded-For`. Never set it when the
server is reachable without the proxy: clients could then choose their own
address.

The image alone works without Compose behind any other proxy:

```bash
docker build -t devotchi-server .
docker run -d --name devotchi -p 127.0.0.1:8080:8080 -v devotchi-data:/data \
  -e DEVOTCHI_TRUST_PROXY=1 devotchi-server
```

The proxy must pass WebSocket upgrades on `/ws` and must not time out a
request in under 5 minutes: permission hooks are held open until the device
answers (up to the room's hold time, at most 300 s).

## Multi-user settings

| Variable | Values |
|---|---|
| `DEVOTCHI_MODE` | `multi-user` turns accounts on. Anonymous token minting is closed; every user owns one room. |
| `DEVOTCHI_REGISTRATION` | `open` (anyone can register), `invite` (registration requires `DEVOTCHI_INVITE_CODE`), `closed` (no new accounts). |
| `DEVOTCHI_INVITE_CODE` | Required with `invite`; the server refuses to start without it (exit code 2). |

Users register and log in at `https://<domain>/account`. The page also lists
and revokes API tokens and devices, creates a token for the CLI (shown once)
and confirms a device's pairing code. From the command line, `devotchi login`
performs the same login and stores a token for the server (architecture 2.5). Login sessions last 30 days and end
with logout. Passwords are stored as bcrypt hashes (cost 12); login is limited
per IP and per email address and takes the same time for unknown addresses.

To close registration after creating your own account, set
`DEVOTCHI_REGISTRATION=closed` and run `docker compose up -d` again.

## Pointing a device at the server

The device stores one base URL without a path (`ws://host:port` or
`wss://host`, protocol section 10). Set it

- from the firmware console over USB: `server ws://192.168.1.20:8080`, or
  `server wss://devotchi.example.com`;
- or during BLE WiFi provisioning, through the custom endpoint `custom-data`
  with the payload `server=<url>`.

Then pair it: the device shows a code, and `devotchi pair <CODE>` (or the
account page, or the dashboard) confirms it into your room.

The firmware verifies `wss://` and `https://` against the ESP-IDF certificate
bundle, i.e. against public certificate authorities. A hosted server
therefore needs a publicly trusted certificate, which Caddy obtains. A
self-signed or private-CA certificate only works with firmware built with
that CA embedded (`CONFIG_DEVOTCHI_TLS_EXTRA_CA`). On a LAN, use plain
`ws://`.

## Backups

The database is one SQLite file in WAL mode (`devotchi.db` plus
`devotchi.db-wal` and `devotchi.db-shm` while the server runs). Either

- take a consistent copy while the server runs:

  ```bash
  sqlite3 /path/to/devotchi.db "VACUUM INTO '/backups/devotchi-$(date +%F).db'"
  ```

  The `server` image has no `sqlite3` binary, but Node's built-in SQLite
  module does the same inside the running container:

  ```bash
  docker compose exec server node -e "new (require('node:sqlite').DatabaseSync)('/data/devotchi.db').exec(\"VACUUM INTO '/data/backup.db'\")"
  docker compose cp server:/data/backup.db ./devotchi-$(date +%F).db
  docker compose exec server rm /data/backup.db
  ```

  (Node prints an `ExperimentalWarning` for `node:sqlite`; it is harmless.)
  `VACUUM INTO` refuses to overwrite an existing file, hence the `rm`;
- or stop the server and copy the file: `docker compose stop server`,
  `docker compose cp server:/data/devotchi.db ./devotchi.db` (works on the
  stopped container; the `-wal` file is folded into the database on a clean
  stop), then `docker compose start server`.

To restore, stop the server and put the copy in place as `devotchi.db`
(remove stale `-wal` and `-shm` files next to it).

The first time the server opens a database written by the old sql.js server
(protocol v1), it copies the file to `<file>.v0.bak` next to it before
migrating, and logs the path. That copy is never deleted or overwritten;
remove it yourself once the migrated server works.

## Upgrading

1. Back up the database (above).
2. LAN: `git pull`, `pnpm install`, `pnpm run build`, restart the process.
   Compose: `git pull`, then `docker compose up -d --build`.
3. Migrations run automatically at startup inside one transaction. A
   database from a newer server version is refused with a one-line error
   instead of being modified; downgrade by restoring the backup.

Restarting forgets in-memory state only: pending permission requests (their
hooks get the neutral reply first), room status and mood, the
`dialogWhilePending` counters, and device pairings that were confirmed but
not yet fetched by the device (the device gets `410` and shows a new code).
Paired devices reconnect on their own.
