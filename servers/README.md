# servers/: the configuration of the machines we run; today only walnut.nanobyte.dev (its containers, nginx, the upload and the walnut-watch publisher).
`make walnut-server` puts it in place, `make walnut-server-test` tests the publisher; the site's content is in website/.
Touch it when the server side changes (docs/WALNUT-SERVER.md).

## walnut-rate's receiver (built, not deployed)

`walnut.nanobyte.dev/rate/` holds the receiver for testers' sessions (`walnut-rate submit`, PLAN 5.12):
`walnut-rate-receiver` (stdlib Python: one POST of one bundle, the format check, a per-address limit, the
quarantine), `walnut-rate-receiver.service` (DynamicUser, ProtectSystem=strict, only the quarantine writable, only
the proxy's networks may connect) and `nginx-location.conf` (the two locations for walnut.nanobyte.dev.conf).
`deploy.sh` doesn't install any of it yet, and walnut.nanobyte.dev.conf doesn't route `/rate/`, so `make
walnut-server` leaves the server as it was. Provisional: the user hasn't settled the transport (our receiver or
GitHub). The tests are in the walnut-rate suite (`make test ONLY=walnut-rate`). To put it live, on the server as
the deploy login (it has sudo):

1. The group and the quarantine; the login that pulls joins the group (a new login session picks it up):
   `sudo groupadd --system walnut-rate && sudo usermod -aG walnut-rate $USER &&
   sudo install -d -o root -g walnut-rate -m 2770 /data/nanobyte/Walnut/submissions`
2. The program, from the PC: `rsync -t --chmod=755 servers/walnut.nanobyte.dev/rate/walnut-rate-receiver
   HOST:/data/nanobyte/Walnut/bin/`
3. The address the main proxy reaches it on, mainproxy_net's gateway:
   `docker network inspect mainproxy_net -f '{{(index .IPAM.Config 0).Gateway}}'` (say 172.18.0.1), then
   `echo WALNUT_RATE_LISTEN=172.18.0.1:8096 > /data/nanobyte/Walnut/state/walnut-rate-receiver.env`. The unit
   lets in 172.16.0.0/12 and localhost only (IPAddressAllow): check the network's subnet is inside it, and that
   the host's firewall lets the proxy container reach that port.
4. The unit: copy `walnut-rate-receiver.service` to the server, `sudo install -m 644 walnut-rate-receiver.service
   /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl enable --now walnut-rate-receiver`;
   then `curl -s http://172.18.0.1:8096/rate/health` (ok) on the host and
   `docker exec main-proxy wget -qO- http://172.18.0.1:8096/rate/health` from the proxy;
   `systemd-analyze security walnut-rate-receiver` for the sandbox.
5. nginx: the two locations of `nginx-location.conf` into walnut.nanobyte.dev.conf's 443 server block, above
   `location /`, with the address filled in; commit; `make walnut-server` (nginx -t, the old config back if it
   fails); `curl -s https://walnut.nanobyte.dev/rate/health`.
6. A first submission from the PS3 (`walnut-rate submit`: the URL is already the default) and
   `tools/gamedb/submissions.py pull` on the PC (it reads WALNUT_HOST from config.local.mk).
7. Then the site's "How to help" page (website/gamedb/content/help.md) loses `draft = true` and gets a menu
   link, and hugo.toml's `ratingDocs` points at it; deploy.sh learns steps 1 to 4 (like walnut-watch's units).
