#!/usr/bin/python3 -I
"""batch-publish.py: testing's one publisher (.github/workflows/walnut-publish.yml).

Package workflows only build: each successful build stores its output (github-ci/build-package.sh's
directory: the debs, the -dbgsym debs, the source package) as a GitHub artifact named

    walnut-<suite>@<package>@<source>=<version>[,<source>=<version>...]

(the newest version of each source its debs come from; an epoch's ':' is written '%', which no
artifact name may hold). The name says what the build would add, so the publisher decides from the
artifact listing and the published index alone, without downloading anything it doesn't publish:

  - per package, the newest build (highest version, then the newest artifact) supersedes the older ones;
  - it is skipped when testing already has every one of its sources at that version or a newer one
    (published before, or superseded by a newer published build); a version published with other
    bytes stays as it is (walnut-repo.py: published files never change);
  - everything else is published together, in one walnut-repo.py add and one upload (publish.sh), split
    only when the artifacts add up to more than --max-bytes (default 3 GiB; one bigger artifact goes
    alone): a runner has room for a bundle and its downloads twice over, and walnut-watch takes 8 GiB.

No state is kept anywhere else: what testing's signed index holds is what is published, so a run is
idempotent, a failed publish leaves its artifacts for the next run, and two runs can't publish one
build twice (one run at a time; were two to race, walnut-watch makes the second stale and
walnut-repo.py skips what the first published). A linux build that goes out queues ps3rsx's build for
testing (its GPU module is built against testing's kernel).

  name SUITE PKG DIR     the artifact name for DIR (the build job)
  plan [--suite S]       what publish would do; exit 0, prints "todo N" last (the publisher's check)
  publish [--suite S] [--work DIR] [--max-bytes N] [--dry-run]
Env: GITHUB_TOKEN, GITHUB_REPOSITORY, GITHUB_API_URL (Actions sets the last two), WALNUT_APT_URL
(walnut-repo.py's), WALNUT_KEYRING (tests: the keyring testing is checked against), and for publish
what github-ci/publish.sh needs (the keys).
"""
import argparse, functools, importlib.machinery, importlib.util, json, os, re, shutil, subprocess, sys
import time, urllib.error, urllib.request, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_loader("walnut_repo", importlib.machinery.SourceFileLoader(
	"walnut_repo", f"{ROOT}/apt-repo/walnut-repo.py"))
wr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(wr)

KEYRING = os.environ.get("WALNUT_KEYRING", f"{ROOT}/apt-repo/keyrings/testing-keyring.gpg")	# tests: their own
NAME_RE = re.compile(r"walnut-(testing|stable)@([a-z0-9][a-z0-9.+-]*)@([a-z0-9][a-z0-9.+-]*=[A-Za-z0-9.+~%-]+"
                     r"(?:,[a-z0-9][a-z0-9.+-]*=[A-Za-z0-9.+~%-]+)*)\Z")
FILE_RE = re.compile(r"(?:src/)?[A-Za-z0-9_+~-][A-Za-z0-9._+~-]{0,200}\Z")	# what an artifact may hold
REBUILD_AFTER = {"linux": ["ps3rsx"]}	# published package -> workflows built for testing after it
BRANCH = "main"


def log(msg):
	print(msg, flush=True)


# --- artifact names ------------------------------------------------------------------------------

def output_name(suite, pkg, d):
	"""walnut-SUITE@PKG@SRC=VER,...: each source's newest version among DIR's debs."""
	newest = {}
	for f in sorted(os.listdir(d)):
		if not f.endswith(".deb"):
			continue
		s = wr.parse_stanzas(wr.deb_control(f"{d}/{f}"))[0]
		src, ver = wr.stanza_source(s), wr.stanza_source_version(s)
		if src not in newest or wr.vercmp(ver, newest[src]) > 0:
			newest[src] = ver
	if not newest:
		raise wr.Error(f"{d}: no .deb")
	name = f"walnut-{suite}@{pkg}@" + ",".join(f"{s}={v.replace(':', '%')}" for s, v in sorted(newest.items()))
	if not NAME_RE.match(name):
		raise wr.Error(f"{name}: not a name the publisher reads")
	return name


def parse_name(name):
	"""(suite, pkg, {source: version}) or None."""
	m = NAME_RE.match(name)
	if not m:
		return None
	pairs = dict(p.split("=", 1) for p in m.group(3).split(","))
	return m.group(1), m.group(2), {s: v.replace("%", ":") for s, v in pairs.items()}


# --- GitHub --------------------------------------------------------------------------------------

