# walnut.nanobyte.dev: the server side

walnut.nanobyte.dev runs on the OVH box in `/data/nanobyte/Walnut`, behind the main proxy (TLS with the `*.nanobyte.dev` certificate). Everything is in `servers/walnut.nanobyte.dev/` and goes in place with `make walnut-server` (`deploy.sh`, idempotent). The plan around it is docs/release/ALPHA.md M4 and M6.

## The containers

| Container | Serves | Notes |
|---|---|---|
| `walnut-repo` | `/apt/`, `/download/`, `/licensed/` (login), `/status/`, `/debian-snapshot/` | plain nginx over `www/`, read-only; a site update never restarts it |
| `walnut-site` | everything else: the welcome page | image `walnut-site:<id>`, built and switched by walnut-watch |
| `walnut-upload` | port 2223 | sshd + rrsync, write-only into `incoming/`, keys in `deploy-keys.pub`, fail2ban jail `walnut-upload` |
| `walnut-aptcache` | behind `/debian-snapshot/` | apt-cacher-ng for snapshot.debian.org, the pinned dates only |
| `walnut-ccache` | `/ccache/` (login) | ccache's remote storage, trimmed daily to 25 GB |

`walnut-watch` (host, systemd path unit + timer) publishes uploads; `walnut-ccache-trim` (daily timer) trims the ccache store.

Not deployed yet: `walnut-rate-receiver` (host, systemd, `rate/`), the receiver of testers' played sessions at
`/rate/submit` into `/data/nanobyte/Walnut/submissions`; servers/README.md has what it is and the steps to put it live.

## Release images

`.github/workflows/release-image.yml` ("Walnut release", by hand) builds the public image from one suite:

| Channel | Id | Signed by | Lands in | Kept |
|---|---|---|---|---|
| stable | `A.B.C`: A.B from the top-level `VERSION` file (by hand), C one past the newest published `A.B.*`, back to 0 when A.B changes (`github-ci/release-id.sh`) | the archive key (environment walnut-stable) | `/download/A.B.C/`, the GitHub release `vA.B.C` | forever; `/download/latest` is the highest A.B.C |
| testing | `nightly-<yyyymmdd>-<hhmm>-<commit>`; the image's apt follows testing | the CI key (environment walnut-testing) | `/download/nightly/<id>/` | the newest 3; `/download/nightly/latest` |

walnut-watch checks the key by id (a nightly signed by the archive key, or a stable one by the CI key, is refused) and never replaces a published id.

## Concurrency: what can run at once, and why nothing breaks

The rule everywhere: content appears whole or not at all (a rename), and anything decided on a state carries that state, so a decision made on an old state is refused, never applied.

| Case | Why it's safe |
|---|---|
| Two release runs of one channel | The workflow's concurrency group runs them one after the other, so the next A.B.C is counted from what's published; an id that exists is refused. |
| Uploads in parallel | Every upload has its own id (time, commit, package, target, attempt), rrsync never overwrites or deletes, the manifest goes last. |
| A manifest before its files (bug, abort) | walnut-watch copies every file with O_NOFOLLOW and hashes the copy against the manifest: a missing or partial file refuses the upload. An upload without its manifest is never touched, and goes after 2 days. |
| walnut-watch twice (path unit, timer, `--rebuild`) | One flock (`state/lock`): one instance at a time, the pool sweep inside the same lock. |
| Two publishes built on the same suite state | Each signed Release names its base (`X-Walnut-Base`, the published InRelease's sha256); the second one finds another base published and is refused as **stale**. `github-ci/publish.sh` reads that verdict from `/status/apt__<id>` and builds again on the new state; nothing is lost or overwritten. |
| A publish with two suites (`testing` + `testing-debug`) | Every suite's new generation is staged (hidden, unused) before any switch; a failure stages nothing in the end. Then the symlink switches, back to back. |
| apt clients during a switch | The switch is one `rename` of a symlink; indices are read by hash, and the old generation's by-hash files are carried over for 14 days; pool files no index names any more stay 14 days too. `apt-repo/walnut-repo.py` reads by hash as well. |
| Publishing while a site update runs | Separate containers: the site switch restarts `walnut-site` only. |
| A publisher's own verdict | `/status/<kind>__<id>`, written whole by rename: `ok`, `stale` or `refused` with the reason. No guessing from hashes. |
| ccache: two builds writing one key | Keys are hashes of compiler, flags and preprocessed source: both write a valid result for it. nginx takes a PUT into `.incoming` on the same disk and renames it into place: a reader sees the old file or the whole new one. Measured 2026-10-22: 8 writers alternating two 4 MB objects on one key, 8 readers, 96 PUTs and 200 GETs, every read byte-exact one of the two (or 404 before the first), no failures. |
| ccache: a broken or cut-off upload | An incomplete PUT is never renamed in; ccache checks every entry's checksum and takes a bad one as a miss. |
| ccache: the trim during a build | A file deleted while it's being read is read to the end; the next lookup misses. The trim leaves `.incoming` alone. |
| Redeploys during builds | Containers that change are recreated: uploads retry (`publish.sh`), apt and ccache requests retry or miss. |

Tests: `servers/walnut.nanobyte.dev/test_walnut_watch.py` (stale bases, two-suite staging, status files, the sweep, hostile uploads) and `apt-repo/test_walnut_repo.py` (two publishers on one base, against the real walnut-watch).
