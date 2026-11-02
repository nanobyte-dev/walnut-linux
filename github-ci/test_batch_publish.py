#!/usr/bin/python3 -I
"""github-ci/batch-publish.py, testing's one publisher: its plan (what supersedes what, what is
published) and whole runs against a fake GitHub API, github-ci/publish.sh and the real walnut-watch
(the bundle dropped into incoming/ the way walnut-upload.sh drops it). Throwaway CI key. Only this
repository's main runs count: a fork's pull request whose branch is called main, a pull_request run, a
commit main doesn't contain are refused.
  python3 github-ci/test_batch_publish.py"""
import functools, hashlib, http.server, importlib.machinery, importlib.util, io, json, lzma, os, shutil, subprocess
import sys, tarfile, tempfile, threading, unittest, urllib.parse, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPT = f"{HERE}/batch-publish.py"
WATCH = f"{ROOT}/servers/walnut.nanobyte.dev/walnut-watch"
sys.path.insert(0, f"{ROOT}/apt-repo")
from test_walnut_repo import make_deb	# noqa: E402

_spec = importlib.util.spec_from_loader("batch_publish", importlib.machinery.SourceFileLoader("batch_publish", SCRIPT))
bp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bp)


REPO_ID, FORK_ID = 42, 77
MAIN_SHA, OTHER_SHA, OLD_SHA = "a" * 40, "b" * 40, "d" * 40	# MAIN_SHA: replaced by the fixture repository's commit (setUpClass)
PKGS = ("foo", "bar", "baz", "qux", "big", "small", "tiny", "pr", "fork", "side", "ahead", "badwolf", "evil")


def fixture_repo(root, extra=""):
	"""A git repository the publisher reads packages.toml from: packages for the names the tests build, linux
	(owner of linux-image-@RELEASE@), badwolf (external), the real tools/deb-version.sh. Returns the commit."""
	os.makedirs(root, exist_ok=True)
	if not os.path.exists(f"{root}/.git"):	# a first commit with no list at all
		open(f"{root}/README", "w").write("x\n")
		fixture_repo.old = commit_all(root, "no list yet", init=True)
	os.makedirs(f"{root}/tools")
	for f in ("deb-version.sh", "pkg-dir.sh"):
		shutil.copy(f"{ROOT}/tools/{f}", f"{root}/tools/{f}")
	toml = 'version = 1\n[reserved]\n"linux-image-" = "linux"\n'
	for n in PKGS + ("linux",):
		d = f"packages/walnut/{n}"
		os.makedirs(f"{root}/{d}")
		open(f"{root}/{d}/package.conf", "w").write("TYPE=native\nBUILD=native\n")
		binaries = ["linux-image-@RELEASE@"] if n == "linux" else [n]
		tier = "external" if n == "badwolf" else "trusted"
		toml += f'\n[package.{n}]\ndir = "{d}"\nsource = "{n}"\ntier = "{tier}"\nbinaries = {json.dumps(binaries)}\n'
	os.makedirs(f"{root}/github-ci")
	open(f"{root}/github-ci/packages.toml", "w").write(toml + extra)
	open(f"{root}/github-ci/protected-names.txt", "w").write("libc6\nsudo\nmawk\n*-keyring\n")
	return commit_all(root, "fixture")


