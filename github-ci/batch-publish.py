#!/usr/bin/python3 -I
"""batch-publish.py: testing's one publisher (.github/workflows/walnut-publish.yml).

Package workflows only build: each successful build stores its output (github-ci/build-package.sh's
directory: the debs, the -dbgsym debs, the source package) as a GitHub artifact named

    walnut-<suite>@<package>@<source>=<version>[,<source>=<version>...]

(the newest version of each source its debs come from; an epoch's ':' is written '%', which no
artifact name may hold). The name says what the build would add, so the publisher decides from the
artifact listing and the published index alone, without downloading anything it doesn't publish:

  - only a build of this repository's main counts: the artifact's run is a push or a dispatch on main, of
    this repository's own code (head_repository is this repository: a fork's pull request runs here too,
    and its branch may be called main), at a commit main contains (trusted(); the rest is refused);
  - per package, the newest build (highest version, then the newest artifact) supersedes the older ones;
  - it is skipped when testing already has every one of its sources at that version or a newer one
    (published before, or superseded by a newer published build); a version published with other
    bytes stays as it is (walnut-repo.py: published files never change);
  - everything else is published together, in one walnut-repo.py add and one upload (publish.sh), split
    only when the artifacts add up to more than --max-bytes (default 3 GiB; one bigger artifact goes
    alone): a runner has room for a bundle and its downloads twice over, and walnut-watch takes 8 GiB.

Which names a build may publish (github-ci/packages.toml, github-ci/walnut_lists.py): every deb must be one
its package owns, from a Source and a version the package's entry gives, and the relations it names
(Replaces, Breaks, Conflicts, Provides) must be ones the entry lists. The lists are read from git at the run's
head_sha and at the checkout's HEAD (a name must be the package's in both), never from the artifact. A
refusal drops that artifact only; a refusal of the artifact's contents deletes it (publish --delete-refused),
so the next run doesn't meet it again. verify (a job with no secrets) checks and writes a manifest of what
passed; publish (the job with the keys) checks the same bytes again, against that manifest.

No state is kept anywhere else: what testing's signed index holds is what is published, so a run is
idempotent, a failed publish leaves its artifacts for the next run, and two runs can't publish one
build twice (one run at a time; were two to race, walnut-watch makes the second stale and
walnut-repo.py skips what the first published). A linux build that goes out queues ps3rsx's build for
testing (its GPU module is built against testing's kernel).

  name SUITE PKG DIR     the artifact name for DIR (the build job)
  plan [--suite S]       what publish would do; exit 0, prints "todo N" last (the publisher's check)
  verify [--suite S] [--work DIR] [--manifest FILE]
                         downloads and checks what publish would publish; writes the manifest (no secrets)
  publish [--suite S] [--work DIR] [--max-bytes N] [--dry-run] [--manifest FILE] [--delete-refused]
  check-output PKG DIR   the checks on a build's output directory (build-package.sh; stable's publish job)
Env: GITHUB_TOKEN, GITHUB_REPOSITORY, GITHUB_API_URL (Actions sets the last two), WALNUT_APT_URL
(walnut-repo.py's), WALNUT_KEYRING (tests: the keyring testing is checked against), and for publish
what github-ci/publish.sh needs (the keys).
"""
import argparse, functools, hashlib, importlib.machinery, importlib.util, json, os, re, shutil, subprocess, sys
import tempfile, time, urllib.error, urllib.request, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_loader("walnut_repo", importlib.machinery.SourceFileLoader(
	"walnut_repo", f"{ROOT}/apt-repo/walnut-repo.py"))
wr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(wr)

_spec = importlib.util.spec_from_loader("walnut_lists", importlib.machinery.SourceFileLoader(
	"walnut_lists", f"{ROOT}/github-ci/walnut_lists.py"))
wl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(wl)

GIT_DIR = os.environ.get("WALNUT_GIT_DIR", ROOT)	# the trusted checkout the lists are read from (tests: their own)
LEAK_CHECK = os.environ.get("WALNUT_LEAK_CHECK", f"{ROOT}/tools/leak-check.py")	# tests: their own
LISTS = "github-ci/packages.toml"
PROTECTED = "github-ci/protected-names.txt"
# checks that only warn while the rollout runs (due before the next public release); everything else
# refuses from the first commit. The strictness is this file's, never an old commit's.
WARN = {"version", "arch", "filename", "dsc-version", "relations"}
WORKFLOW_RE = re.compile(r"\.github/workflows/pkg-([a-z0-9][a-z0-9.+-]*)\.yml(?:@refs/heads/main)?\Z")
MAX_ZIP_FILES, MAX_ZIP_BYTES = 1000, 8 << 30
KEYRING = os.environ.get("WALNUT_KEYRING", f"{ROOT}/apt-repo/keyrings/testing-keyring.gpg")	# tests: their own
NAME_RE = re.compile(r"walnut-(testing|stable)@([a-z0-9][a-z0-9.+-]*)@([a-z0-9][a-z0-9.+-]*=[A-Za-z0-9.+~%-]+"
                     r"(?:,[a-z0-9][a-z0-9.+-]*=[A-Za-z0-9.+~%-]+)*)\Z")
