# Hosting the co-op site

One container runs the game page and the co-op relay; Caddy in front of it
gets an HTTPS certificate by itself. New builds are published to
`ghcr.io/reecen9696/galaxslop:latest` on every push to `main`. No game data
is in the image: players import their own disc in the page.

## You need

- A Linux machine reachable from the internet (any small VPS: 1 vCPU, 1 GB
  RAM is plenty) with **ports 80 and 443 open** (also forwarded on the router
  if it is at home).
- Docker with the compose plugin: `curl -fsSL https://get.docker.com | sh`
- A hostname pointing at the machine's public IP: an A record such as
  `smg.example.com`, or without a domain, `<ip-with-dashes>.sslip.io` (e.g.
  `203-0-113-7.sslip.io` for 203.0.113.7), which needs no DNS setup.

## Set up (once)

```
mkdir smg && cd smg
curl -fsSLO https://raw.githubusercontent.com/reecen9696/galaxslop/main/deploy/compose.yml
echo "DOMAIN=smg.example.com" > .env
docker compose up -d
```

Open `https://smg.example.com/`. The certificate takes a few seconds the
first time; `docker compose logs caddy` shows it.

## Update to the latest build

```
cd smg && docker compose pull && docker compose up -d
```

## Optional

- `ROOMS=PLAY22` in `.env` sets the permanent room code(s) everyone can
  join (comma-separated; default `PLAY22`).
- Health check: `https://<DOMAIN>/healthz` answers `ok`.
- Logs: `docker compose logs -f smg`.