def commit_all(root, msg, init=False):
	g = ["git", "-C", root, "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"]
	if init:
		subprocess.run(["git", "-C", root, "init", "-q", "-b", "main"], check=True)
	subprocess.run(g + ["add", "-A"], check=True)
	subprocess.run(g + ["commit", "-q", "-m", msg], check=True)
	return subprocess.run(["git", "-C", root, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()


def art(aid, name, created="2026-10-26T10:00:00Z", size=1000, branch="main", expired=False, head_repo=REPO_ID, sha=None):
	return {"id": aid, "name": name, "created_at": created, "size_in_bytes": size, "expired": expired,
	        "workflow_run": {"id": 9000 + aid, "head_branch": branch, "repository_id": REPO_ID,
	                         "head_repository_id": head_repo, "head_sha": sha or MAIN_SHA}}


def run_record(run_id, event="push", branch="main", head_repo=REPO_ID, sha=None, path=".github/workflows/pkg-foo.yml"):
	"""actions/runs/N as GitHub has it (the fields the publisher reads)"""
	return {"id": run_id, "event": event, "head_branch": branch, "head_sha": sha or MAIN_SHA, "path": path,
	        "repository": {"id": REPO_ID, "full_name": "o/r"},
	        "head_repository": {"id": head_repo, "full_name": "o/r" if head_repo == REPO_ID else "someone/r"}}


class Plan(unittest.TestCase):
	def test_name_round_trip(self):
		with tempfile.TemporaryDirectory() as d:
			make_deb(f"{d}/m_1_ppc64.deb", "ps3rsx-modules-ps3", "0.874+k6.12+ps3.855", source="ps3rsx")
			make_deb(f"{d}/md_1_ppc64.deb", "ps3rsx-modules-ps3-debug", "0.874+k6.12+ps3.855.debug2026", source="ps3rsx")
			make_deb(f"{d}/e_1_ppc64.deb", "e", "1:2.0-1")
			os.makedirs(f"{d}/src")
			name = bp.output_name("testing", "ps3rsx", d)
		self.assertEqual(name, "walnut-testing@ps3rsx@e=1%2.0-1,ps3rsx=0.874+k6.12+ps3.855.debug2026")
		self.assertEqual(bp.parse_name(name), ("testing", "ps3rsx", {"e": "1:2.0-1", "ps3rsx": "0.874+k6.12+ps3.855.debug2026"}))
		self.assertIsNone(bp.parse_name("debs-ps3rsx-testing"))	# the old per-package publish's artifacts

	def test_plan(self):
		arts = [art(1, "walnut-testing@foo@foo=1.0+ps3.9"),
		        art(2, "walnut-testing@foo@foo=1.0+ps3.10", created="2026-10-26T09:00:00Z"),	# newer version wins
		        art(3, "walnut-testing@bar@bar=2", created="2026-10-26T09:00:00Z"),
		        art(4, "walnut-testing@bar@bar=2", created="2026-10-26T11:00:00Z"),	# same version: newer artifact
		        art(5, "walnut-testing@pub@pub=3"),	# published
		        art(6, "walnut-testing@old@old=1"),	# testing has a newer one
		        art(7, "walnut-testing@br@br=1", branch="topic"),	# not main
		        art(8, "walnut-testing@ex@ex=1", expired=True),
		        art(9, "walnut-stable@st@st=1"),	# stable publishes from its own run
		        art(10, "debs-foo-testing"),
		        art(11, "walnut-testing@fork@fork=9", head_repo=FORK_ID),	# a fork's pull request, its branch "main"
		        dict(art(12, "walnut-testing@norepo@norepo=1"), workflow_run={"id": 9012, "head_branch": "main"})]
		todo, skipped = bp.plan(arts, "testing", {"pub": "3", "old": "2", "foo": "1.0+ps3.9"})
		self.assertEqual([(p, a["id"]) for p, _, a in todo], [("bar", 4), ("foo", 2)])
		self.assertEqual(dict(skipped), {"pub": "published", "old": "superseded by old 2"})

	def test_accept_only_this_repositorys_main_runs(self):
		self.assertIsNone(bp.run_refusal(run_record(1), REPO_ID))
		self.assertIsNone(bp.run_refusal(run_record(1, event="workflow_dispatch"), REPO_ID))
		for run, why in ((run_record(1, event="pull_request"), "not a push"),
		                 (run_record(1, event="pull_request", head_repo=FORK_ID), "not a push"),
		                 (run_record(1, event="push", head_repo=FORK_ID), "another repository"),
		                 (run_record(1, branch="topic"), "not on main"),
		                 (run_record(1, sha="main"), "no commit"),
		                 (dict(run_record(1), head_repository=None), "another repository")):
			self.assertIn(why, bp.run_refusal(run, REPO_ID) or "", run)
		self.assertIn("another repository", bp.run_refusal(run_record(1), None))

	def test_chunks(self):
		todo = [(p, {}, art(i, f"walnut-testing@{p}@{p}=1", size=s))
		        for i, (p, s) in enumerate((("a", 2), ("b", 5), ("c", 1), ("d", 9), ("e", 3)))]
		self.assertEqual([[t[0] for t in c] for c in bp.chunks(todo, 6)], [["d"], ["b"], ["e", "a", "c"]])


class FakeGitHub(http.server.BaseHTTPRequestHandler):
	"""/repos/o/r/actions/artifacts (paged), .../artifacts/N/zip (a redirect to the blob store, which
	refuses the token, as GitHub's does), the repository, its runs (runs: a push on main unless set),
	compare (main contains a commit unless compare says otherwise) and workflow dispatches (recorded)."""
	artifacts, blobs, dispatched, runs, compare, deleted = [], {}, [], {}, {}, []

	def log_message(self, *a):
		pass

	def reply(self, code, body=b"", ctype="application/json", headers=()):
		self.send_response(code)
		for k, v in headers:
			self.send_header(k, v)
		self.send_header("Content-Type", ctype)
		self.send_header("Content-Length", str(len(body)))
		self.end_headers()
		self.wfile.write(body)

	def do_GET(self):
		p = self.path
		if p.startswith("/blob/"):
			if self.headers.get("Authorization"):
				return self.reply(400, b"no token here")
			return self.reply(200, self.blobs[int(p.split("/")[2])], "application/zip")
		if self.headers.get("Authorization") != "Bearer t0ken":
			return self.reply(401)
		if p.startswith("/repos/o/r/actions/artifacts?"):
			page = int(urllib.parse.parse_qs(p.split("?", 1)[1])["page"][0])
			return self.reply(200, json.dumps({"artifacts": self.artifacts[(page - 1) * 100:page * 100]}).encode())
		if p == "/repos/o/r":
			return self.reply(200, json.dumps({"id": REPO_ID, "full_name": "o/r"}).encode())
		if p.startswith("/repos/o/r/actions/runs/"):
			n = int(p.split("/")[6])
			return self.reply(200, json.dumps(self.runs.get(n) or run_record(n)).encode())
		if p.startswith("/repos/o/r/compare/"):
			sha = p.split("/")[5].split("...")[0]
			return self.reply(200, json.dumps({"status": self.compare.get(sha, "ahead")}).encode())
		if p.startswith("/repos/o/r/actions/artifacts/") and p.endswith("/zip"):
			return self.reply(302, headers=[("Location", f"http://{self.headers['Host']}/blob/{p.split('/')[6]}")])
		self.reply(404)

	def do_DELETE(self):
		aid = int(self.path.split("/")[6])
		self.deleted.append(aid)
		FakeGitHub.artifacts = [a for a in self.artifacts if a["id"] != aid]
		self.reply(204)

	def do_POST(self):
		body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
		self.dispatched.append((self.path.split("/")[6], body))
		self.reply(204)


class Quiet(http.server.SimpleHTTPRequestHandler):
	def log_message(self, *a):
		pass


UPLOAD = """#!/bin/bash
# the test's walnut-upload.sh: the bundle into incoming/ as walnut-upload.sh drops it, then the watcher
set -e
kind=$1 id=$2 dir=$3
[ ! -e "$WALNUT_ROOT/refuse" ] || { printf 'refused\\ntest: refused\\n' > "$WALNUT_ROOT/www/status/${kind}__$id"; exit 0; }
cp -r "$dir" "$WALNUT_ROOT/incoming/$kind/$id"
(cd "$dir" && find . -type f -printf '%P\\0' | LC_ALL=C sort -z | xargs -0r sha256sum) > "$WALNUT_ROOT/incoming/$kind/$id.ready"
echo "$id" >> "$WALNUT_ROOT/uploads"
WALNUT_NO_DOCKER=1 "$WATCH" >/dev/null
"""


class Runs(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.tmp = tempfile.TemporaryDirectory()
		t = cls.tmp.name
		cls.gpg = f"{t}/gpg"
		os.makedirs(cls.gpg, 0o700)
		gpg = ["gpg", "--homedir", cls.gpg, "--batch"]
		subprocess.run(gpg + ["--passphrase", "", "--quick-gen-key", "Walnut test ci <ci@test.invalid>", "ed25519",
		                      "sign", "never"], check=True, capture_output=True)
		cls.ring = f"{t}/ci.gpg"
		with open(cls.ring, "wb") as f:
			f.write(subprocess.run(gpg + ["--export"], check=True, capture_output=True).stdout)
		cls.secret = subprocess.run(gpg + ["--armor", "--export-secret-keys"], check=True, capture_output=True,
		                            text=True).stdout
		cls.upload = f"{t}/upload.sh"
		with open(cls.upload, "w") as f:
			f.write(UPLOAD)
		os.chmod(cls.upload, 0o755)
		global MAIN_SHA, OLD_SHA
		cls.repo = f"{t}/repo"
		MAIN_SHA = fixture_repo(cls.repo)
		OLD_SHA = fixture_repo.old
		cls.gh = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FakeGitHub)
		threading.Thread(target=cls.gh.serve_forever, daemon=True).start()

	@classmethod
	def tearDownClass(cls):
		cls.gh.shutdown()
		cls.gh.server_close()
		cls.tmp.cleanup()

	def setUp(self):
		self.root = tempfile.mkdtemp(dir=self.tmp.name)
		for k in ("apt", "site", "download", "licensed"):
			os.makedirs(f"{self.root}/incoming/{k}")
		os.makedirs(f"{self.root}/www/apt")
		os.makedirs(f"{self.root}/www/status")
		os.makedirs(f"{self.root}/keys")
		shutil.copy(self.ring, f"{self.root}/keys/testing-keyring.gpg")
		handler = functools.partial(Quiet, directory=f"{self.root}/www")
		self.www = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
		threading.Thread(target=self.www.serve_forever, daemon=True).start()
		self.addCleanup(self.www.server_close)
		self.addCleanup(self.www.shutdown)
		self.url = f"http://127.0.0.1:{self.www.server_port}"
		FakeGitHub.artifacts, FakeGitHub.blobs, FakeGitHub.dispatched, FakeGitHub.runs, FakeGitHub.compare = \
			[], {}, [], {}, {}
		FakeGitHub.deleted = []
		self.n = 0

	def build(self, pkg, version, debs=None, created=None, size=None, run=None, sha=None, src_extra=None, dsc_lists=None,
	          as_name=None, dsc_text=None):
		"""A package build's artifact: its debs (name, package, source) and a source package."""
		self.n += 1
		d = f"{self.root}/build{self.n}"
		os.makedirs(f"{d}/src")
		for fname, package, source, *kw in debs or [(f"{pkg}_{version}_ppc64.deb", pkg, None),
		                                              (f"{pkg}-dbgsym_{version}_ppc64.deb", f"{pkg}-dbgsym", pkg)]:
			make_deb(f"{d}/{fname}", package, version, source=source, **(kw[0] if kw else {}))
		for fname, data in (src_extra or {}).items():
			with open(f"{d}/{fname}", "wb") as f:
				f.write(data)
		orig = f"{d}/src/{pkg}_{version}.tar.xz"
		data = f"{pkg} {version}".encode()
		with tarfile.open(orig, "w:xz") as t:	# a real tarball: leak-check reads it
			ti = tarfile.TarInfo(f"{pkg}/README")
			ti.size = len(data)
			t.addfile(ti, io.BytesIO(data))
		with open(orig, "rb") as f:
			data = f.read()
		h, md5, size_o = hashlib.sha256(data).hexdigest(), hashlib.md5(data).hexdigest(), len(data)
		listed = dsc_lists or os.path.basename(orig)
		with open(f"{d}/src/{pkg}_{version}.dsc", "w" if dsc_text is None else "wb") as f:
			if dsc_text is not None:
				f.write(dsc_text)
				f = open(os.devnull, "w")
			f.write(f"Format: 3.0 (native)\nSource: {pkg}\nVersion: {version}\nArchitecture: any\n"
			        f"Files:\n {md5} {size_o} {listed}\nChecksums-Sha256:\n {h} {size_o} {listed}\n")
		name = as_name or bp.output_name("testing", pkg, d)
		z = io.BytesIO()
		with zipfile.ZipFile(z, "w") as zf:
			for dirpath, _, files in os.walk(d):
				for f in files:
					zf.write(os.path.join(dirpath, f), os.path.relpath(os.path.join(dirpath, f), d))
		aid = 100 + self.n
		FakeGitHub.blobs[aid] = z.getvalue()
		FakeGitHub.artifacts.insert(0, art(aid, name, created=created or f"2026-10-26T10:{self.n:02d}:00Z",
		                                   size=size or len(z.getvalue())))
		FakeGitHub.runs[9000 + aid] = run_record(9000 + aid, sha=sha, **{"path": f".github/workflows/pkg-{pkg}.yml", **(run or {})})
		return name

	def run_publisher(self, *args, ok=True, cmd="publish", env=None):
		env = dict(os.environ, GITHUB_TOKEN="t0ken", GITHUB_REPOSITORY="o/r",
		           GITHUB_API_URL=f"http://127.0.0.1:{self.gh.server_port}", GITHUB_SHA="abcdef0123456789",
		           WALNUT_APT_URL=f"{self.url}/apt", WALNUT_STATUS_URL=f"{self.url}/status",
		           WALNUT_SIGN_KEY=self.secret, WALNUT_DEPLOY_KEY_DATA="unused", WALNUT_UPLOAD=self.upload,
		           WALNUT_ROOT=self.root, WATCH=WATCH, WALNUT_KEYRING=self.ring, WALNUT_GIT_DIR=self.repo, **(env or {}))
		r = subprocess.run([SCRIPT, cmd, "--work", f"{self.root}/work", *args], env=env,
		                   capture_output=True, text=True)
		if os.environ.get("TDEBUG"):
			print(r.stdout + r.stderr)
		if ok is not None:
			self.assertEqual(r.returncode == 0, ok, r.stdout + r.stderr)
		return r.stdout + r.stderr

	def uploads(self):
		p = f"{self.root}/uploads"
		if not os.path.exists(p):
			return []
		with open(p) as f:
			return f.read().split()

	def packages(self, suite="testing"):
		with open(f"{self.root}/www/apt/dists/{suite}/main/binary-ppc64/Packages") as f:
			return f.read()

	def test_runs(self):
		self.build("foo", "1.0+ps3.9")
		self.build("bar", "2")
		self.build("linux", "6.12-1+ps3.9", debs=[("k_1_ppc64.deb", "linux-image-6.12.1+ps3.9", "linux")])
		self.run_publisher()
		self.assertEqual(len(self.uploads()), 1)	# all three in one upload
		self.assertIn("batch3", self.uploads()[0])
		pk = self.packages()
		for p in ("Package: foo\n", "Package: bar\n", "Package: linux-image-6.12.1+ps3.9\n"):
			self.assertIn(p, pk)
		self.assertIn("Package: foo-dbgsym", self.packages("testing-debug"))
		self.assertTrue(os.path.exists(f"{self.root}/www/apt/pool/testing/main/f/foo/foo_1.0+ps3.9.dsc"))
		self.assertEqual(FakeGitHub.dispatched, [("pkg-ps3rsx.yml", {"ref": "main", "inputs": {"target": "testing"}})])
		# again: nothing new, nothing uploaded
		self.assertIn("nothing to publish", self.run_publisher())
		self.assertEqual(len(self.uploads()), 1)
		# an older unpublished build and a newer one of foo, a rebuild of bar's published version
		self.build("foo", "1.0+ps3.10")
		self.build("foo", "1.0+ps3.12")
		self.build("bar", "2")
		out = self.run_publisher()
		self.assertIn("-- bar: published", out)
		self.assertEqual(len(self.uploads()), 2)
		pk = self.packages()
		self.assertIn("Version: 1.0+ps3.12\n", pk)
		self.assertNotIn("Version: 1.0+ps3.10\n", pk)	# superseded before it went out
		self.assertEqual(len(FakeGitHub.dispatched), 1)	# no new kernel
		# a refused upload leaves its builds for the next run; each package is tried on its own
		self.build("baz", "1")
		self.build("qux", "1")
		with open(f"{self.root}/refuse", "w"):
			pass
		out = self.run_publisher(ok=False)
		self.assertIn("each package on its own", out)
		self.assertNotIn("Package: baz\n", self.packages())
		os.unlink(f"{self.root}/refuse")
		self.run_publisher()
		self.assertIn("Package: baz\n", self.packages())
		self.assertIn("Package: qux\n", self.packages())
		self.assertIn("batch2", self.uploads()[-1])

	def test_accept_only_this_repositorys_main_runs(self):
		self.build("foo", "1")
		self.build("pr", "1", run={"event": "pull_request"})	# the artifact listing alone can't tell
		self.build("fork", "1", run={"event": "pull_request", "head_repo": FORK_ID})
		self.build("side", "1", sha=OTHER_SHA)
		FakeGitHub.compare[OTHER_SHA] = "diverged"	# a commit main doesn't contain
		self.build("ahead", "1", sha="c" * 40)
		FakeGitHub.compare["c" * 40] = "behind"
		out = self.run_publisher()
		for pkg in ("pr", "fork", "side", "ahead"):
			self.assertIn(f"!! {pkg}: artifact", out)
			self.assertNotIn(f"Package: {pkg}\n", self.packages())
		self.assertIn("Package: foo\n", self.packages())
		self.assertEqual(len(self.uploads()), 1)

	def test_size_bound(self):
		self.build("big", "1", size=5 << 30)
		self.build("small", "1", size=1 << 20)
		self.build("tiny", "1", size=1 << 20)
		self.run_publisher()
		self.assertEqual(len(self.uploads()), 2)	# big alone, then the others together
		self.assertIn("-big-", self.uploads()[0])
		self.assertIn("batch2", self.uploads()[1])
		self.assertIn("Package: tiny\n", self.packages())


	# --- CR1 + C3: a build publishes only what its package owns -------------------------------------

	def refused(self, pkg, out, text=None):
		self.assertIn(f"!! {pkg}: artifact", out)
		self.assertIn("refused", out)
		if text:
			self.assertIn(text, out)
		self.assertNotIn(f"Package: {pkg}\n", self.packages() if os.path.exists(
			f"{self.root}/www/apt/dists/testing/main/binary-ppc64/Packages") else "")

	def test_foreign_names_are_refused_the_rest_publishes(self):
		self.build("foo", "1.0")
		# a name another package owns, one in nobody's list, a reserved kernel name, a name under another Source
		cases = {"evil": [("linux-image-ps3_1_ppc64.deb", "linux-image-ps3", "evil")],
		         "badwolf": [("libc6_1_ppc64.deb", "libc6", "badwolf")],
		         "baz": [("bar_1_ppc64.deb", "bar", "baz")],
		         "qux": [("qux_1_ppc64.deb", "qux", "mesa")]}
		for pkg, debs in cases.items():
			self.build(pkg, "1", debs=debs)
		out = self.run_publisher("--delete-refused")
		for pkg, why in (("evil", "belongs to linux"), ("badwolf", "is in no package's list"),
		                 ("baz", "belongs to bar"), ("qux", "Source 'mesa'")):
			self.assertIn(f"!! {pkg}: artifact", out)
			self.assertIn(why, out)
		pk = self.packages()
		self.assertIn("Package: foo\n", pk)
		for n in ("linux-image-ps3", "libc6", "Package: bar\n"):
			self.assertNotIn(n, pk)
		self.assertEqual(len(FakeGitHub.deleted), 4)	# refused for what they hold: gone, not met again
		self.assertIn("nothing to publish", self.run_publisher("--delete-refused"))

	def test_a_name_in_the_suite_under_another_source(self):
		self.build("foo", "1.0")
		self.run_publisher()
		# bar's build now makes a deb named foo with Source bar: the name is foo's, and also in the suite as foo's
		self.build("bar", "1", debs=[("foo_9_ppc64.deb", "foo", "bar")])
		out = self.run_publisher()
		self.assertIn("!! bar: artifact", out)
		self.assertIn("belongs to foo", out)

	def test_wrong_workflow_and_no_list_and_claims(self):
		self.build("foo", "1")
		self.build("bar", "1", run={"path": ".github/workflows/pkg-foo.yml"})	# bar's artifact from foo's workflow
		self.build("baz", "1", run={"path": ".github/workflows/evil.yml"})
		self.build("qux", "1", sha=OLD_SHA)	# a commit with no packages.toml
		self.build("tiny", "1", as_name="walnut-testing@tiny@tiny=999")	# the name claims another version
		out = self.run_publisher("--delete-refused")
		self.assertIn("not pkg-bar.yml", out)
		self.assertIn("not pkg-baz.yml", out)
		self.assertIn("no github-ci/packages.toml at", out)
		self.assertIn("the name says", out)
		self.assertIn("Package: foo\n", self.packages())
		for n in ("bar", "baz", "qux", "tiny"):
			self.assertNotIn(f"Package: {n}\n", self.packages())
		# deleted: tiny (the claim its debs contradict); not bar and baz (a workflow path says nothing about the
		# contents: if GitHub changed its format every pending artifact would go), not qux (no list)
		self.assertEqual(FakeGitHub.deleted, [105])

	def test_strict_debs_and_dsc_and_unlisted_files(self):
		base = "Package: evil\nVersion: 1\nArchitecture: ppc64\nMaintainer: T <t@t>\nDescription: x\n"
		clean = base.replace("evil", "libc6")
		self.build("evil", "1", debs=[("e_1_ppc64.deb", "evil", None, {"control_tar": [
			("./control", base.encode()), ("./control", clean.encode())]})], as_name="walnut-testing@evil@evil=1")
		self.build("badwolf", "1", dsc_lists="/etc/passwd")	# a .dsc naming a path
		self.build("baz", "1", src_extra={"src/extra.sh": b"echo"})	# a file no .dsc lists
		self.build("qux", "1", debs=[("q_1_ppc64.deb", "qux", None, {"control_extra": "Filename: pool/x\n"})],
		           as_name="walnut-testing@qux@qux=1")
		self.build("foo", "1")
		out = self.run_publisher("--delete-refused")
		for pkg in ("evil", "badwolf", "baz", "qux"):
			self.assertIn(f"!! {pkg}: artifact", out)
		self.assertIn("not a plain, single, usual control file", out)
		self.assertIn("is not a plain file name", out)
		self.assertIn("files no .dsc lists", out)
		self.assertEqual(len(FakeGitHub.deleted), 4)
		self.assertIn("Package: foo\n", self.packages())

	def test_relations(self):
		def rel(pkg, **kw):
			self.build(pkg, "1", debs=[(f"{pkg}_1_ppc64.deb", pkg, None, {"control_extra": "".join(
				f"{k}: {v}\n" for k, v in kw.items())})])
		rel("badwolf", Replaces="libc6")	# an external package names a protected name
		rel("foo", Provides="sudo")	# so does a trusted one
		rel("bar", Breaks="foo")	# another package of ours
		rel("baz", Replaces="something-foreign")	# trusted, foreign, not listed: a warning for now
		rel("qux", Essential="yes")
		rel("tiny", Depends="libc6 (>= 2)")	# a Depends is no relation of the kind checked
		rel("small", Breaks="mawk")	# a protected name, but only Breaks: refused unless listed (not listed)
		out = self.run_publisher()
		for pkg in ("badwolf", "foo", "bar", "qux", "small"):
			self.assertIn(f"!! {pkg}: artifact", out)
		self.assertIn("is a protected name", out)
		self.assertIn("belongs to package foo", out)
		self.assertIn("warning (relations)", out)
		pk = self.packages()
		self.assertIn("Package: baz\n", pk)
		self.assertIn("Package: tiny\n", pk)
		for n in ("badwolf", "foo", "bar", "qux", "small"):
			self.assertNotIn(f"Package: {n}\n", pk)
		self.assertEqual(FakeGitHub.deleted, [])	# no --delete-refused

	def test_lists_at_both_commits(self):
		"""A name must be the package's at the run's commit and at HEAD; one owner per name."""
		repo = f"{self.tmp.name}/repo2-{self.n}"
		old = fixture_repo(repo)
		# HEAD: baz gives its name up (retired); foo is untouched
		toml = open(f"{repo}/github-ci/packages.toml").read()
		toml = toml.replace('binaries = ["baz"]', 'binaries = []\nretired = ["baz"]')
		open(f"{repo}/github-ci/packages.toml", "w").write(toml)
		commit_all(repo, "baz retires its name")
		self.repo = repo
		self.build("baz", "1", sha=old)
		self.build("foo", "1", sha=old)
		out = self.run_publisher()
		self.assertIn("is no longer baz's at HEAD", out)
		self.assertIn("Package: foo\n", self.packages())
		self.assertNotIn("Package: baz\n", self.packages())

	def test_toml_errors(self):
		bad = {"two owners": '[package.a]\ndir="x"\nsource="a"\ntier="trusted"\nbinaries=["n"]\n'
		                      '[package.b]\ndir="y"\nsource="b"\ntier="trusted"\nbinaries=["n"]\n',
		       "unknown key": '[package.a]\ndir="x"\nsource="a"\ntier="trusted"\nbinaries=[]\nbogus=1\n',
		       "bad tier": '[package.a]\ndir="x"\nsource="a"\ntier="great"\nbinaries=[]\n',
		       "pattern outside reserved": '[package.a]\ndir="x"\nsource="a"\ntier="trusted"\nbinaries=["n-@RELEASE@"]\n',
		       "unknown token": '[reserved]\n"n-" = "a"\n[package.a]\ndir="x"\nsource="a"\ntier="trusted"\nbinaries=["n-@ANY@"]\n',
		       "same source": '[package.a]\ndir="x"\nsource="s"\ntier="trusted"\nbinaries=["a"]\n'
		                      '[package.b]\ndir="y"\nsource="s"\ntier="trusted"\nbinaries=["b"]\n',
		       "reserved name elsewhere": '[reserved]\n"k-" = "a"\n[package.a]\ndir="x"\nsource="a"\ntier="trusted"\nbinaries=[]\n'
		                                  '[package.b]\ndir="y"\nsource="b"\ntier="trusted"\nbinaries=["k-x"]\n',
		       "overlapping patterns": '[reserved]\n"k-" = "a"\n"k-i-" = "b"\n[package.a]\ndir="x"\nsource="a"\ntier="trusted"\nbinaries=["k-@RELEASE@"]\n'
		                               '[package.b]\ndir="y"\nsource="b"\ntier="trusted"\nbinaries=["k-i-@RELEASE@"]\n',
		       "dbgsym listed": '[package.a]\ndir="x"\nsource="a"\ntier="trusted"\nbinaries=["a-dbgsym"]\n',
		       "not toml": "[[["}
		for why, text in bad.items():
			with self.assertRaises(bp.wl.ListError, msg=why):
				bp.wl.Lists(text if why == "not toml" else "version = 1\n" + text)

	def test_verify_then_publish_with_a_manifest(self):
		self.build("foo", "1")
		self.build("bar", "1")
		man = f"{self.root}/manifest.json"
		out = self.run_publisher("--manifest-out", man, cmd="verify")
		self.assertIn("verified: ", out)
		self.assertEqual(self.uploads(), [])	# verify publishes nothing
		ok = json.load(open(man))["ok"]
		self.assertEqual(len(ok), 2)
		# a manifest that doesn't match the bytes: nothing goes out for that artifact
		first = next(iter(ok.values()))
		first["files"][next(iter(first["files"]))] = "0" * 64
		json.dump({"ok": ok}, open(man, "w"))
		out = self.run_publisher("--manifest", man, ok=False)
		self.assertIn("not the ones the verify job checked", out)
		self.assertEqual(len(self.uploads()), 1)	# the other one went out
		published = self.packages()
		self.assertEqual(sum(f"Package: {n}\n" in published for n in ("foo", "bar")), 1)
		# an artifact the manifest doesn't know is left for the next run
		json.dump({"ok": {}}, open(man, "w"))
		out = self.run_publisher("--manifest", man)
		self.assertIn("was not verified", out)
		self.assertEqual(len(self.uploads()), 1)
		# the honest manifest publishes it
		self.run_publisher("--manifest-out", man, cmd="verify")
		self.run_publisher("--manifest", man)
		self.assertEqual(len(self.uploads()), 2)
		self.assertIn("Package: foo\n", self.packages())
		self.assertIn("Package: bar\n", self.packages())

	def test_check_output(self):
		"""build-package.sh's drift check and stable's strict check, on a directory."""
		self.build("foo", "1")
		d = f"{self.root}/build{self.n}"
		env = dict(os.environ, WALNUT_GIT_DIR=self.repo)
		r = subprocess.run([SCRIPT, "check-output", "foo", d], env=env, capture_output=True, text=True)
		self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
		r = subprocess.run([SCRIPT, "check-output", "--strict", "foo", d], env=env, capture_output=True, text=True)
		self.assertNotEqual(r.returncode, 0)	# strict needs dpkg-deb: this host has none
		self.assertIn("dpkg-deb is missing", r.stdout + r.stderr)
		env = dict(env, PATH=f"{self.fake_dpkg(honest=True)}:{os.environ['PATH']}")
		r = subprocess.run([SCRIPT, "check-output", "--strict", "foo", d], env=env, capture_output=True, text=True)
		self.assertNotEqual(r.returncode, 0)	# strict: the fixture's versions are not what the commit gives
		self.assertIn("(version)", r.stdout + r.stderr)
		env = dict(env, PATH=f"{self.fake_dpkg(honest=False)}:{os.environ['PATH']}")	# dpkg reads other fields than walnut-repo
		r = subprocess.run([SCRIPT, "check-output", "foo", d], env=env, capture_output=True, text=True)
		self.assertNotEqual(r.returncode, 0)
		self.assertIn("read different fields", r.stdout + r.stderr)
		make_deb(f"{d}/libc6_1_ppc64.deb", "libc6", "1")
		r = subprocess.run([SCRIPT, "check-output", "foo", d], env=env, capture_output=True, text=True)
		self.assertNotEqual(r.returncode, 0)

	def test_escaping_and_zip_bounds(self):
		self.assertNotIn("\n", bp.esc("a\n::stop-commands::x"))
		self.assertNotIn("\x1b", bp.esc("\x1b[31m"))
		gh = bp.GitHub.__new__(bp.GitHub)
		z = io.BytesIO()
		with zipfile.ZipFile(z, "w") as zf:
			for i in range(bp.MAX_ZIP_FILES + 1):
				zf.writestr(f"f{i}.deb", b"")
		import contextlib

		@contextlib.contextmanager
		def fake(path, **kw):
			yield io.BytesIO(z.getvalue())
		gh.request = fake
		with tempfile.TemporaryDirectory() as t:
			with self.assertRaises(bp.wr.Error):
				gh.download({"id": 1, "name": "x"}, f"{t}/d")


	def test_publish_sh_key_check_uploads_nothing(self):
		"""publish.sh --no-upload (the stable dry run): the signing key must sign for the suite's keyring, the
		upload key must parse; nothing is added or uploaded."""
		key = f"{self.root}/deploy"
		subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", key], check=True)
		self.build("foo", "1")
		d = f"{self.root}/build{self.n}"
		env = dict(os.environ, WALNUT_SIGN_KEY=self.secret, WALNUT_DEPLOY_KEY_DATA=open(key).read(),
		           WALNUT_KEYRING=self.ring, WALNUT_UPLOAD="/bin/false", WALNUT_ROOT=self.root)
		pub = f"{ROOT}/github-ci/publish.sh"
		r = subprocess.run([pub, "--no-upload", "testing", "foo", d], env=env, capture_output=True, text=True)
		self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
		self.assertIn("nothing was uploaded", r.stdout)
		self.assertEqual(self.uploads(), [])
		# another keyring: the key does not sign for it
		other = f"{self.root}/other-ring.gpg"
		home = f"{self.root}/gpg-other"
		os.makedirs(home, 0o700)
		subprocess.run(["gpg", "--homedir", home, "--batch", "--passphrase", "", "--quick-gen-key", "o <o@o.invalid>",
		                "ed25519", "sign", "never"], check=True, capture_output=True)
		open(other, "wb").write(subprocess.run(["gpg", "--homedir", home, "--export"], capture_output=True).stdout)
		r = subprocess.run([pub, "--no-upload", "testing", "foo", d], env=dict(env, WALNUT_KEYRING=other),
		                   capture_output=True, text=True)
		self.assertNotEqual(r.returncode, 0)
		self.assertIn("does not sign for", r.stderr)
		# an upload key that is not a key
		r = subprocess.run([pub, "--no-upload", "testing", "foo", d], env=dict(env, WALNUT_DEPLOY_KEY_DATA="nonsense"),
		                   capture_output=True, text=True)
		self.assertNotEqual(r.returncode, 0)

	def fake_dpkg(self, honest):
		"""A directory with a dpkg-deb that prints the fields (honest), or another Package (a differential)."""
		d = tempfile.mkdtemp(dir=self.root)
		path = f"{d}/dpkg-deb"
		with open(path, "w") as f:
			f.write(f"#!{sys.executable}\nimport importlib.machinery, importlib.util, sys\n"
			        f"sp = importlib.util.spec_from_loader('wr', importlib.machinery.SourceFileLoader('wr', '{ROOT}/apt-repo/walnut-repo.py'))\n"
			        "wr = importlib.util.module_from_spec(sp); sp.loader.exec_module(wr)\n"
			        "f = wr.deb_fields(sys.argv[2])\n"
			        f"if not {honest}: f['Package'] = 'libc6'\n"
			        "for k in sys.argv[3:]:\n\tif k in f: print(f'{k}: {f[k]}')\n")
		os.chmod(path, 0o755)
		return d

	def zip_artifact_with(self, pkg, member):
		"""build(), then the artifact's zip gets one more member."""
		name = self.build(pkg, "1")
		aid = 100 + self.n
		z = io.BytesIO(FakeGitHub.blobs[aid])
		with zipfile.ZipFile(z, "a") as zf:
			zf.writestr(member, b"x")
		FakeGitHub.blobs[aid] = z.getvalue()
		return name

	def test_one_odd_artifact_never_stops_the_rest(self):
		"""M-1: a zip member outside the name rules refuses that artifact (deleted); verify exits 0 and the others publish."""
		self.zip_artifact_with("bar", "src/a/b")
		self.build("foo", "1")
		man = f"{self.root}/m.json"
		out = self.run_publisher("--manifest-out", man, cmd="verify")
		self.assertIn("refused (zip)", out)
		self.assertEqual(list(json.load(open(man))["ok"]), ["102"])
		self.run_publisher("--manifest", man, "--delete-refused")
		self.assertIn("Package: foo\n", self.packages())
		self.assertEqual(FakeGitHub.deleted, [101])
		# only a refusal: a verdict, not a failure; publish then deletes it
		self.zip_artifact_with("baz", "evil.sh")
		self.run_publisher(cmd="verify")

	def test_malformed_inputs_refuse_the_artifact_not_the_run(self):
		"""M-2: an empty or Source-less .dsc, a corrupt control.tar, a bomb: refusals; foo still publishes."""
		self.build("foo", "1")
		bad = {"bar": ("control_member", ("control.tar.gz", b"not gzip")),
		       "baz": ("control_member", ("control.tar.xz", lzma.compress(b"\0" * (80 << 20)))),
		       "qux": ("control_member", ("control.tar.zst", b"\x28\xb5\x2f\xfd junk"))}
		for pkg, (k, v) in bad.items():
			self.build(pkg, "1", debs=[(f"{pkg}_1_ppc64.deb", pkg, None, {k: v})], as_name=f"walnut-testing@{pkg}@{pkg}=1")
		for pkg, dsc in (("tiny", b""), ("small", b"Version: 1\n")):
			self.build(pkg, "1", dsc_text=dsc, as_name=f"walnut-testing@{pkg}@{pkg}=1")
		out = self.run_publisher("--delete-refused")
		for pkg in ("bar", "baz", "qux", "tiny", "small"):
			self.assertIn(f"!! {pkg}: artifact", out)
		self.assertNotIn("Traceback", out)
		self.assertNotIn("internal error", out)
		self.assertIn("Package: foo\n", self.packages())

	def test_the_keys_stay_out_of_every_child_but_publish_sh(self):
		"""M-3: git, leak-check and the rest never see the signing or upload key; publish.sh still gets them."""
		bindir = tempfile.mkdtemp(dir=self.root)
		log = f"{self.root}/git-env.log"
		real = shutil.which("git")
		with open(f"{bindir}/git", "w") as f:
			f.write(f"#!/bin/bash\nenv | grep -E '^WALNUT_(SIGN|DEPLOY)' >> {log} || true\nexec {real} \"$@\"\n")
		os.chmod(f"{bindir}/git", 0o755)
		self.build("foo", "1")
		man = f"{self.root}/m.json"
		self.run_publisher("--manifest-out", man, cmd="verify", env={"PATH": f"{bindir}:{os.environ['PATH']}"})
		if os.path.exists(log):
			os.unlink(log)	# verify holds no secret in production; the test harness gives it the same env as publish
		self.run_publisher("--manifest", man, env={"PATH": f"{bindir}:{os.environ['PATH']}"})
		self.assertIn("Package: foo\n", self.packages())	# publish.sh had the key
		self.assertTrue(os.path.exists(f"{self.root}/uploads"))
		self.assertFalse(os.path.exists(log) and open(log).read().strip(), "a child saw the key")

	def test_leak_check_crash_keeps_the_artifact(self):
		"""M-4: only a LEAK: verdict deletes; a crash keeps the artifact, publishes nothing of it and fails visibly."""
		script = f"{self.root}/leak.py"
		self.build("foo", "1")
		self.build("bar", "1")
		open(script, "w").write("import sys\nif '/bar/' in ' '.join(sys.argv):\n\traise SystemExit(1)\n")
		out = self.run_publisher(env={"WALNUT_LEAK_CHECK": script}, ok=None)
		self.assertIn("(leak-error)", out)	# bar's run: exit 1 and no LEAK: line
		self.assertNotIn("Package: bar\n", self.packages())
		self.assertEqual(FakeGitHub.deleted, [])
		open(script, "w").write("import sys\nif '/bar/' in ' '.join(sys.argv):\n\tprint('LEAK: licensed package bar')\n\traise SystemExit(1)\n")
		out = self.run_publisher("--delete-refused", env={"WALNUT_LEAK_CHECK": script}, ok=None)
		self.assertIn("(leak)", out)
		self.assertEqual(FakeGitHub.deleted, [102])

	def test_relations_pin_and_important_need_both_lists(self):
		repo = f"{self.tmp.name}/repo3-{self.n}"
		old = fixture_repo(repo, extra="")
		toml = open(f"{repo}/github-ci/packages.toml").read()
		# at the old commit foo may relate to "wanted-pkg" and bar may move from "oldsrc"; HEAD drops both
		open(f"{repo}/github-ci/packages.toml", "w").write(toml.replace('[package.badwolf]\n', '[package.badwolf]\nrelations = ["wanted-pkg"]\n').replace(
			'[package.bar]\n', '[package.bar]\nmoved_from = ["oldsrc"]\n'))
		mid = commit_all(repo, "foo may relate")
		open(f"{repo}/github-ci/packages.toml", "w").write(toml)
		commit_all(repo, "and not any more")
		self.repo = repo
		self.build("badwolf", "1", sha=mid, debs=[("badwolf_1_ppc64.deb", "badwolf", None, {"control_extra": "Replaces: wanted-pkg\n"})])
		self.build("baz", "1", sha=old, debs=[("baz_1_ppc64.deb", "baz", None, {"control_extra": "Important: no\n"})])
		self.build("qux", "1", sha=old)
		out = self.run_publisher()
		self.assertIn("wanted-pkg", out)
		self.assertNotIn("Package: badwolf\n", self.packages())
		self.assertIn("Important field", out)
		self.assertNotIn("Package: baz\n", self.packages())

	def test_check_output_manifest_binds_the_key_job(self):
		"""L-5: stable's check job (dpkg-deb, leak-check) writes a manifest; the key job checks the same bytes with python only."""
		self.build("foo", "1")
		d = f"{self.root}/build{self.n}"
		man = f"{self.root}/stable-manifest.json"
		env = dict(os.environ, WALNUT_GIT_DIR=self.repo, PATH=f"{self.fake_dpkg(honest=True)}:{os.environ['PATH']}")
		run = lambda *args, e=env: subprocess.run([SCRIPT, "check-output", *args], env=e, capture_output=True, text=True)
		# the fixture's versions aren't the commit's: stable would refuse; the manifest steps are checked on a non-strict run
		r = run("--manifest-out", man, "foo", d)
		self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
		self.assertTrue(json.load(open(man))["files"])
		plain = dict(os.environ, WALNUT_GIT_DIR=self.repo)	# the key job: no dpkg-deb on its PATH is fine with a manifest
		r = run("--manifest", man, "foo", d, e=plain)
		self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
		with open(f"{d}/foo_1_ppc64.deb", "ab") as f:	# the bytes changed between the two jobs
			f.write(b"\0")
		r = run("--manifest", man, "foo", d, e=plain)
		self.assertNotEqual(r.returncode, 0)
		with open(f"{d}/foo_1_ppc64.deb", "r+b") as f:
			f.truncate(os.path.getsize(f"{d}/foo_1_ppc64.deb") - 1)
		self.assertEqual(run("--manifest", man, "foo", d, e=plain).returncode, 0)
		# strict without a manifest wants dpkg-deb
		r = run("--strict", "foo", d, e=plain)
		self.assertIn("dpkg-deb is missing", r.stdout + r.stderr)

if __name__ == "__main__":
	unittest.main()