FILE_RE = re.compile(r"(?:src/)?[A-Za-z0-9_+~-][A-Za-z0-9._+~-]{0,200}\Z")	# what an artifact may hold
REBUILD_AFTER = {"linux": ["ps3rsx"]}	# published package -> workflows built for testing after it
BRANCH = "main"
EVENTS = ("push", "workflow_dispatch")	# a build of main: pushed to it, or started on it (REBUILD_AFTER, by hand)
SHA_RE = re.compile(r"[0-9a-f]{40}\Z")


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


class ZipRefusal(wr.Error):
	"""The artifact's zip itself is wrong (names, bounds, damage): about its contents, not about the network."""


# --- GitHub --------------------------------------------------------------------------------------

class GitHub:
	def __init__(self):
		self.api = os.environ.get("GITHUB_API_URL", "https://api.github.com").rstrip("/")
		self.repo = os.environ["GITHUB_REPOSITORY"]
		self.token = os.environ["GITHUB_TOKEN"]

	def request(self, path, data=None, tries=5, method=None):
		url = path if path.startswith("http") else f"{self.api}/repos/{self.repo}/{path}"
		for n in range(tries):
			req = urllib.request.Request(url, data=json.dumps(data).encode() if data is not None else None,
			                             method=method or ("POST" if data is not None else "GET"))
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
				# the zip is the build job's own work (it can replace the upload action): bounded before extracting
				infos = zf.infolist()
				if len(infos) > MAX_ZIP_FILES or sum(m.file_size for m in infos) > MAX_ZIP_BYTES:
					raise ZipRefusal(f"artifact {art['id']}: too many or too big files in the zip")
				for m in infos:
					if m.is_dir():
						continue
					if not FILE_RE.match(m.filename):
						raise ZipRefusal(f"{art['name']}: unexpected file {esc(m.filename)}")
					dst = f"{to}/{m.filename}"
					os.makedirs(os.path.dirname(dst), exist_ok=True)
					with zf.open(m) as src, open(dst, "wb") as f:
						shutil.copyfileobj(src, f, 1 << 20)
		except zipfile.BadZipFile as e:
			raise ZipRefusal(f"artifact {art['id']}: not a zip ({esc(e)})") from None
		finally:
			os.unlink(z)

	@functools.cached_property
	def repo_id(self):
		with self.request(f"{self.api}/repos/{self.repo}") as r:
			return json.load(r)["id"]

	def run(self, run_id):
		with self.request(f"actions/runs/{int(run_id)}") as r:
			return json.load(r)

	def on_main(self, sha):
		"""Whether main contains commit sha (compare sha...main: main identical to it or ahead of it)."""
		with self.request(f"compare/{sha}...{BRANCH}") as r:
			return json.load(r).get("status") in ("identical", "ahead")

	def delete_artifact(self, art):
		with self.request(f"actions/artifacts/{int(art['id'])}", method="DELETE"):
			pass

	def dispatch(self, workflow, inputs):
		with self.request(f"actions/workflows/{workflow}/dispatches", {"ref": BRANCH, "inputs": inputs}):
			pass


# --- the plan ------------------------------------------------------------------------------------

def newest_builds(artifacts, suite):
	"""Per package, the build that supersedes the others: [(pkg, sources, artifact)]."""
	best = {}
	for a in artifacts:
		p = parse_name(a["name"])
		run = a.get("workflow_run") or {}
		if (not p or p[0] != suite or a.get("expired") or run.get("head_branch") != BRANCH
				or not run.get("repository_id") or run.get("head_repository_id") != run.get("repository_id")):
			continue	# not a build of main, or of a fork's code (a pull request from a fork runs here too)
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


def run_refusal(run, repo_id):
	"""Why a workflow run is not a build of this repository's main (None when it is), from the run's own
	record (actions/runs/N); trusted() also asks whether main contains its commit."""
	if run.get("event") not in EVENTS:
		return f"its run is a {run.get('event')!s:.40} event, not a push or dispatch"
	if run.get("head_branch") != BRANCH:
		return f"its run is not on {BRANCH}"
	if not repo_id or (run.get("head_repository") or {}).get("id") != repo_id \
			or (run.get("repository") or {}).get("id") != repo_id:
		return "its run built another repository's code"
	if not SHA_RE.match(run.get("head_sha") or ""):
		return "its run names no commit"
	return None


