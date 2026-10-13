# server: the co-op relay

Rooms, PlayerState relay and (P3-01) the shared progress log, as specified
in `docs/NETCODE.md`. It never simulates the game: each client runs its own
world and the relay only forwards. Node + `ws`, TypeScript run directly by
Node's type stripping (no build step).

```
cd server
npm install
npm run dev          # ws://localhost:8787  (PORT=... to change)
npm test             # vitest: rooms, relay, errors, ping, 5 clients x 30 Hz soak
npm run typecheck
```

Environment: `PORT` (default 8787), `ALLOWED_ORIGINS` (comma-separated
origins for the browser build; unset accepts any, for local play), `ROOMS`
(comma-separated permanent room codes, e.g. `ROOMS=PLAY22`, so everyone can
join a known room without passing a code around).

- `src/protocol.ts`: the wire format the relay reads and writes.
- `src/relay.ts`: rooms (6-character codes without 0/O/1/I/L, max 5
  players), relaying, ping, the 15 s silence drop, 60 messages/s limit.
  Transport-independent.
- `src/main.ts`: the WebSocket server.

Deployment is P3-05 (a container on a cheap host, D-010).

## Running the published image

Every push to `main` builds the wasm, bundles the page and pushes
`ghcr.io/reecen9696/galaxslop:latest` (amd64 + arm64,
`.github/workflows/docker.yml`).

With HTTPS (what players need): `deploy/compose.yml` runs the image behind
Caddy; setup and updates are in `deploy/README.md`. Without the compose file, `docker run -p 8787:8787
ghcr.io/reecen9696/galaxslop:latest` serves plain HTTP for localhost only.
