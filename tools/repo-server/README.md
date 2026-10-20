# Repository server

`docker-compose.yml` and `nginx.conf` run on the LAN's repository server in `REPO_DIR`
(`config.local.mk`; `docker compose up -d`), serving `public/` at `REPO_URL`, port 8070 on the
address in `.env` (`REPO_BIND_IP=<the server's LAN address>`, not tracked):

- `ps3/`: the official repository (distribution `ps3`, components `main` and `deps`, frozen
  `img-<stamp>` snapshots). Add-only: nothing published here is ever removed or changed.
- `ps3-licensed/`: licensed game content we may not redistribute (distribution `ps3-licensed`,
  component `main`: classicube-data, quake-shareware, quake2-demo-data; `img-<stamp>` snapshots
  next to the ones in `ps3/`). Add-only like `ps3/`, its own pool. Served like the others for now;
  being its own directory, it can get an access restriction in `nginx.conf` later.
- `ps3-dev/`: test builds (`make deploy DIST=dev`), throwaway.
- `ps3-dbgsym/`: debug symbols of recent builds, throwaway.
- `aptly-db/` (next to `public/`, not served): backup of `build/aptly/db` after each publish.

`tools/repo-publish.sh` publishes with aptly on the dev PC and syncs here with rsync over SSH.