def trusted(gh, a):
	"""Why artifact a is refused (None: a build of this repository's main): its run is checked against GitHub
	(the event, the repository its code came from) and its commit must be on main. Any error refuses.
	The run's record is kept on the artifact (a["run_record"]) for the checks after this one."""
	try:
		run = gh.run(a["workflow_run"]["id"])
		why = run_refusal(run, gh.repo_id)
		if why is None and not gh.on_main(run["head_sha"]):
			why = f"its commit {run['head_sha'][:12]} is not on {BRANCH}"
		a["run_record"] = run
		return why
	except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError) as e:
		return f"its run could not be checked ({e})"


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
	"""(Ctx, [builds to publish], [(build, why, about_contents)] refused before any download)."""
	state = wr.Suite(a.url, a.suite, KEYRING)
	debug = wr.Suite(a.url, f"{a.suite}-debug", KEYRING)
	todo, skipped = plan(gh.artifacts(), a.suite, published_versions(state))
	for pkg, why in skipped:
		log(f"-- {pkg}: {why}")

	def index(st):
		out = {}
		for s in st.packages:
			out.setdefault(s["Package"], set()).add(wr.stanza_source(s))
		return out
	ctx = Ctx(a.suite, index(state), index(debug), warn=getattr(a, "warn", None),
	          dpkg=getattr(a, "dpkg", "auto"))
	ok, refused = [], []
	for t in todo:
		why = trusted(gh, t[2])
		if why:
			log(f"!! {t[0]}: artifact {t[2]['id']} refused, not a build of {BRANCH}: {why}")
			continue
		run = t[2]["run_record"]
		wf = WORKFLOW_RE.fullmatch(run.get("path") or "")
		if not wf or wf.group(1) != t[0]:
			refused.append((t, f"its run is workflow {esc(run.get('path'))}, not pkg-{t[0]}.yml", False))
			continue
		try:
			ctx.lists(run["head_sha"])
			ctx.lists("HEAD")
		except NoList as e:
			refused.append((t, str(e), False))
			continue
		log(f".. {describe(t)}")
		ok.append(t)
	for t, why, _ in refused:
		log(f"!! {t[0]}: artifact {t[2]['id']} refused: {why}")
	return ctx, ok, refused


def summary(lines):
	f = os.environ.get("GITHUB_STEP_SUMMARY")
	if f:
		with open(f, "a") as fh:
			fh.write("\n".join(lines) + "\n")


# --- what a build may publish ---------------------------------------------------------------------

def esc(x, n=200):
	"""A string from an artifact, safe in a log line and the step summary: ASCII, no control characters
	(so no workflow command can be smuggled in), cut."""
	return ascii(str(x))[:n]


class NoList(Exception):
	pass


class Verdict:
	"""The outcome for one artifact: refusals [(id, message, about_contents)], warnings [(id, message)]."""

	def __init__(self):
		self.refuse, self.warn = [], []

	@property
	def delete(self):
		"""Whether the artifact may be deleted: it was refused for what it contains, nothing else."""
		return bool(self.refuse) and all(c for _, _, c in self.refuse)


class Ctx:
	"""What the checks need: the lists (from git at a commit, and at HEAD), the suite's index, the batch's claims."""

	def __init__(self, suite="testing", index=None, debug_index=None, warn=None, dpkg="auto", leak=True,
	             disk_lists=None):
		self.suite, self.index, self.debug_index = suite, index or {}, debug_index or {}
		self.warn = WARN if warn is None else warn
		self.dpkg, self.leak = dpkg, leak
		self.disk_lists = disk_lists	# check-output: the working tree's file, not git
		self._lists, self._versions, self.claimed = {}, {}, {}
		text = self.show("HEAD", PROTECTED)
		self.protected = wl.load_protected(text) if text is not None else None	# None: no list, nothing is judged

	def show(self, rev, path):
		"""A file at a commit of the trusted checkout (None when it isn't there)."""
		if self.disk_lists and path in (LISTS, PROTECTED):
			p = os.path.join(GIT_DIR, path)
			return open(p).read() if os.path.exists(p) else None
		if rev != "HEAD" and not SHA_RE.match(rev):
			return None
		r = subprocess.run(["git", "-C", GIT_DIR, "show", f"{rev}:{path}"], capture_output=True)
		return r.stdout.decode("utf-8", "replace") if r.returncode == 0 else None

	def lists(self, rev):
		if rev not in self._lists:
			text = self.show(rev, LISTS)
			if text is None:
				self._lists[rev] = NoList(f"no {LISTS} at {rev[:12]}")
			else:
				try:
					self._lists[rev] = wl.Lists(text)
				except wl.ListError as e:
					self._lists[rev] = NoList(f"{LISTS} at {rev[:12]}: {e}")
		if isinstance(self._lists[rev], NoList):
			raise self._lists[rev]
		return self._lists[rev]

	def expected_n(self, sha, pkg):
		"""(N, TYPE, VERSION) of the package at commit sha: tools/deb-version.sh of that commit, run in a
		throwaway worktree of it (full history), or None."""
		key = (sha, pkg)
		if key not in self._versions:
			self._versions[key] = None
			with tempfile.TemporaryDirectory() as t:
				wt = f"{t}/w"
				try:
					subprocess.run(["git", "-C", GIT_DIR, "worktree", "add", "--detach", "-q", wt, sha],
					               check=True, capture_output=True)
					out = subprocess.run([f"{wt}/tools/deb-version.sh", pkg], cwd=wt, check=True, capture_output=True,
					                     text=True, timeout=300).stdout.split("\n")[0]
					pd = subprocess.run([f"{wt}/tools/pkg-dir.sh", pkg], cwd=wt, check=True, capture_output=True,
					                    text=True).stdout.strip()
					conf = open(f"{wt}/{pd}/package.conf").read()
					g = lambda k: (re.search(rf'^{k}="?([^"\n]*)"?$', conf, re.M) or [None, ""])[1]
					if re.fullmatch(r"[0-9]+", out):
						self._versions[key] = (out, g("TYPE"), g("VERSION"))
				except (subprocess.SubprocessError, OSError):
					pass
				finally:
					subprocess.run(["git", "-C", GIT_DIR, "worktree", "remove", "--force", wt], capture_output=True)
		return self._versions[key]


