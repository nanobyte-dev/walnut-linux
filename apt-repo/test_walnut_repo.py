#!/usr/bin/env python3
"""apt-repo/walnut-repo.py against the real servers/walnut.nanobyte.dev/walnut-watch: real .deb files, the
published tree served over local HTTP, throwaway keys (archive, CI). python3 apt-repo/test_walnut_repo.py"""
import base64, functools, hashlib, http.server, io, lzma, os, shutil, subprocess, tarfile, tempfile, threading, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = f"{HERE}/walnut-repo.py"
WATCH = f"{HERE}/../servers/walnut.nanobyte.dev/walnut-watch"
UPLOAD_ID = iter(range(1, 1000))


def make_deb(path, package, version, source=None, arch="ppc64"):
	control = f"Package: {package}\nVersion: {version}\nArchitecture: {arch}\nMaintainer: T <t@t>\n"
	if source:
		control += f"Source: {source}\n"
	control += "Description: test\n a test package\n"

	def tar_xz(files):
		b = io.BytesIO()
		with tarfile.open(fileobj=b, mode="w") as t:
			for name, data in files.items():
				ti = tarfile.TarInfo(name)
				ti.size = len(data)
				t.addfile(ti, io.BytesIO(data))
		return lzma.compress(b.getvalue())

	members = [("debian-binary", b"2.0\n"), ("control.tar.xz", tar_xz({"./control": control.encode()})),
	           ("data.tar.xz", tar_xz({f"./usr/share/doc/{package}/v": version.encode()}))]
	with open(path, "wb") as f:
		f.write(b"!<arch>\n")
		for name, data in members:
			f.write(f"{name:<16}{0:<12}{0:<6}{0:<6}{'100644':<8}{len(data):<10}`\n".encode())
			f.write(data + (b"\n" if len(data) & 1 else b""))
	return path


