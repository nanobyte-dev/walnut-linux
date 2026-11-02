#!/usr/bin/env python3
"""apt-repo/walnut-repo.py against the real servers/walnut.nanobyte.dev/walnut-watch: real .deb files, the
published tree served over local HTTP, throwaway keys (archive, CI). python3 apt-repo/test_walnut_repo.py"""
import base64, functools, hashlib, http.server, io, lzma, os, shutil, subprocess, tarfile, tempfile, threading, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = f"{HERE}/walnut-repo.py"
WATCH = f"{HERE}/../servers/walnut.nanobyte.dev/walnut-watch"
UPLOAD_ID = iter(range(1, 1000))


def make_deb(path, package, version, source=None, arch="ppc64", payload=b"", control_extra="", control=None,
             control_tar=None, ar_extra=(), control_member=None):
	"""A .deb. payload: more bytes in the data tar (other bytes, same version). control: the whole control text
	(for crafted ones); control_tar: the control tar's members [(name, bytes)] instead of one ./control;
	ar_extra: more ar members [(name, bytes)] after data.tar.xz; control_member: (name, bytes) in place of the
	control.tar.xz member (corrupt, other compression, a bomb)."""
	if control is None:
		control = f"Package: {package}\nVersion: {version}\nArchitecture: {arch}\nMaintainer: T <t@t>\n"
		if source:
			control += f"Source: {source}\n"
		control += control_extra + "Description: test\n a test package\n"

	def tar_xz(files):
		b = io.BytesIO()
		with tarfile.open(fileobj=b, mode="w") as t:
			for name, data in files:
				ti = tarfile.TarInfo(name)
				ti.size = len(data)
				t.addfile(ti, io.BytesIO(data))
		return lzma.compress(b.getvalue())

	members = [("debian-binary", b"2.0\n"),
	           control_member or ("control.tar.xz", tar_xz(control_tar if control_tar is not None else [("./control", control.encode())])),
	           ("data.tar.xz", tar_xz([(f"./usr/share/doc/{package}/v", version.encode() + payload)])), *ar_extra]
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
		other = make_deb(f"{d}/x/data_2_all.deb", "data", "2", arch="all", payload=b"x")	# same version, other bytes
		_, r = add([other], ok=False)
		self.assertIn("already published with other bytes", r.stderr)

	def make_dsc(self, name, version, files, source="foo", listed=None):
		"""A .dsc in debs/src/ listing `files` ({name: bytes}); `listed` renames what the .dsc says."""
		d = f"{self.debs}/src"
		os.makedirs(d, exist_ok=True)
		rows = []
		for n, data in files.items():
			if "/" not in n:
				open(f"{d}/{n}", "wb").write(data)
			rows.append((n if listed is None else listed.get(n, n), data))
		path = f"{d}/{name}_{version}.dsc"
		with open(path, "w") as f:
			f.write(f"Format: 3.0 (native)\nSource: {source}\nVersion: {version}\nArchitecture: any\nFiles:\n")
			f.write("".join(f" {hashlib.md5(b).hexdigest()} {len(b)} {n}\n" for n, b in rows))
			f.write("Checksums-Sha256:\n" + "".join(f" {hashlib.sha256(b).hexdigest()} {len(b)} {n}\n" for n, b in rows))
		return path

	def test_dsc_names_are_plain_files(self):
		"""A .dsc's file names are never paths: /proc/self/environ and ../x would be copied into the pool."""
		secret = f"{self.root}/secret"
		open(secret, "w").write("WALNUT_SIGN_KEY=hunter2\n")
		for bad in (secret, "../x", "sub/x", "a\u00e9", "..", "x\0y"):
			dsc = self.make_dsc("foo", "1.0", {"orig.tar.xz": b"orig"}, listed={"orig.tar.xz": bad})
			r = self.add_fail([dsc])
			self.assertIn("plain file name", r.stderr, bad)
			for dp, _, fs in os.walk(f"{self.root}/bundle-out"):
				self.assertEqual(fs, [] if "pool" in dp else fs, bad)	# no pool file at all
		# the sha256 and the size are the .dsc's word
		dsc = self.make_dsc("foo", "1.0", {"orig.tar.xz": b"orig"})
		open(f"{self.debs}/src/orig.tar.xz", "wb").write(b"other")
		self.assertIn("size and sha256", self.add_fail([dsc]).stderr)
		# a symlink next to the .dsc is not a regular file
		os.unlink(f"{self.debs}/src/orig.tar.xz")
		os.symlink(secret, f"{self.debs}/src/orig.tar.xz")
		self.assertIn("regular file", self.add_fail([dsc]).stderr)
		# a Package: field would replace the Source in the index
		dsc = self.make_dsc("foo", "1.0", {"o.tar.xz": b"o"})
		with open(dsc, "a") as f:
			f.write("Package: libc6\n")
		self.assertIn("Package:", self.add_fail([dsc]).stderr)
		# a Source that is a path
		dsc = self.make_dsc("foo", "1.0", {"o.tar.xz": b"o"}, source="x/../../y")
		self.assertIn("bad Source", self.add_fail([dsc]).stderr)

	def test_dropped_dsc_leaves_no_files(self):
		"""A .dsc that prune drops from Sources gets no file in the pool (nothing unindexed is published)."""
		new = [self.make_dsc("foo", f"1.0+ps3.{n}", {f"o{n}.tar.xz": f"o{n}".encode()}) for n in (5, 6, 7)]
		old = self.make_dsc("foo", "0.1", {"old.tar.xz": b"old"})
		out = self.add(new + [old], keep=2)
		pool = os.path.join(out, "pool", "testing", "main", "f", "foo")
		self.assertFalse(os.path.exists(f"{pool}/old.tar.xz"))
		self.assertFalse(os.path.exists(f"{pool}/o5.tar.xz"))
		self.assertTrue(os.path.exists(f"{pool}/o7.tar.xz"))

	def add_fail(self, files, suite="testing"):
		out = f"{self.root}/bundle-out"
		return self.repo("add", "--suite", suite, "--keyring", self.keys["ci"][2], "--sign-key", self.keys["ci"][1],
		                 "--out", out, "--keep", "2", *files, ok=False)


	def test_strict_debs(self):
		"""What apt, dpkg and the checks see is one thing: crafted debs are refused (K2)."""
		d = self.debs
		base = "Package: badwolf\nVersion: 1\nArchitecture: ppc64\nMaintainer: T <t@t>\nDescription: x\n"
		clean = base.replace("badwolf", "libc6").replace("Version: 1", "Version: 99")
		crafted = {
			"dup field, other case": make_deb(f"{d}/a.deb", "badwolf", "1", control=base + "package: libc6\nversion: 99\n"),
			"dup field": make_deb(f"{d}/b.deb", "badwolf", "1", control=base + "Version: 99\n"),
			"two control members": make_deb(f"{d}/c.deb", "badwolf", "1", control_tar=[
				("./control", base.encode()), ("./control", clean.encode())]),
			"control and control": make_deb(f"{d}/c2.deb", "badwolf", "1", control_tar=[
				("./control", base.encode()), ("control", clean.encode())]),
			"unusual member": make_deb(f"{d}/d.deb", "badwolf", "1", control_tar=[
				("./control", base.encode()), ("./evil", b"x")]),
			"extra ar member": make_deb(f"{d}/e.deb", "badwolf", "1", ar_extra=[("control.tar.xz", b"x" * 4)]),
			"two stanzas": make_deb(f"{d}/f.deb", "badwolf", "1", control=base + "\n" + clean),
			"Filename field": make_deb(f"{d}/g.deb", "badwolf", "1", control_extra="Filename: pool/x\n"),
			"lower-case sha": make_deb(f"{d}/h.deb", "badwolf", "1", control_extra="sha256: 0\n"),
			"odd Source": make_deb(f"{d}/i.deb", "badwolf", "1", source="../../x"),
			"Source with junk": make_deb(f"{d}/j.deb", "badwolf", "1", source="mesa (99999) x"),
			"lower-case field name": make_deb(f"{d}/k.deb", "badwolf", "1", control=base.replace("Package:", "package:")),
		}
		for why, deb in crafted.items():
			r = self.repo("add", "--suite", "testing", "--keyring", self.keys["ci"][2], "--sign-key",
			              self.keys["ci"][1], "--out", f"{self.root}/o", deb, ok=False)
			self.assertNotEqual(r.returncode, 0, why)
			self.assertIn("!!", r.stderr, why)
		ok = make_deb(f"{d}/l.deb", "badwolf", "1")	# bytes after the last ar member
		with open(ok, "ab") as f:
			f.write(b"\n")
		r = self.repo("add", "--suite", "testing", "--keyring", self.keys["ci"][2], "--sign-key",
		              self.keys["ci"][1], "--out", f"{self.root}/o", ok, ok=False)
		self.assertNotEqual(r.returncode, 0)
		self.assertTrue(self.publish(self.add([make_deb(f"{d}/m.deb", "badwolf", "1")])).startswith("ok"))


	def test_names_and_copies(self):
		"""File names are plain on every path, and a file copied into the pool is the one that was hashed."""
		d = self.debs
		odd = make_deb(f"{d}/we ird_1_ppc64.deb", "weird", "1")
		r = self.repo("add", "--suite", "stable", "--keyring", self.keys["archive"][2], "--sign-key",
		              self.keys["archive"][1], "--out", f"{self.root}/o", odd, who="archive", ok=False)
		self.assertIn("not a plain file name", r.stderr)
		import importlib.machinery, importlib.util
		spec = importlib.util.spec_from_loader("wrmod", importlib.machinery.SourceFileLoader("wrmod", REPO))
		m = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(m)
		src = f"{self.root}/f"
		open(src, "wb").write(b"abc")
		m.copy_pool(f"{self.root}/pool", "a/f", src, hashlib.sha256(b"abc").hexdigest())
		with self.assertRaises(m.Error):
			m.copy_pool(f"{self.root}/pool", "a/g", src, hashlib.sha256(b"abd").hexdigest())
		self.assertFalse(os.path.exists(f"{self.root}/pool/a/g"))


	def test_dependency_fields_apt_can_parse(self):
		"""One value apt can't parse makes apt refuse the whole Packages file on every console (H-1)."""
		d = self.debs
		bad = {"Depends": ["foo bar", "foo (>= )", "foo (<< 1", "foo,, bar", "foo |", "Foo", "foo (~ 1)", "foo (>= 1 2)"],
		       "Breaks": ["foo (<< 1", "foo | bar", "foo bar"], "Conflicts": ["foo | bar"], "Replaces": ["foo | bar"],
		       "Provides": ["foo (>= 1)", "foo:any", "foo | bar", "foo bar"], "Built-Using": ["foo", "foo (>= 1)"],
		       "Pre-Depends": ["libc6 (>=2.0)x"], "Recommends": ["a (= 1) b"], "Suggests": ["(>= 1)"], "Enhances": ["a b"]}
		n = 0
		for field, values in bad.items():
			for v in values:
				n += 1
				deb = make_deb(f"{d}/r{n}.deb", "badwolf", "1", control_extra=f"{field}: {v}\n")
				r = self.repo("add", "--suite", "testing", "--keyring", self.keys["ci"][2], "--sign-key",
				              self.keys["ci"][1], "--out", f"{self.root}/o", deb, ok=False)
				self.assertNotEqual(r.returncode, 0, (field, v))
				self.assertIn("not a dependency apt can parse", r.stderr, (field, v))
		good = ("Depends: libc6 (>= 2.34), foo:any | bar (<< 2:1.0~rc1-1+b1),\n baz\n", "Provides: foo (= 1.0), bar\n",
		        "Breaks: old (<< 1.0), older\n", "Built-Using: gcc (= 14.2.0-1)\n", "Pre-Depends: x | y\n")
		for i, extra in enumerate(good):
			self.assertTrue(self.publish(self.add([make_deb(f"{d}/ok{i}.deb", f"okpkg{i}", "1", control_extra=extra)])).startswith("ok"), extra)


	def test_malformed_input_is_refused_not_a_crash(self):
		"""Empty or Source-less .dsc, corrupt or oversized control.tar, a .zst one: an error line, never a traceback (M-2)."""
		d = self.debs
		import gzip as gz
		cases = {
			"corrupt gzip": make_deb(f"{d}/a.deb", "badwolf", "1", control_member=("control.tar.gz", b"not gzip at all")),
			"truncated xz": make_deb(f"{d}/b.deb", "badwolf", "1", control_member=("control.tar.xz", lzma.compress(b"x" * 5000)[:30])),
			"xz bomb": make_deb(f"{d}/c.deb", "badwolf", "1", control_member=("control.tar.xz", lzma.compress(b"\0" * (80 << 20)))),
			"gzip bomb": make_deb(f"{d}/e.deb", "badwolf", "1", control_member=("control.tar.gz", gz.compress(b"\0" * (80 << 20), 1))),
			"corrupt zst": make_deb(f"{d}/f.deb", "badwolf", "1", control_member=("control.tar.zst", b"\x28\xb5\x2f\xfd garbage")),
			"not a tar": make_deb(f"{d}/g.deb", "badwolf", "1", control_member=("control.tar.xz", lzma.compress(b"hello"))),
			"unknown compression": make_deb(f"{d}/h.deb", "badwolf", "1", control_member=("control.tar.bz2", b"BZh")),
		}
		for why, deb in cases.items():
			r = self.repo("add", "--suite", "testing", "--keyring", self.keys["ci"][2], "--sign-key",
			              self.keys["ci"][1], "--out", f"{self.root}/o", deb, ok=False)
			self.assertEqual(r.returncode, 1, why)
			self.assertIn("!!", r.stderr, why)
			self.assertNotIn("Traceback", r.stderr, why)
		# a .dsc: empty, without Source, not text
		os.makedirs(f"{d}/src", exist_ok=True)
		for why, data in (("empty", b""), ("no Source", b"Version: 1\nFiles:\n"), ("binary", b"\xff\xfe\x00")):
			path = f"{d}/src/x_1.dsc"
			open(path, "wb").write(data)
			r = self.repo("add", "--suite", "testing", "--keyring", self.keys["ci"][2], "--sign-key",
			              self.keys["ci"][1], "--out", f"{self.root}/o", path, ok=False)
			self.assertEqual(r.returncode, 1, why)
			self.assertNotIn("Traceback", r.stderr, why)
		# a good zst control.tar is read (python's module or the zstd program) or refused cleanly, never a crash
		if shutil.which("zstd"):
			b = io.BytesIO()
			with tarfile.open(fileobj=b, mode="w") as t:
				c = b"Package: zpkg\nVersion: 1\nArchitecture: ppc64\nMaintainer: T <t@t>\nDescription: x\n"
				ti = tarfile.TarInfo("./control")
				ti.size = len(c)
				t.addfile(ti, io.BytesIO(c))
			z = subprocess.run(["zstd", "-q", "-c"], input=b.getvalue(), capture_output=True, check=True).stdout
			deb = make_deb(f"{d}/z.deb", "zpkg", "1", control_member=("control.tar.zst", z))
			self.assertTrue(self.publish(self.add([deb])).startswith("ok"))


	def test_pool_copies_are_plain_files_written_once(self):
		"""L-4: a link as an argument, and one pool path with two contents, are refused."""
		d = self.debs
		real = make_deb(f"{d}/real.deb", "linked", "1")
		os.symlink(real, f"{d}/linked_1_ppc64.deb")
		r = self.repo("add", "--suite", "testing", "--keyring", self.keys["ci"][2], "--sign-key",
		              self.keys["ci"][1], "--out", f"{self.root}/o", f"{d}/linked_1_ppc64.deb", ok=False)
		self.assertIn("not a regular file", r.stderr)
		# two source packages of one name sharing an orig tarball name with different bytes
		a = self.make_dsc("foo", "1.0", {"orig.tar.xz": b"one"})
		os.rename(a, f"{d}/foo_1.0.dsc")
		os.rename(f"{d}/src/orig.tar.xz", f"{d}/orig-one")
		os.makedirs(f"{d}/two")
		open(f"{d}/two/orig.tar.xz", "wb").write(b"two")
		open(f"{d}/two/foo_1.1.dsc", "w").write(open(f"{d}/foo_1.0.dsc").read().replace("1.0", "1.1").replace(
			hashlib.sha256(b"one").hexdigest(), hashlib.sha256(b"two").hexdigest()).replace(
			hashlib.md5(b"one").hexdigest(), hashlib.md5(b"two").hexdigest()))
		os.rename(f"{d}/orig-one", f"{d}/orig.tar.xz")
		r = self.repo("add", "--suite", "testing", "--keyring", self.keys["ci"][2], "--sign-key",
		              self.keys["ci"][1], "--out", f"{self.root}/o", f"{d}/foo_1.0.dsc", f"{d}/two/foo_1.1.dsc", ok=False)
		self.assertIn("two different files for one pool path", r.stderr)
		# the same orig shared by two versions is fine
		open(f"{d}/two/orig.tar.xz", "wb").write(b"one")
		open(f"{d}/two/foo_1.1.dsc", "w").write(open(f"{d}/foo_1.0.dsc").read().replace("1.0", "1.1"))
		self.repo("add", "--suite", "testing", "--keyring", self.keys["ci"][2], "--sign-key",
		          self.keys["ci"][1], "--out", f"{self.root}/o", f"{d}/foo_1.0.dsc", f"{d}/two/foo_1.1.dsc")


	def test_ar_sizes_and_concatenated_streams(self):
		"""R-1: a negative or odd ar size is refused at once (it used to seek back and loop forever); R-2: input after
		the first gzip member or xz stream is refused (dpkg reads further members, python's decompressors stop)."""
		d = self.debs
		import gzip as gz
		base = make_deb(f"{d}/ok.deb", "badwolf", "1")
		raw = open(base, "rb").read()
		# the data.tar member's header: size field at offset 48 of its 60 bytes
		i = raw.index(b"data.tar.xz")
		for why, field in (("negative", b"-60       "), ("sign", b"+10       "), ("hex", b"0x10      "), ("space first", b" 10       "),
		                   ("empty", b"          ")):
			bad = raw[:i + 48] + field + raw[i + 58:]
			open(f"{d}/bad.deb", "wb").write(bad)
			r = subprocess.run([REPO, "--url", self.url, "add", "--suite", "testing", "--keyring", self.keys["ci"][2],
			                    "--sign-key", self.keys["ci"][1], "--out", f"{self.root}/o", f"{d}/bad.deb"],
			                   env=dict(os.environ, GNUPGHOME=self.keys["ci"][0]), capture_output=True, text=True, timeout=20)
			self.assertEqual(r.returncode, 1, (why, r.stdout, r.stderr))
			self.assertIn("!!", r.stderr, why)
		# two members: the first complete (one stanza), the second another one
		def tarball(control):
			b = io.BytesIO()
			with tarfile.open(fileobj=b, mode="w") as t:
				ti = tarfile.TarInfo("./control")
				ti.size = len(control)
				t.addfile(ti, io.BytesIO(control))
			return b.getvalue()
		ctl = lambda name: f"Package: {name}\nVersion: 1\nArchitecture: ppc64\nMaintainer: T <t@t>\nDescription: x\n".encode()
		first, second = tarball(ctl("badwolf"))[:-1024], tarball(ctl("evilname"))	# the first without its end blocks
		cases = {"gzip": ("control.tar.gz", gz.compress(first, mtime=0) + gz.compress(second, mtime=0)),
		         "xz": ("control.tar.xz", lzma.compress(first) + lzma.compress(second))}
		if shutil.which("zstd"):
			z = lambda b: subprocess.run(["zstd", "-q", "-c"], input=b, capture_output=True, check=True).stdout
			cases["zstd"] = ("control.tar.zst", z(first) + z(second))
		for why, member in cases.items():
			deb = make_deb(f"{d}/cat-{why}.deb", "badwolf", "1", control_member=member)
			r = self.repo("add", "--suite", "testing", "--keyring", self.keys["ci"][2], "--sign-key",
			              self.keys["ci"][1], "--out", f"{self.root}/o", deb, ok=False)
			self.assertEqual(r.returncode, 1, (why, r.stdout, r.stderr))
			self.assertNotIn("Traceback", r.stderr, why)

if __name__ == "__main__":
	unittest.main()