def sha256_file(p):
	h = hashlib.sha256()
	with open(p, "rb") as f:
		while chunk := f.read(1 << 20):
			h.update(chunk)
	return h.hexdigest()


def hash_dir(d):
	"""{relative path: sha256} of every file under d (symlinks and special files are an error)."""
	out = {}
	for dp, _, fs in os.walk(d):
		for f in fs:
			p = os.path.join(dp, f)
			if os.path.islink(p) or not os.path.isfile(p):
				raise wr.Error(f"{os.path.relpath(p, d)}: not a regular file")
			out[os.path.relpath(p, d)] = sha256_file(p)
	return out


def expected_version_ok(ver, pkg, exp):
	"""Whether deb version ver is what a build of pkg at that commit gives."""
	n, typ, base = exp
	if typ == "native":
		return re.fullmatch(rf"0\.{re.escape(n)}(\+k.+)?", ver) is not None
	return ver.endswith(f"+ps3.{n}") and (not base or ver.startswith(base + "+ps3."))


def dpkg_view(path):
	r = subprocess.run(["dpkg-deb", "-f", path, "Package", "Version", "Architecture", "Source"],
	                   capture_output=True, text=True, timeout=120)
	if r.returncode:
		raise wr.Error(f"dpkg-deb -f: {r.stderr.strip()[:200]}")
	return dict(l.split(": ", 1) for l in r.stdout.splitlines() if ": " in l)