class GitHub:
	def __init__(self):
		self.api = os.environ.get("GITHUB_API_URL", "https://api.github.com").rstrip("/")
		self.repo = os.environ["GITHUB_REPOSITORY"]
		self.token = os.environ["GITHUB_TOKEN"]

	def request(self, path, data=None, tries=5):
		url = path if path.startswith("http") else f"{self.api}/repos/{self.repo}/{path}"
		for n in range(tries):
			req = urllib.request.Request(url, data=json.dumps(data).encode() if data is not None else None,
			                             method="POST" if data is not None else "GET")
			# not sent on to where a download redirects (the blob store has its own signed URL)
			req.add_unredirected_header("Authorization", f"Bearer {self.token}")
			req.add_header("Accept", "application/vnd.github+json")
			req.add_header("X-GitHub-Api-Version", "2022-11-28")
			try:
				return urllib.request.urlopen(req, timeout=120)
			except urllib.error.HTTPError as e:
				if e.code < 500 and e.code != 429 or n == tries - 1:
					raise
			except (urllib.error.URLError, TimeoutError, ConnectionError):
				if n == tries - 1:
					raise
			time.sleep(10 * (n + 1))

	def artifacts(self):
		out, page = [], 1
		while True:
			with self.request(f"actions/artifacts?per_page=100&page={page}") as r:
				batch = json.load(r)["artifacts"]
			out += batch
			if len(batch) < 100:
				return out
			page += 1

	def download(self, art, to):
		"""The artifact's files into to/ (only .deb files and src/ files with plain names)."""
		os.makedirs(to)
		z = f"{to}.zip"
		with self.request(f"actions/artifacts/{art['id']}/zip") as r, open(z, "wb") as f:
			shutil.copyfileobj(r, f, 1 << 20)
		try:
			with zipfile.ZipFile(z) as zf:
				for m in zf.infolist():
					if m.is_dir():
						continue
					if not FILE_RE.match(m.filename):
						raise wr.Error(f"{art['name']}: unexpected file {m.filename!r}")
					dst = f"{to}/{m.filename}"
					os.makedirs(os.path.dirname(dst), exist_ok=True)
					with zf.open(m) as src, open(dst, "wb") as f:
						shutil.copyfileobj(src, f, 1 << 20)
		finally:
			os.unlink(z)

	def dispatch(self, workflow, inputs):
		with self.request(f"actions/workflows/{workflow}/dispatches", {"ref": BRANCH, "inputs": inputs}):
			pass


# --- the plan ------------------------------------------------------------------------------------

def newest_builds(artifacts, suite):
	"""Per package, the build that supersedes the others: [(pkg, sources, artifact)]."""
	best = {}
	for a in artifacts:
		p = parse_name(a["name"])
		if not p or p[0] != suite or a.get("expired") or (a.get("workflow_run") or {}).get("head_branch") != BRANCH:
			continue
		_, pkg, srcs = p
		cur = best.get(pkg)
		if cur is None or later(srcs, a, cur[0], cur[1]):
			best[pkg] = (srcs, a)
	return [(pkg, srcs, a) for pkg, (srcs, a) in sorted(best.items())]


def later(srcs, a, srcs2, a2):
	"""Whether build (srcs, a) supersedes (srcs2, a2): a higher version, else the newer artifact."""
	c = wr.vercmp(max(srcs.values(), key=functools.cmp_to_key(wr.vercmp)),
	              max(srcs2.values(), key=functools.cmp_to_key(wr.vercmp)))
	return c > 0 or c == 0 and (a["created_at"], a["id"]) > (a2["created_at"], a2["id"])


def published_versions(suite_state):
	"""{source: its newest version in the suite}."""
	out = {}
	for s in suite_state.packages:
		src, ver = wr.stanza_source(s), wr.stanza_source_version(s)
		if src not in out or wr.vercmp(ver, out[src]) > 0:
			out[src] = ver
	return out


def plan(artifacts, suite, have):
	"""(to publish, skipped): to publish [(pkg, sources, artifact)], skipped [(pkg, why)]."""
	todo, skipped = [], []
	for pkg, srcs, a in newest_builds(artifacts, suite):
		vs = {s: have.get(s) for s in srcs}
		if all(v is not None and wr.vercmp(v, srcs[s]) >= 0 for s, v in vs.items()):
			why = "published" if all(vs[s] == srcs[s] for s in srcs) else "superseded by " + \
				",".join(f"{s} {v}" for s, v in vs.items())
			skipped.append((pkg, why))
		else:
			todo.append((pkg, srcs, a))
	return todo, skipped


def chunks(todo, max_bytes):
	"""The builds in bundles of at most max_bytes of artifacts (a bigger one alone), biggest first."""
	out, cur, size = [], [], 0
	for t in sorted(todo, key=lambda t: -t[2]["size_in_bytes"]):
		n = t[2]["size_in_bytes"]
		if cur and size + n > max_bytes:
			out.append(cur)
			cur, size = [], 0
		cur.append(t)
		size += n
	return out + ([cur] if cur else [])


