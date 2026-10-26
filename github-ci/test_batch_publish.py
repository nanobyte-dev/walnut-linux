#!/usr/bin/python3 -I
"""github-ci/batch-publish.py, testing's one publisher: its plan (what supersedes what, what is
published) and whole runs against a fake GitHub API, github-ci/publish.sh and the real walnut-watch
(the bundle dropped into incoming/ the way walnut-upload.sh drops it). Throwaway CI key.
  python3 github-ci/test_batch_publish.py"""
import functools, hashlib, http.server, importlib.machinery, importlib.util, io, json, os, shutil, subprocess
import sys, tempfile, threading, unittest, urllib.parse, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPT = f"{HERE}/batch-publish.py"
WATCH = f"{ROOT}/servers/walnut.nanobyte.dev/walnut-watch"
sys.path.insert(0, f"{ROOT}/apt-repo")
from test_walnut_repo import make_deb	# noqa: E402

_spec = importlib.util.spec_from_loader("batch_publish", importlib.machinery.SourceFileLoader("batch_publish", SCRIPT))
bp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bp)


def art(aid, name, created="2026-10-26T10:00:00Z", size=1000, branch="main", expired=False):
	return {"id": aid, "name": name, "created_at": created, "size_in_bytes": size, "expired": expired,
	        "workflow_run": {"id": 9000 + aid, "head_branch": branch}}


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
		        art(10, "debs-foo-testing")]
		todo, skipped = bp.plan(arts, "testing", {"pub": "3", "old": "2", "foo": "1.0+ps3.9"})
		self.assertEqual([(p, a["id"]) for p, _, a in todo], [("bar", 4), ("foo", 2)])
		self.assertEqual(dict(skipped), {"pub": "published", "old": "superseded by old 2"})

	def test_chunks(self):
		todo = [(p, {}, art(i, f"walnut-testing@{p}@{p}=1", size=s))
		        for i, (p, s) in enumerate((("a", 2), ("b", 5), ("c", 1), ("d", 9), ("e", 3)))]
		self.assertEqual([[t[0] for t in c] for c in bp.chunks(todo, 6)], [["d"], ["b"], ["e", "a", "c"]])


class FakeGitHub(http.server.BaseHTTPRequestHandler):
	"""/repos/o/r/actions/artifacts (paged), .../artifacts/N/zip (a redirect to the blob store, which
	refuses the token, as GitHub's does) and workflow dispatches (recorded)."""
	artifacts, blobs, dispatched = [], {}, []

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
		if p.startswith("/repos/o/r/actions/artifacts/") and p.endswith("/zip"):
			return self.reply(302, headers=[("Location", f"http://{self.headers['Host']}/blob/{p.split('/')[6]}")])
		self.reply(404)

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
		FakeGitHub.artifacts, FakeGitHub.blobs, FakeGitHub.dispatched = [], {}, []
		self.n = 0

	def build(self, pkg, version, debs=None, created=None, size=None):
		"""A package build's artifact: its debs (name, package, source) and a source package."""
		self.n += 1
		d = f"{self.root}/build{self.n}"
		os.makedirs(f"{d}/src")
		for fname, package, source in debs or [(f"{pkg}_{version}_ppc64.deb", pkg, None),
		                                         (f"{pkg}-dbgsym_{version}_ppc64.deb", f"{pkg}-dbgsym", pkg)]:
			make_deb(f"{d}/{fname}", package, version, source=source)
		orig = f"{d}/src/{pkg}_{version}.tar.xz"
		data = f"{pkg} {version}".encode()
		with open(orig, "wb") as f:
			f.write(data)
		h, md5, size_o = hashlib.sha256(data).hexdigest(), hashlib.md5(data).hexdigest(), len(data)
		with open(f"{d}/src/{pkg}_{version}.dsc", "w") as f:
			f.write(f"Format: 3.0 (native)\nSource: {pkg}\nVersion: {version}\nArchitecture: any\n"
			        f"Files:\n {md5} {size_o} {os.path.basename(orig)}\nChecksums-Sha256:\n {h} {size_o} {os.path.basename(orig)}\n")
		name = bp.output_name("testing", pkg, d)
		z = io.BytesIO()
		with zipfile.ZipFile(z, "w") as zf:
			for dirpath, _, files in os.walk(d):
				for f in files:
					zf.write(os.path.join(dirpath, f), os.path.relpath(os.path.join(dirpath, f), d))
		aid = 100 + self.n
		FakeGitHub.blobs[aid] = z.getvalue()
		FakeGitHub.artifacts.insert(0, art(aid, name, created=created or f"2026-10-26T10:{self.n:02d}:00Z",
		                                   size=size or len(z.getvalue())))
		return name

	def run_publisher(self, *args, ok=True):
		env = dict(os.environ, GITHUB_TOKEN="t0ken", GITHUB_REPOSITORY="o/r",
		           GITHUB_API_URL=f"http://127.0.0.1:{self.gh.server_port}", GITHUB_SHA="abcdef0123456789",
		           WALNUT_APT_URL=f"{self.url}/apt", WALNUT_STATUS_URL=f"{self.url}/status",
		           WALNUT_SIGN_KEY=self.secret, WALNUT_DEPLOY_KEY_DATA="unused", WALNUT_UPLOAD=self.upload,
		           WALNUT_ROOT=self.root, WATCH=WATCH, WALNUT_KEYRING=self.ring)
		r = subprocess.run([SCRIPT, "publish", "--work", f"{self.root}/work", *args], env=env,
		                   capture_output=True, text=True)
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
		self.build("linux", "6.12-1+ps3.9", debs=[("k_1_ppc64.deb", "linux-image-6.12+ps3.9", "linux")])
		self.run_publisher()
		self.assertEqual(len(self.uploads()), 1)	# all three in one upload
		self.assertIn("batch3", self.uploads()[0])
		pk = self.packages()
		for p in ("Package: foo\n", "Package: bar\n", "Package: linux-image-6.12+ps3.9\n"):
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

	def test_size_bound(self):
		self.build("big", "1", size=5 << 30)
		self.build("small", "1", size=1 << 20)
		self.build("tiny", "1", size=1 << 20)
		self.run_publisher()
		self.assertEqual(len(self.uploads()), 2)	# big alone, then the others together
		self.assertIn("-big-", self.uploads()[0])
		self.assertIn("batch2", self.uploads()[1])
		self.assertIn("Package: tiny\n", self.packages())


if __name__ == "__main__":
	unittest.main()