def check_artifact(ctx, pkg, claimed_srcs, d, sha):
	"""Everything the publisher checks about one downloaded build (the output directory d of package pkg,
	built at commit sha, the artifact name claiming claimed_srcs {source: version}). A Verdict."""
	v = Verdict()

	def R(id_, msg, content=True):
		(v.warn.append((id_, msg)) if id_ in ctx.warn else v.refuse.append((id_, msg, content)))

	try:
		L, H = ctx.lists(sha), ctx.lists("HEAD")
		if ctx.protected is None:
			raise NoList(f"no {PROTECTED} at HEAD")
	except NoList as e:
		R("list", str(e), False)
		return v
	entry, hentry = L.entries.get(pkg), H.entries.get(pkg)
	if entry is None or hentry is None:
		R("list", f"{pkg} is not in {LISTS} at {sha[:12]} and at HEAD", False)
		return v
	if "licensed" in (entry.tier, hentry.tier):
		R("tier", f"{pkg} is licensed: CI never builds it")
		return v
	# the file set: debs at the top, a src/ of plain files
	try:
		top = sorted(os.listdir(d))
		src_files = sorted(os.listdir(f"{d}/src")) if os.path.isdir(f"{d}/src") else []
	except OSError as e:
		R("files", f"unreadable output ({e})", False)
		return v
	debs = [f for f in top if f.endswith(".deb")]
	extra = [f for f in top if f not in debs and f != "src"]
	if extra or not debs or any(not FILE_RE.match(f) or "/" in f for f in debs + src_files):
		R("files", f"the output must be .deb files and src/ files with plain names (got {esc(extra or top)})")
		return v
	if any(not os.path.isfile(f"{d}/src/{f}") or os.path.islink(f"{d}/src/{f}") for f in src_files):
		R("files", "src/ holds more than plain files")
		return v
	newest = {}
	for f in debs:
		path = f"{d}/{f}"
		try:
			fields = wr.deb_fields(path)
		except Exception as e:	# every malformed input is a refusal, whatever it raises
			R("format", f"{f}: {esc(e)}")
			continue
		name, ver = fields["Package"], fields["Version"]
		if ctx.dpkg != "off":
			try:
				if not shutil.which("dpkg-deb"):
					if ctx.dpkg == "require":
						raise wr.Error("dpkg-deb is missing")
				else:
					dv = dpkg_view(path)
					mine = {k: fields.get(k) for k in ("Package", "Version", "Architecture", "Source")}
					if {k: dv.get(k) for k in mine} != mine:
						R("format", f"{f}: dpkg-deb and walnut-repo read different fields ({esc(dv)} / {esc(mine)})")
						continue
			except (wr.Error, subprocess.SubprocessError, OSError) as e:
				R("format", f"{f}: {esc(e)}", False)
				continue
		# ownership: this package's name in the list at the run's commit and at HEAD
		own, hown = L.owner(name), H.owner(name)
		if own is None:
			R("name", f"{f}: {esc(name)} is in no package's list")
		elif own[0] != pkg or own[1]:
			R("name", f"{f}: {esc(name)} belongs to {own[0]}{' (retired)' if own[1] else ''}, not {pkg}")
		elif hown is None or hown[0] != pkg or hown[1]:
			R("name", f"{f}: {esc(name)} is no longer {pkg}'s at HEAD")
		elif ctx.claimed.get(name, pkg) != pkg:
			R("name", f"{f}: {esc(name)} is also claimed by {ctx.claimed[name]} in this batch")
		else:
			ctx.claimed[name] = pkg
		# the Source: this package's, in both lists; the suite's other owners of the name
		m = wr.SOURCE_FIELD_RE.match(fields.get("Source", name))
		src = m.group(1)
		srcver = m.group(2) or ver
		if src != entry.source or src != hentry.source:
			R("source", f"{f}: Source {esc(src)}, {pkg} makes {entry.source}")
		idx = ctx.debug_index if name.endswith("-dbgsym") else ctx.index
		others = idx.get(name, set()) - {entry.source} - (set(entry.moved_from) & set(hentry.moved_from))
		if others:
			R("moved", f"{f}: {esc(name)} is in the suite under {esc(sorted(others))}, not a listed move")
		if src not in newest or wr.vercmp(srcver, newest[src]) > 0:
			newest[src] = srcver
		# relations, version, architecture, file name
		rr, ww = [], []
		for LL, ee in ((L, entry), (H, hentry)):	# a relation must be allowed by both lists
			r1, w1 = wl.check_relations(LL, ctx.protected, ee, fields)
			rr += [x for x in r1 if x not in rr]
			ww += [x for x in w1 if x not in ww]
		for x in rr:
			v.refuse.append(("relations-r", f"{f}: {esc(x)}", True))
		for x in ww:
			R("relations", f"{f}: {esc(x)}")
		exp = ctx.expected_n(sha, pkg)
		if exp is None:
			R("version", f"{f}: the expected version could not be computed at {sha[:12]}", False)
		elif not expected_version_ok(ver, pkg, exp):
			R("version", f"{f}: version {esc(ver)} is not what {sha[:12]} gives {pkg} (N={exp[0]})")
		if fields.get("Architecture") not in ("ppc64", "all"):
			R("arch", f"{f}: architecture {esc(fields.get('Architecture'))}")
		if f != f"{name}_{ver.split(':', 1)[-1]}_{fields.get('Architecture')}.deb":
			R("filename", f"{f}: not {name}_{esc(ver)}_{esc(fields.get('Architecture'))}.deb")
	if v.refuse:
		return v
	# the artifact name only repeats what the debs say
	if len(newest) != 1:
		R("source", f"the debs come from {len(newest)} sources ({esc(sorted(newest))}); one per package")
	elif newest != claimed_srcs:
		R("claim", f"the name says {esc(claimed_srcs)}, the debs say {esc(newest)}")
	# the source package
	listed = set()
	for f in src_files:
		if f.endswith(".dsc"):
			try:
				st, files = wr.dsc_stanza(f"{d}/src/{f}", ctx.suite)
			except Exception as e:
				R("dsc", f"src/{f}: {esc(e)}")
				continue
			listed |= {os.path.basename(x) for x, _ in files}
			if st["Package"] != entry.source or st["Package"] != hentry.source:
				R("dsc", f"src/{f}: Source {esc(st['Package'])}, {pkg} makes {entry.source}")
			elif newest and st["Version"] != next(iter(newest.values())):
				R("dsc-version", f"src/{f}: version {esc(st['Version'])}, the debs' source version is {esc(newest)}")
	if [f for f in src_files if f not in listed]:
		R("dsc", f"src/ holds files no .dsc lists: {esc([f for f in src_files if f not in listed])}")
	# a package built elsewhere and pinned (BUILD=xcc): exactly the pinned files with the pinned hashes
	pins = [ctx.show(rev, f"github-ci/prebuilt/{pkg}.sha256") for rev in (sha, "HEAD")]	# at the commit and at HEAD
	if any(p is not None for p in pins):
		def pinned(pin):
			want = {}
			for l in (pin or "").splitlines():
				if l.strip() and not l.startswith("#"):
					h, _, n = l.partition(" ")
					want[("" if n.endswith(".deb") else "src/") + n.strip()] = h
			return want
		have = {os.path.relpath(os.path.join(dp, f), d): sha256_file(os.path.join(dp, f))
		        for dp, _, fs in os.walk(d) for f in fs}
		if pins[0] is None or pins[1] is None or have != pinned(pins[0]) or have != pinned(pins[1]):
			R("pin", f"the files are not exactly the ones github-ci/prebuilt/{pkg}.sha256 pins, at the commit and at HEAD")
	if ctx.leak and not v.refuse and os.path.exists(LEAK_CHECK):
		for kind, files in (("debs", [f"{d}/{f}" for f in debs]),
		                    ("sources", [f"{d}/src/{f}" for f in src_files if f.endswith(".dsc")])):
			if not files:
				continue
			r = subprocess.run([sys.executable, "-I", LEAK_CHECK, kind, *files],
			                   capture_output=True, text=True)
			if r.returncode == 1 and "LEAK:" in r.stdout:
				R("leak", f"leak-check {kind}: {esc(r.stdout, 300)}")	# a leak: about the contents
			elif r.returncode:	# a crash or an unreadable declaration is not a verdict: keep the artifact, fail visibly
				R("leak-error", f"leak-check {kind} did not finish (exit {r.returncode}): {esc(r.stdout + r.stderr, 300)}", False)
	return v