def describe(t):
	pkg, srcs, a = t
	return f"{pkg} ({','.join(f'{s} {v}' for s, v in srcs.items())}; artifact {a['id']} of run " \
	       f"{a['workflow_run']['id']}, {a['size_in_bytes'] / 1e6:.1f} MB)"


def current_plan(gh, a):
	state = wr.Suite(a.url, a.suite, KEYRING)
	todo, skipped = plan(gh.artifacts(), a.suite, published_versions(state))
	for pkg, why in skipped:
		log(f"-- {pkg}: {why}")
	for t in todo:
		log(f".. {describe(t)}")
	return todo


def summary(lines):
	f = os.environ.get("GITHUB_STEP_SUMMARY")
	if f:
		with open(f, "a") as fh:
			fh.write("\n".join(lines) + "\n")


# --- commands ------------------------------------------------------------------------------------

def cmd_name(a):
	print(output_name(a.suite, a.pkg, a.dir))


def cmd_plan(a):
	todo = current_plan(GitHub(), a)
	print(f"todo {len(todo)}")


def cmd_publish(a):
	gh = GitHub()
	todo = current_plan(gh, a)
	if not todo:
		log(">> nothing to publish")
		summary(["Nothing to publish."])
		return 0
	failed, done = [], []
	parts = chunks(todo, a.max_bytes)
	for n, part in enumerate(parts, 1):
		work = f"{os.path.abspath(a.work)}/{n}"
		shutil.rmtree(work, ignore_errors=True)
		dirs = []
		for t in part:
			d = f"{work}/{t[0]}"
			try:
				gh.download(t[2], d)
				dirs.append((t, d))
			except (wr.Error, urllib.error.URLError, OSError, zipfile.BadZipFile) as e:
				log(f"!! {t[0]}: download failed: {e}")
				failed.append(t[0])
		if not dirs:
			continue
		names = ", ".join(t[0] for t, _ in dirs)
		log(f">> bundle {n}/{len(parts)}: {names}")
		if a.dry_run:
			done += [t[0] for t, _ in dirs]
			continue
		rc = publish(a.suite, f"batch{len(dirs)}" if len(dirs) > 1 else dirs[0][0][0], [d for _, d in dirs])
		if rc == 3 and len(dirs) > 1:
			# refused: one package must not hold back the others; each alone shows which
			log("-- the bundle was refused: each package on its own")
			for t, d in dirs:
				(done if publish(a.suite, t[0], [d]) == 0 else failed).append(t[0])
		else:
			(done if rc == 0 else failed).extend(t[0] for t, _ in dirs)
		shutil.rmtree(work, ignore_errors=True)
	for pkg in done:
		for wf in REBUILD_AFTER.get(pkg, []):
			if a.dry_run:
				log(f"-- would build {wf} for {a.suite} (after {pkg})")
				continue
			try:
				gh.dispatch(f"pkg-{wf}.yml", {"target": a.suite})
				log(f">> {wf}: built for {a.suite} next (after {pkg})")
			except (urllib.error.URLError, OSError) as e:	# published all the same: not queued again
				log(f"!! {wf}: could not start its build ({e}): run pkg-{wf}.yml for {a.suite} by hand")
				failed.append(f"{wf} (start its build by hand)")
	summary([f"Published to {a.suite}: {', '.join(done) or 'nothing'}."] +
	        ([f"Failed (left for the next run): {', '.join(failed)}."] if failed else []))
	log(f">> published: {' '.join(done) or '-'}; failed: {' '.join(failed) or '-'}")
	return 1 if failed else 0


def publish(suite, label, dirs):
	return subprocess.run([f"{ROOT}/github-ci/publish.sh", suite, label, *dirs]).returncode


def main():
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = p.add_subparsers(dest="cmd", required=True)
	s = sub.add_parser("name")
	s.add_argument("suite", choices=("testing", "stable"))
	s.add_argument("pkg")
	s.add_argument("dir")
	for c in ("plan", "publish"):
		s = sub.add_parser(c)
		s.add_argument("--suite", default="testing", choices=("testing",))
		s.add_argument("--url", default=os.environ.get("WALNUT_APT_URL", wr.URL))
		if c == "publish":
			s.add_argument("--work", default="build/tmp/batch-publish")
			s.add_argument("--max-bytes", type=int, default=3 << 30)
			s.add_argument("--dry-run", action="store_true", help="download and plan the bundles, upload nothing")
	a = p.parse_args()
	try:
		sys.exit({"name": cmd_name, "plan": cmd_plan, "publish": cmd_publish}[a.cmd](a) or 0)
	except wr.Error as e:
		sys.exit(f"!! {e}")


if __name__ == "__main__":
	main()