class Repo(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.tmp = tempfile.TemporaryDirectory()
		t = cls.tmp.name
		cls.keys = {}
		for who in ("archive", "ci"):
			home = f"{t}/gpg-{who}"
			os.makedirs(home, 0o700)
			subprocess.run(["gpg", "--homedir", home, "--batch", "--passphrase", "", "--quick-gen-key",
			                f"Walnut test {who} <{who}@test.invalid>", "ed25519", "sign", "never"],
			               check=True, capture_output=True)
			fpr = subprocess.run(["gpg", "--homedir", home, "--with-colons", "--list-keys"], capture_output=True,
			                     text=True).stdout.split("fpr:::::::::")[1].split(":")[0]
			ring = f"{t}/{who}.gpg"
			with open(ring, "wb") as f:
				f.write(subprocess.run(["gpg", "--homedir", home, "--export"], capture_output=True).stdout)
			cls.keys[who] = (home, fpr, ring)

	@classmethod
	def tearDownClass(cls):
		cls.tmp.cleanup()

	def setUp(self):
		self.root = tempfile.mkdtemp(dir=self.tmp.name)
		for k in ("site", "download", "apt", "licensed"):
			os.makedirs(f"{self.root}/incoming/{k}")
		os.makedirs(f"{self.root}/www/apt")
		os.makedirs(f"{self.root}/keys")
		shutil.copy(self.keys["archive"][2], f"{self.root}/keys/archive-keyring.gpg")
		shutil.copy(self.keys["ci"][2], f"{self.root}/keys/testing-keyring.gpg")
		handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=f"{self.root}/www/apt")
		handler.log_message = lambda *a: None
		self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
		threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
		self.url = f"http://127.0.0.1:{self.httpd.server_port}"
		self.debs = f"{self.root}/debs"
		os.makedirs(self.debs)

	def tearDown(self):
		self.httpd.shutdown()

	def repo(self, *args, who="ci", ok=True):
		env = dict(os.environ, GNUPGHOME=self.keys[who][0])
		r = subprocess.run([REPO, "--url", self.url, *args], env=env, capture_output=True, text=True)
		if ok:
			self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
		return r

	def add(self, files, suite="testing", keep=2, debug_keep=2, release_only=False):
		out = f"{self.root}/bundle-{next(UPLOAD_ID)}"
		who = "ci" if suite == "testing" else "archive"
		self.repo("add", "--suite", suite, "--keyring", self.keys[who][2], "--sign-key", self.keys[who][1],
		          "--out", out, "--keep", str(keep), "--debug-keep", str(debug_keep),
		          *(["--release-only"] if release_only else []), *files, who=who)
		return out

	def publish(self, bundle, kind="apt"):
		"""Drops the bundle the way walnut-upload.sh does and runs the watcher; its result."""
		bid = f"u{next(UPLOAD_ID)}"
		shutil.copytree(bundle, f"{self.root}/incoming/{kind}/{bid}")
		with open(f"{self.root}/incoming/{kind}/{bid}.ready", "w") as m:
			for d, _, fs in os.walk(bundle):
				for f in fs:
					p = os.path.join(d, f)
					m.write(f"{hashlib.sha256(open(p, 'rb').read()).hexdigest()}  {os.path.relpath(p, bundle)}\n")
		r = subprocess.run([WATCH], env=dict(os.environ, WALNUT_ROOT=self.root, WALNUT_NO_DOCKER="1"),
		                   capture_output=True, text=True)
		self.assertEqual(r.returncode, 0, r.stderr)
		return open(f"{self.root}/state/done/{kind}__{bid}").read()

	def packages(self, suite):
		return open(f"{self.root}/www/apt/dists/{suite}/main/binary-ppc64/Packages").read()

	def test_testing_symbols_prune_fetch(self):
		d = self.debs
		v1 = [make_deb(f"{d}/foo_1.0+ps3.1_ppc64.deb", "foo", "1.0+ps3.1"),
		      make_deb(f"{d}/foo-dbgsym_1.0+ps3.1_ppc64.deb", "foo-dbgsym", "1.0+ps3.1", source="foo")]
		self.assertTrue(self.publish(self.add(v1)).startswith("ok"))
		self.assertNotIn("dbgsym", self.packages("testing"))	# symbols: testing-debug, same bundle
		self.assertIn("Package: foo-dbgsym", self.packages("testing-debug"))
		self.assertIn("Filename: pool/testing-debug/main/f/foo/foo-dbgsym_1.0+ps3.1_ppc64.deb", self.packages("testing-debug"))
		# two more versions with keep=2: 1.0+ps3.1 goes
		for n in (2, 10):
			b = self.add([make_deb(f"{d}/foo_1.0+ps3.{n}_ppc64.deb", "foo", f"1.0+ps3.{n}")])
			self.assertTrue(self.publish(b).startswith("ok"))
		pk = self.packages("testing")
		self.assertNotIn("1.0+ps3.1\n", pk)
		self.assertIn("1.0+ps3.2", pk)
		self.assertIn("1.0+ps3.10", pk)	# 10 > 2: versions compare as Debian's
		self.assertIn("1.0+ps3.1\n", self.packages("testing-debug"))	# no new symbols: left as it was
		# fetch: the newest version's debs, checked against the signed index
		to = f"{self.root}/fetched"
		self.repo("fetch", "--suite", "testing", "--keyring", self.keys["ci"][2], "--source", "foo", "--to", to)
		self.assertEqual(os.listdir(to), ["foo_1.0+ps3.10_ppc64.deb"])

	def test_stable_build(self):
		d = self.debs
		# the same version built for testing and, separately, for stable: other bytes, own pools
		os.makedirs(f"{d}/t")
		os.makedirs(f"{d}/s")
		self.assertTrue(self.publish(self.add([make_deb(f"{d}/t/foo_1_ppc64.deb", "foo", "1")])).startswith("ok"))
		st = [make_deb(f"{d}/s/foo_1_ppc64.deb", "foo", "1"),
		      make_deb(f"{d}/s/foo-dbgsym_1_ppc64.deb", "foo-dbgsym", "1", source="foo"),
		      make_deb(f"{d}/s/k_1_ppc64.deb", "linux-image-6.12+ps3.9-debug2026", "1", source="linux"),
		      make_deb(f"{d}/s/k-dbgsym_1_ppc64.deb", "linux-image-6.12+ps3.9-debug2026-dbgsym", "1", source="linux"),
		      make_deb(f"{d}/s/m_1_ppc64.deb", "ps3rsx-modules-ps3-debug", "1", source="ps3rsx"),
		      make_deb(f"{d}/s/r_1_ppc64.deb", "linux-image-6.12+ps3.9", "1", source="linux")]
		self.assertTrue(self.publish(self.add(st, suite="stable", keep=0, debug_keep=3, release_only=True)).startswith("ok"))
		stable = self.packages("stable")
		self.assertIn("Filename: pool/stable/main/f/foo/foo_1_ppc64.deb", stable)
		self.assertIn("Package: linux-image-6.12+ps3.9\n", stable)
		self.assertNotIn("debug", stable)
		sd = self.packages("stable-debug")
		self.assertIn("Package: foo-dbgsym", sd)
		self.assertNotIn("linux-image", sd)	# the debug kernel's symbols stay out with it
		self.assertTrue(os.path.exists(f"{self.root}/www/apt/pool/testing/main/f/foo/foo_1_ppc64.deb"))
		# the CI key can't publish stable
		out = f"{self.root}/bad"
		self.repo("add", "--suite", "stable", "--keyring", self.keys["archive"][2], "--sign-key", self.keys["ci"][1],
		          "--out", out, f"{d}/s/foo_1_ppc64.deb", who="ci")
		self.assertIn("not signed by the archive-keyring.gpg key", self.publish(out))

	def test_two_publishers(self):
		a = self.add([make_deb(f"{self.debs}/a_1_ppc64.deb", "a", "1")])
		b = self.add([make_deb(f"{self.debs}/b_1_ppc64.deb", "b", "1")])	# same base as a
		self.assertTrue(self.publish(a).startswith("ok"))
		self.assertIn("stale base", self.publish(b))
		b = self.add([f"{self.debs}/b_1_ppc64.deb"])	# again on the new state: keeps a
		self.assertTrue(self.publish(b).startswith("ok"))
		pk = self.packages("testing")
		self.assertIn("Package: a\n", pk)
		self.assertIn("Package: b\n", pk)

	def test_republish_same_version(self):
		deb = make_deb(f"{self.debs}/c_1_ppc64.deb", "c", "1")
		self.assertTrue(self.publish(self.add([deb])).startswith("ok"))
		again = self.add([deb])
		self.assertFalse(os.path.exists(f"{again}/pool"))	# nothing new
		self.assertTrue(self.publish(again).startswith("ok"))

	def test_tampered_published_index(self):
		self.assertTrue(self.publish(self.add([make_deb(f"{self.debs}/d_1_ppc64.deb", "d", "1")])).startswith("ok"))
		# the index the tool reads: by hash, as apt does (the plain name and its by-hash copy alike)
		d = f"{self.root}/www/apt/dists/testing/main/binary-ppc64"
		real = hashlib.sha256(open(f"{d}/Packages", "rb").read()).hexdigest()
		for p in (f"{d}/Packages", f"{d}/by-hash/SHA256/{real}"):
			os.chmod(p, 0o644)
			with open(p, "a") as f:
				f.write("\nPackage: evil\nVersion: 1\nFilename: pool/x\nSHA256: 00\n")
		r = self.repo("add", "--suite", "testing", "--keyring", self.keys["ci"][2], "--sign-key", self.keys["ci"][1],
		              "--out", f"{self.root}/x", f"{self.debs}/d_1_ppc64.deb", ok=False)
		self.assertIn("does not match the signed Release", r.stderr)

	def test_wrong_keyring_for_published(self):
		self.assertTrue(self.publish(self.add([make_deb(f"{self.debs}/e_1_ppc64.deb", "e", "1")])).startswith("ok"))
		r = self.repo("base", "--suite", "testing", "--keyring", self.keys["archive"][2], ok=False)
		self.assertIn("not signed by", r.stderr)

	def test_licensed_suite(self):
		"""The password-protected licensed suite (apt-repo/licensed-publish.sh): read with the login from
		WALNUT_APT_AUTH_FILE, signed with the archive key, every version kept, and a published version
		with other bytes refused (--strict)."""
		www = f"{self.root}/www/licensed"
		os.makedirs(www)

		class Auth(http.server.SimpleHTTPRequestHandler):
			def __init__(self, *a, **kw):
				super().__init__(*a, directory=www, **kw)

			def log_message(self, *a):
				pass

			def do_GET(self):
				if self.headers.get("Authorization") != "Basic " + base64.b64encode(b"walnut:secret").decode():
					self.send_response(401)
					self.end_headers()
					return
				super().do_GET()
		httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Auth)
		threading.Thread(target=httpd.serve_forever, daemon=True).start()
		self.addCleanup(httpd.shutdown)
		url = f"http://127.0.0.1:{httpd.server_port}"
		auth = f"{self.root}/auth"
		with open(auth, "w") as f:
			f.write("walnut:secret\n")
		home, fpr, ring = self.keys["archive"]

		def add(files, with_auth=True, ok=True):
			out = f"{self.root}/bundle-{next(UPLOAD_ID)}"
			env = dict(os.environ, GNUPGHOME=home)
			if with_auth:
				env["WALNUT_APT_AUTH_FILE"] = auth
			r = subprocess.run([REPO, "--url", url, "add", "--suite", "licensed", "--keyring", ring, "--sign-key", fpr,
			                    "--strict", "--out", out, *files], env=env, capture_output=True, text=True)
			if ok:
				self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
			return out, r
		d = self.debs
		os.makedirs(f"{d}/x")
		b, _ = add([make_deb(f"{d}/data_1_all.deb", "data", "1", arch="all")])
		self.assertTrue(self.publish(b, kind="licensed").startswith("ok"))
		b, _ = add([make_deb(f"{d}/data_2_all.deb", "data", "2", arch="all")])
		self.assertTrue(self.publish(b, kind="licensed").startswith("ok"))
		pk = open(f"{www}/dists/licensed/main/binary-ppc64/Packages").read()
		self.assertIn("Version: 1\n", pk)	# kept: no --keep
		self.assertIn("Filename: pool/licensed/main/d/data/data_2_all.deb", pk)
		_, r = add([f"{d}/data_2_all.deb"], with_auth=False, ok=False)	# the suite needs its login
		self.assertNotEqual(r.returncode, 0)
		self.assertIn("401", r.stderr)
		other = make_deb(f"{d}/x/data_2_all.deb", "data", "2", arch="all")
		with open(other, "ab") as f:	# the same version, other bytes
			f.write(b"\n")
		_, r = add([other], ok=False)
		self.assertIn("already published with other bytes", r.stderr)


if __name__ == "__main__":
	unittest.main()
