# Repository server

`docker-compose.yml` and `nginx.conf` run on tibi-server in `/data/fast/NewServer/Ps3Repo/`
(`docker compose up -d`), serving `public/` at `http://10.0.0.10:8070/`:

- `ps3/`: the official repository (distribution `ps3`, components `main` and `deps`, frozen
  `img-<stamp>` snapshots). Add-only: nothing published here is ever removed or changed.
- `ps3-dev/`: test builds (`make deploy DIST=dev`), throwaway.
- `ps3-dbgsym/`: debug symbols of recent builds, throwaway.
- `aptly-db/` (next to `public/`, not served): backup of `build/aptly/db` after each publish.

`tools/repo-publish.sh` publishes with aptly on the dev PC and syncs here with rsync over SSH.