# --- commands ------------------------------------------------------------------------------------

def cmd_name(a):
	print(output_name(a.suite, a.pkg, a.dir))


def cmd_plan(a):
	_, todo, _ = current_plan(GitHub(), a)
	print(f"todo {len(todo)}")


def report(t, v):
	for id_, msg, _ in v.refuse:
		log(f"!! {t[0]}: artifact {t[2]['id']} refused ({id_}): {msg}")
	for id_, msg in v.warn:
		log(f"!! {t[0]}: warning ({id_}): {msg}")
		if os.environ.get("GITHUB_ACTIONS"):
			log(f"::warning title=walnut publish {t[0]}::{id_}")	# fixed text: nothing from the artifact in a command
	if os.environ.get("GITHUB_ACTIONS") and any(i == "leak-error" for i, _, _ in v.refuse):
		log("::error title=walnut publish::leak-check did not finish: artifact kept, not published")


def stage(d, to, want):
	"""Copies d to a fresh directory to while hashing; the copy is what gets published. The hashes must be
	the ones the checks saw (and the verify job's manifest)."""
	got = {}
	for dp, _, fs in os.walk(d):
		for f in fs:
			src = os.path.join(dp, f)
			rel = os.path.relpath(src, d)
			dst = os.path.join(to, rel)
			os.makedirs(os.path.dirname(dst), exist_ok=True)
			h = hashlib.sha256()
			with open(src, "rb") as fi, open(dst, "xb") as fo:
				while chunk := fi.read(1 << 20):
					h.update(chunk)
					fo.write(chunk)
			got[rel] = h.hexdigest()
	if got != want:
		raise wr.Error("the files changed after they were checked")


def cmd_verify(a):
	return run_batch(a, publishing=False)


def cmd_publish(a):
	return run_batch(a, publishing=True)


