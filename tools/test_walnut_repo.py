#!/usr/bin/env python3
"""tools/walnut-repo.py against the real tools/walnut-server/walnut-watch: real .deb files, the
published tree served over local HTTP, throwaway keys (archive, CI). python3 tools/test_walnut_repo.py"""
import functools, hashlib, http.server, io, lzma, os, shutil, subprocess, tarfile, tempfile, threading, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = f"{HERE}/walnut-repo.py"
WATCH = f"{HERE}/walnut-server/walnut-watch"
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

	def add(self, files, suite="testing", keep=2):
		out = f"{self.root}/bundle-{next(UPLOAD_ID)}"
		self.repo("add", "--suite", suite, "--keyring", self.keys["ci"][2], "--sign-key", self.keys["ci"][1],
		          "--out", out, "--keep", str(keep), *files)
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

	def test_add_publish_prune_promote_fetch(self):
		d = self.debs
		v1 = [make_deb(f"{d}/foo_1.0+ps3.1_ppc64.deb", "foo", "1.0+ps3.1"),
		      make_deb(f"{d}/foo-dbgsym_1.0+ps3.1_ppc64.deb", "foo-dbgsym", "1.0+ps3.1", source="foo")]
		self.assertTrue(self.publish(self.add(v1)).startswith("ok"))
		self.assertIn("Package: foo-dbgsym", self.packages("testing"))
		# two more versions with keep=2: 1.0+ps3.1 goes
		for n in (2, 10):
			b = self.add([make_deb(f"{d}/foo_1.0+ps3.{n}_ppc64.deb", "foo", f"1.0+ps3.{n}")])
			self.assertTrue(self.publish(b).startswith("ok"))
		pk = self.packages("testing")
		self.assertNotIn("1.0+ps3.1\n", pk)
		self.assertIn("1.0+ps3.2", pk)
		self.assertIn("1.0+ps3.10", pk)	# 10 > 2: versions compare as Debian's
		# promote: the newest foo, without dbgsym; signed by the archive key
		v11 = [make_deb(f"{d}/foo_1.0+ps3.11_ppc64.deb", "foo", "1.0+ps3.11"),
		       make_deb(f"{d}/foo-dbgsym_1.0+ps3.11_ppc64.deb", "foo-dbgsym", "1.0+ps3.11", source="foo")]
		self.assertTrue(self.publish(self.add(v11)).startswith("ok"))
		out = f"{self.root}/promote"
		self.repo("promote", "--from-keyring", self.keys["ci"][2], "--suite", "stable", "--keyring",
		          self.keys["archive"][2], "--sign-key", self.keys["archive"][1], "--out", out, "--source", "foo",
		          who="archive")
		self.assertFalse(os.path.exists(f"{out}/pool"))	# the files are published already
		self.assertTrue(self.publish(out).startswith("ok"))
		st = self.packages("stable")
		self.assertIn("Version: 1.0+ps3.11", st)
		self.assertNotIn("dbgsym", st)
		# fetch: the newest version's debs, checked against the signed index
		to = f"{self.root}/fetched"
		self.repo("fetch", "--suite", "stable", "--keyring", self.keys["archive"][2], "--source", "foo", "--to", to)
		self.assertEqual(os.listdir(to), ["foo_1.0+ps3.11_ppc64.deb"])
		self.assertEqual(open(f"{to}/foo_1.0+ps3.11_ppc64.deb", "rb").read(), open(v11[0], "rb").read())

	def test_promote_skips_debug_only_version(self):
		d = self.debs
		debs = [make_deb(f"{d}/m_0.5+k6.1_ppc64.deb", "ps3rsx-modules-6.1", "0.5+k6.1", source="ps3rsx"),
		        make_deb(f"{d}/m_0.5+k6.1-debug2_ppc64.deb", "ps3rsx-modules-6.1-debug2", "0.5+k6.1-debug2",
		                 source="ps3rsx")]
		self.assertTrue(self.publish(self.add(debs)).startswith("ok"))
		out = f"{self.root}/promote"
		self.repo("promote", "--from-keyring", self.keys["ci"][2], "--suite", "stable", "--keyring",
		          self.keys["archive"][2], "--sign-key", self.keys["archive"][1], "--out", out, "--source", "ps3rsx",
		          who="archive")
		self.assertTrue(self.publish(out).startswith("ok"))
		st = self.packages("stable")
		self.assertIn("Package: ps3rsx-modules-6.1\n", st)
		self.assertNotIn("debug", st)

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
		p = f"{self.root}/www/apt/dists/testing/main/binary-ppc64/Packages"
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


if __name__ == "__main__":
	unittest.main()