def run_batch(a, publishing):
	if publishing:
		# the keys leave this process's environment (and every child's: git, leak-check, ...); only publish.sh gets them
		for k in ("WALNUT_SIGN_KEY", "WALNUT_DEPLOY_KEY_DATA", "WALNUT_SIGN_PASSPHRASE"):
			if k in os.environ:
				SECRETS[k] = os.environ.pop(k)
	gh = GitHub()
	ctx, todo, refused = current_plan(gh, a)
	manifest = None
	if publishing and a.manifest:
		with open(a.manifest) as f:
			manifest = json.load(f)["ok"]
		ctx.dpkg = "off"	# verify's job (it holds no secret); this one reads the same bytes only with python
		ctx.leak = False	# verify ran leak-check on these bytes; the manifest binds them. Nothing here opens a deb or a tarball with a tool
	elif not publishing and a.dpkg_required:
		ctx.dpkg = "require"
	failed, done, ok_manifest, refused_names = [], [], {}, []
	if a.delete_refused and publishing and not a.dry_run:
		for t, why, content in refused:
			if content:
				delete(gh, t)
	if not todo:
		log(">> nothing to publish")
		summary(["Nothing to publish."])
		write_manifest(a, ok_manifest)
		return 0
	parts = chunks(todo, a.max_bytes)
	for n, part in enumerate(parts, 1):
		work = f"{os.path.abspath(a.work)}/{n}"
		shutil.rmtree(work, ignore_errors=True)
		dirs = []
		for t in part:
			d = f"{work}/{t[0]}"
			try:
				gh.download(t[2], d)
				sha = t[2]["run_record"]["head_sha"]
				v = check_artifact(ctx, t[0], t[1], d, sha)
				report(t, v)
				if v.refuse:
					refused_names.append(t[0])
					if v.delete and a.delete_refused and publishing and not a.dry_run:
						delete(gh, t)
					continue
				hashes = hash_dir(d)
				if manifest is not None:
					m = manifest.get(str(t[2]["id"]))
					if m is None:
						log(f"-- {t[0]}: artifact {t[2]['id']} was not verified; left for the next run")
						continue
					if m["files"] != hashes:
						raise wr.Error("the files are not the ones the verify job checked")
				ok_manifest[str(t[2]["id"])] = {"name": t[2]["name"], "files": hashes}
				dirs.append((t, d, hashes))
			except ZipRefusal as e:	# the zip itself is wrong: refused for its contents
				log(f"!! {t[0]}: artifact {t[2]['id']} refused (zip): {esc(e)}")
				refused_names.append(t[0])
				if a.delete_refused and publishing and not a.dry_run:
					delete(gh, t)
			except (wr.Error, urllib.error.URLError, OSError) as e:
				log(f"!! {t[0]}: {esc(e)}")
				failed.append(t[0])
			except Exception as e:	# a bug: this artifact is left for the next run, the others go on
				log(f"!! {t[0]}: internal error {type(e).__name__}: {esc(e)}")
				failed.append(t[0])
		if not dirs:
			continue
		names = ", ".join(t[0] for t, _, _ in dirs)
		log(f">> bundle {n}/{len(parts)}: {names}")
		if not publishing or a.dry_run:
			done += [t[0] for t, _, _ in dirs]
			shutil.rmtree(work, ignore_errors=True)
			continue
		staged = []
		for t, d, hashes in dirs:	# what is signed is a copy, hashed on the way
			sd = f"{work}/staged/{t[0]}"
			try:
				stage(d, sd, hashes)
				staged.append((t, sd))
			except (wr.Error, OSError) as e:
				log(f"!! {t[0]}: {esc(e)}")
				failed.append(t[0])
		if not staged:
			continue
		rc = publish(a.suite, f"batch{len(staged)}" if len(staged) > 1 else staged[0][0][0], [d for _, d in staged])
		if rc == 3 and len(staged) > 1:
			# refused: one package must not hold back the others; each alone shows which
			log("-- the bundle was refused: each package on its own")
			for t, d in staged:
				(done if publish(a.suite, t[0], [d]) == 0 else failed).append(t[0])
		else:
			(done if rc == 0 else failed).extend(t[0] for t, _ in staged)
		shutil.rmtree(work, ignore_errors=True)
	write_manifest(a, ok_manifest)
	if not publishing:
		log(f">> verified: {' '.join(done) or '-'}; refused: {' '.join(refused_names) or '-'}; failed: {' '.join(failed) or '-'}")
		summary([f"Verified: {', '.join(done) or 'nothing'}."] +
		        ([f"Refused: {', '.join(refused_names)}."] if refused_names else []) +
		        ([f"Failed (left for the next run): {', '.join(failed)}."] if failed else []))
		if failed and os.environ.get("GITHUB_ACTIONS"):
			log("::warning title=walnut verify::some builds could not be checked; the others go on")
		# one odd artifact never stops the rest: the status is bad only when nothing passed and something did not
		return 1 if not done and (failed or refused_names) else 0
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
	        ([f"Refused: {', '.join(refused_names)}."] if refused_names else []) +
	        ([f"Failed (left for the next run): {', '.join(failed)}."] if failed else []))
	log(f">> published: {' '.join(done) or '-'}; refused: {' '.join(refused_names) or '-'}; failed: {' '.join(failed) or '-'}")
	return 1 if failed else 0


def delete(gh, t):
	"""Deletes a refused artifact (its contents are in the log): the next run doesn't meet it again, and the
	package's next-newest build is looked at instead."""
	try:
		gh.delete_artifact(t[2])
		log(f"-- {t[0]}: artifact {t[2]['id']} deleted; the package's next-newest build is looked at next")
	except (urllib.error.URLError, OSError) as e:
		log(f"!! {t[0]}: artifact {t[2]['id']} could not be deleted ({esc(e)})")


def write_manifest(a, ok):
	if getattr(a, "manifest_out", None):
		with open(a.manifest_out, "w") as f:
			json.dump({"ok": ok}, f, separators=(",", ":"))


def cmd_check_output(a):
	"""The checks on a build's output directory, from the working tree's lists (build-package.sh: the
	drift check; stable's publish job: with --strict, nothing only warns)."""
	ctx = Ctx(a.suite, warn=set() if a.strict else None, dpkg="require" if a.strict else "auto", disk_lists=True)
	if a.suite_index:
		keyring = f"{ROOT}/apt-repo/keyrings/{'archive' if a.suite == 'stable' else 'testing'}-keyring.gpg"
		for st, attr in ((wr.Suite(a.url, a.suite, keyring), "index"), (wr.Suite(a.url, f"{a.suite}-debug", keyring), "debug_index")):
			for p in st.packages:
				getattr(ctx, attr).setdefault(p["Package"], set()).add(wr.stanza_source(p))
	sha = subprocess.run(["git", "-C", GIT_DIR, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
	try:
		ctx._lists[sha] = ctx.lists("HEAD")
	except NoList as e:
		log(f"!! {e}")
		if a.strict:
			return 1
		log("-- no usable list: not a failure")
		return 0
	ctx._lists["HEAD"] = ctx._lists[sha]
	claimed = {}
	debs = [f for f in sorted(os.listdir(a.dir)) if f.endswith(".deb")]
	if debs:	# the name the build job gives it: what its debs say
		name = output_name(a.suite, a.pkg, a.dir)
		claimed = parse_name(name)[2]
	v = check_artifact(ctx, a.pkg, claimed, a.dir, sha)
	t = (a.pkg, claimed, {"id": "-"})
	report(t, v)
	if v.refuse:
		if any(i == "list" for i, _, _ in v.refuse) and not a.strict:
			log("-- no usable list for this package yet: not a failure")
			return 0
		return 1
	return 0


SECRETS = {}	# the signing and upload keys, taken out of the environment; only publish.sh's child gets them


def publish(suite, label, dirs):
	env = {k: v for k, v in os.environ.items() if k not in ("GITHUB_TOKEN", "ACTIONS_RUNTIME_TOKEN", "ACTIONS_ID_TOKEN_REQUEST_TOKEN")}
	env.update(SECRETS)
	return subprocess.run([f"{ROOT}/github-ci/publish.sh", suite, label, *dirs], env=env).returncode


def main():
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = p.add_subparsers(dest="cmd", required=True)
	s = sub.add_parser("name")
	s.add_argument("suite", choices=("testing", "stable"))
	s.add_argument("pkg")
	s.add_argument("dir")
	for c in ("plan", "verify", "publish"):
		s = sub.add_parser(c)
		s.add_argument("--suite", default="testing", choices=("testing",))
		s.add_argument("--url", default=os.environ.get("WALNUT_APT_URL", wr.URL))
		if c != "plan":
			s.add_argument("--work", default="build/tmp/batch-publish")
			s.add_argument("--max-bytes", type=int, default=3 << 30)
			s.add_argument("--manifest-out", help="verify: write what passed, with its hashes")
		if c == "verify":
			s.add_argument("--dpkg-required", action="store_true", help="a missing dpkg-deb is a refusal")
			s.set_defaults(manifest=None, delete_refused=False, dry_run=False)
		if c == "publish":
			s.add_argument("--manifest", help="the verify job's manifest: publish only those, with those hashes")
			s.add_argument("--delete-refused", action="store_true", help="delete artifacts refused for their contents")
			s.add_argument("--dry-run", action="store_true", help="download and plan the bundles, upload nothing")
			s.set_defaults(dpkg_required=False)
	s = sub.add_parser("check-output")
	s.add_argument("pkg")
	s.add_argument("dir")
	s.add_argument("--suite", default="testing", choices=("testing", "stable"))
	s.add_argument("--strict", action="store_true", help="nothing only warns (stable)")
	s.add_argument("--suite-index", action="store_true", help="also check names against the published suite")
	s.add_argument("--url", default=os.environ.get("WALNUT_APT_URL", wr.URL))
	a = p.parse_args()
	try:
		sys.exit({"name": cmd_name, "plan": cmd_plan, "verify": cmd_verify, "publish": cmd_publish,
		      "check-output": cmd_check_output}[a.cmd](a) or 0)
	except wr.Error as e:
		sys.exit(f"!! {e}")


if __name__ == "__main__":
	main()
