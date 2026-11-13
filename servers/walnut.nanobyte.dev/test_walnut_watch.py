#!/usr/bin/env python3
"""Tests for walnut-watch: runs it against a temporary root (WALNUT_ROOT, no docker) with bundles
dropped the way walnut-upload.sh drops them, good ones and hostile ones.
  python3 servers/walnut.nanobyte.dev/test_walnut_watch.py"""
import gzip, hashlib, json, os, re, shutil, subprocess, sys, tempfile, time, unittest

WATCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "walnut-watch")
GPG = {}	# good, other: GNUPGHOMEs; keyring: the good key exported


def slurp(p, mode="r"):
	with open(p, mode) as f:
		return f.read()


def sha(b):
	return hashlib.sha256(b).hexdigest()


def fprs(who):
	"""[primary fingerprint, subkey fingerprints...] of a test key home"""
	out = subprocess.run(["gpg", "--homedir", GPG[who], "--list-keys", "--with-colons"], check=True,
	                     capture_output=True, text=True).stdout.splitlines()
	res = []
	for i, l in enumerate(out):
		if l.startswith(("pub:", "sub:")):
			res.append(next(x for x in out[i + 1:] if x.startswith("fpr:")).split(":")[9])
	return res


def setUpModule():
	GPG["tmp"] = tempfile.TemporaryDirectory()
	for who in ("good", "other", "ci"):
		home = f"{GPG['tmp'].name}/{who}"
		os.makedirs(home, 0o700)
		subprocess.run(["gpg", "--homedir", home, "--batch", "--passphrase", "", "--quick-gen-key",
		                f"Walnut test {who} <{who}@test.invalid>", "ed25519", "sign", "never"],
		               check=True, capture_output=True)
		GPG[who] = home
	# the archive key is a primary with a signing subkey, as the real one is: gpg signs with the newest signing
	# subkey unless told fpr!, so a licensed bundle signed without care carries the subkey's fingerprint
	subprocess.run(["gpg", "--homedir", GPG["good"], "--batch", "--passphrase", "", "--quick-add-key",
	                fprs("good")[0], "ed25519", "sign", "never"], check=True, capture_output=True)
	for who in ("good", "other", "ci"):
		GPG[who + "_fprs"] = fprs(who)
	for who, name in (("good", "keyring"), ("ci", "testing_keyring")):
		GPG[name] = f"{GPG['tmp'].name}/{name}.gpg"
		with open(GPG[name], "wb") as f:
			f.write(subprocess.run(["gpg", "--homedir", GPG[who], "--export"], check=True, capture_output=True).stdout)


def tearDownModule():
	GPG["tmp"].cleanup()


def sign(data, who="good", clear=False, key=None):
	"""key: "primary" or "sub" signs with exactly that key (fpr!); None lets gpg choose (the newest subkey)"""
	mode = ["--clearsign"] if clear else ["--detach-sign", "--armor"]
	pick = ["--local-user", GPG[who + "_fprs"][0 if key == "primary" else 1] + "!"] if key else []
	return subprocess.run(["gpg", "--homedir", GPG[who], "--batch", *pick, *mode], input=data,
	                      check=True, capture_output=True).stdout


def release(files, who="good", sums=None):
	"""A release directory: files, SHA256SUMS (listing them unless given) and its signature."""
	if sums is None:
		sums = "".join(f"{sha(d)}  {n}\n" for n, d in sorted(files.items())).encode()
	return {**files, "SHA256SUMS": sums, "SHA256SUMS.asc": sign(sums, who)}


class Watch(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.root = self.tmp.name
		for k in ("site", "download", "apt", "licensed"):
			os.makedirs(f"{self.root}/incoming/{k}")
		os.makedirs(f"{self.root}/www")
		os.makedirs(f"{self.root}/keys")
		shutil.copy(GPG["keyring"], f"{self.root}/keys/archive-keyring.gpg")
		shutil.copy(GPG["testing_keyring"], f"{self.root}/keys/testing-keyring.gpg")
		self.install_rules()
		os.makedirs(f"{self.root}/outside")
		with open(f"{self.root}/outside/secret", "w") as f:
			f.write("private\n")

	OWNERSHIP = """version = 1
[reserved]
"linux-image-" = "linux"

[package.foo]
dir = "packages/x/foo"
source = "foo"
tier = "trusted"
binaries = ["foo"]
relations = ["oldfoo"]

[package.bar]
dir = "packages/x/bar"
source = "bar"
tier = "trusted"
binaries = ["bar", "bar-data"]

[package.q]
dir = "packages/x/q"
source = "q"
tier = "licensed"
binaries = ["q"]

[package.linux]
dir = "packages/x/linux"
source = "linux"
tier = "trusted"
binaries = ["linux-image-@RELEASE@"]
"""

	def install_rules(self, ownership=None, pins=None):
		d = f"{self.root}/rules"
		os.makedirs(d, exist_ok=True)
		g, c = GPG["good_fprs"], GPG["ci_fprs"]
		with open(f"{d}/ownership.toml", "w") as f:
			f.write(ownership or self.OWNERSHIP)
		with open(f"{d}/protected-names.txt", "w") as f:
			f.write("libc6\nsudo\n")
		with open(f"{d}/pins.json", "w") as f:
			json.dump(pins or {"suites": {
				"apt/testing": c[:1], "apt/testing-debug": c[:1], "download/nightly": c[:1],
				"apt/stable": g, "apt/stable-debug": g, "download/release": g,
				"licensed/licensed": g[:1]}, "anchor": g + c}, f)

	def tearDown(self):
		self.tmp.cleanup()

	def drop(self, kind, bid, files, manifest=None):
		"""files: {relpath: bytes}; the manifest lists them unless given."""
		base = f"{self.root}/incoming/{kind}/{bid}"
		for rel, data in files.items():
			os.makedirs(os.path.dirname(f"{base}/{rel}"), exist_ok=True)
			with open(f"{base}/{rel}", "wb") as f:
				f.write(data)
		if manifest is None:
			manifest = {rel: sha(d) for rel, d in files.items()}
		with open(f"{self.root}/incoming/{kind}/{bid}.ready", "w") as f:
			f.writelines(f"{h}  {rel}\n" for rel, h in sorted(manifest.items()))
		return base

	def run_watch(self, **extra):
		env = dict(os.environ, WALNUT_ROOT=self.root, WALNUT_NO_DOCKER="1", **extra)
		r = subprocess.run([WATCH], env=env, capture_output=True, text=True)
		self.assertEqual(r.returncode, 0, r.stderr)
		return r.stdout

	def result(self, kind, bid):
		with open(f"{self.root}/state/done/{kind}__{bid}") as f:
			return f.read()

	def www(self, rel):
		with open(f"{self.root}/www/{rel}", "rb") as f:
			return f.read()

	def assertGone(self, kind, bid):
		self.assertFalse(os.path.lexists(f"{self.root}/incoming/{kind}/{bid}"))
		self.assertFalse(os.path.lexists(f"{self.root}/incoming/{kind}/{bid}.ready"))

	# --- downloads ---

	def test_download(self):
		self.drop("download", "0.1-alpha", release({"walnut.tar.xz": b"x" * 5000, "sub/kernel": b"k"}))
		self.run_watch()
		self.assertTrue(self.result("download", "0.1-alpha").startswith("ok"))
		self.assertEqual(self.www("download/0.1-alpha/walnut.tar.xz"), b"x" * 5000)
		self.assertGone("download", "0.1-alpha")
		self.assertEqual(os.listdir(f"{self.root}/www/download"), ["0.1-alpha"])	# no staging left

	def test_download_never_replaced(self):
		self.drop("download", "0.1", release({"a": b"one"}))
		self.run_watch()
		self.assertTrue(self.result("download", "0.1").startswith("ok"), self.result("download", "0.1"))
		self.drop("download", "0.1", release({"a": b"two"}))	# the same id again
		self.run_watch()
		self.assertEqual(self.www("download/0.1/a"), b"one")

	def test_tampered(self):
		self.drop("download", "t", {"a": b"real"}, manifest={"a": sha(b"other")})
		self.run_watch()
		self.assertIn("sha256", self.result("download", "t"))
		self.assertFalse(os.path.exists(f"{self.root}/www/download/t"))
		self.assertGone("download", "t")

	def test_extra_and_missing(self):
		self.drop("download", "e", {"a": b"1", "b": b"2"}, manifest={"a": sha(b"1")})
		self.drop("download", "m", {"a": b"1"}, manifest={"a": sha(b"1"), "b": sha(b"2")})
		self.run_watch()
		self.assertIn("differ", self.result("download", "e"))
		self.assertIn("differ", self.result("download", "m"))

	def test_symlink_file(self):
		r = release({"a": b"1"})
		man = {n: sha(d) for n, d in r.items()}
		man["b"] = sha(b"private\n")
		base = self.drop("download", "s", r, manifest=man)
		os.symlink(f"{self.root}/outside/secret", f"{base}/b")
		self.run_watch()
		self.assertTrue(self.result("download", "s").startswith("refused"))
		self.assertFalse(os.path.exists(f"{self.root}/www/download/s"))
		self.assertTrue(os.path.exists(f"{self.root}/outside/secret"))	# cleanup did not follow it

	def test_symlink_bundle_and_dir(self):
		os.symlink(f"{self.root}/outside", f"{self.root}/incoming/download/sl")
		with open(f"{self.root}/incoming/download/sl.ready", "w") as f:
			f.write(f"{sha(b'private' + bytes([10]))}  secret\n")
		base = self.drop("download", "sd", {"x": b"1"}, manifest={"x": sha(b"1"), "d/secret": sha(b"private\n")})
		os.symlink(f"{self.root}/outside", f"{base}/d")
		self.run_watch()
		self.assertTrue(self.result("download", "sl").startswith("refused"))
		self.assertTrue(self.result("download", "sd").startswith("refused"))
		self.assertEqual(os.listdir(f"{self.root}/www/download") if os.path.isdir(f"{self.root}/www/download") else [], [])
		self.assertTrue(os.path.exists(f"{self.root}/outside/secret"))

	def test_bad_names(self):
		self.drop("download", "h", {".hidden": b"1"})
		self.drop("download", "p", {"a": b"1"}, manifest={"../../outside/secret": sha(b"private\n")})
		self.run_watch()
		self.assertTrue(self.result("download", "h").startswith("refused"))
		self.assertTrue(self.result("download", "p").startswith("refused"))

	def test_latest_stable(self):
		for v in ("0.1.0", "0.1.2", "0.1.1", "0.1-alpha"):
			self.drop("download", v, release({"a": v.encode()}))
			self.run_watch()
			self.assertTrue(self.result("download", v).startswith("ok"), self.result("download", v))
		self.assertEqual(self.www("download/latest/a"), b"0.1.2")	# the highest A.B.C, not the newest

	def test_nightly(self):
		ids = [f"nightly-2026102{d}-1200-abcdef{d}" for d in range(5)]
		for n in ids:
			self.drop("download", n, release({"a": n.encode()}, who="ci"))
			self.run_watch()
			self.assertTrue(self.result("download", n).startswith("ok"), self.result("download", n))
		self.assertEqual(sorted(os.listdir(f"{self.root}/www/download/nightly")), ["latest", *ids[-3:]])
		self.assertEqual(self.www("download/nightly/latest/a"), ids[-1].encode())
		self.assertFalse(os.path.lexists(f"{self.root}/www/download/latest"))

	def test_nightly_keys_and_names(self):
		self.drop("download", "nightly-20261023-1200-abcdef0", release({"a": b"1"}))	# the archive key
		self.drop("download", "0.2.0", release({"a": b"1"}, who="ci"))	# a stable signed by CI
		self.drop("download", "nightly-x", release({"a": b"1"}, who="ci"))
		self.drop("download", "latest", release({"a": b"1"}))
		self.run_watch()
		self.assertIn("not signed by the testing-keyring.gpg key", self.result("download", "nightly-20261023-1200-abcdef0"))
		self.assertIn("not signed by the archive-keyring.gpg key", self.result("download", "0.2.0"))
		self.assertIn("a nightly is", self.result("download", "nightly-x"))
		self.assertIn("not a release id", self.result("download", "latest"))
		self.assertFalse(os.path.lexists(f"{self.root}/www/download/nightly"))

	# --- release signatures ---

	def test_release_unsigned(self):
		r = release({"a": b"1"})
		del r["SHA256SUMS.asc"]
		self.drop("download", "u", r)
		self.drop("download", "n", {"a": b"1"})
		self.run_watch()
		self.assertIn("SHA256SUMS.asc", self.result("download", "u"))
		self.assertIn("SHA256SUMS.asc", self.result("download", "n"))

	def test_release_wrong_key(self):
		self.drop("download", "w", release({"a": b"1"}, who="other"))
		self.run_watch()
		self.assertIn("not signed by the archive-keyring.gpg key", self.result("download", "w"))
		self.assertFalse(os.path.exists(f"{self.root}/www/download/w"))

	def test_release_signature_of_other_sums(self):
		r = release({"a": b"1"})
		r["SHA256SUMS"] = f"{sha(b'1')}  a\n{sha(b'2')}  b\n".encode()	# changed after signing
		r["b"] = b"2"
		self.drop("download", "c", r)
		self.run_watch()
		self.assertIn("not signed by the archive-keyring.gpg key", self.result("download", "c"))

	def test_release_unlisted_and_mismatch(self):
		r = release({"a": b"1"})
		r["extra"] = b"sneaked in"
		self.drop("download", "x", r)
		self.drop("download", "m", release({"a": b"1"}, sums=f"{sha(b'2')}  a\n".encode()))
		self.run_watch()
		self.assertIn("unlisted ['extra']", self.result("download", "x"))
		self.assertIn("does not match SHA256SUMS", self.result("download", "m"))

	def test_no_keyring(self):
		os.unlink(f"{self.root}/keys/archive-keyring.gpg")
		self.drop("download", "k", release({"a": b"1"}))
		self.drop("apt", "k", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}))
		self.run_watch()
		self.assertIn("no keyring", self.result("download", "k"))
		self.assertTrue(self.result("apt", "k").startswith("ok"))	# testing has its own key

	def test_no_manifest_waits(self):
		base = f"{self.root}/incoming/download/w"
		os.makedirs(base)
		open(f"{base}/a", "w").close()
		self.run_watch()
		self.assertTrue(os.path.exists(base))	# still uploading: left alone
		old = time.time() - 3 * 86400
		os.utime(base, (old, old))
		# ctime can't be set back: the orphan rule is checked by reading the code path only
		self.assertFalse(os.path.exists(f"{self.root}/state/done/download__w"))

	# --- apt ---

	def base(self, kind, suite):
		p = f"{self.root}/www/{kind}/dists/{suite}/InRelease"
		return sha(slurp(p, "rb")) if os.path.exists(p) else "none"

	def apt_bundle(self, debs, suite="testing", who=None, base=None, bad_line=None, bad_gz=False, key=None, fields=None, release_files=None, extra_hashed=None, idx_extra=None):
		who = who or ("ci" if suite.startswith("testing") else "good")
		base = base or self.base("licensed" if suite == "licensed" else "apt", suite)
		files, paras = {}, []
		for name, data in debs.items():
			pkg, ver, arch = name[:-4].split("_")
			src = pkg.removesuffix("-dbgsym")
			rel = f"pool/{suite}/main/{src[0]}/{src}/{name}"
			files[rel] = data
			st = {"Package": pkg, "Source": src, "Version": ver, "Architecture": arch, "Filename": rel, "SHA256": sha(data)}
			st.update((fields or {}).get(pkg, {}))
			paras.append("".join(f"{k}: {v}\n" for k, v in st.items() if v is not None))
		text = "\n".join(paras).encode()
		gz = gzip.compress(text, mtime=0)
		idx = {"main/binary-ppc64/Packages": text, "main/binary-ppc64/Packages.gz": gz}
		if bad_gz:	# only a truncated gzip index, correctly named and signed
			idx = {"main/binary-ppc64/Packages.gz": gz[:len(gz) // 2]}
		idx.update(idx_extra or {})	# more forms of the index, named in the signed Release
		for rel, d in idx.items():
			files[f"dists/{suite}/{rel}"] = d
		if not bad_gz:
			files[f"dists/{suite}/main/binary-ppc64/by-hash/SHA256/{sha(text)}"] = text
		rel_text = f"Origin: Walnut\nSuite: {suite}\nX-Walnut-Base: {base}\nSHA256:\n" + "".join(
			f" {sha(d)} {len(d)} {rel}\n" for rel, d in idx.items()) + (bad_line or "")
		signkey = key or ("primary" if suite == "licensed" else None)
		files[f"dists/{suite}/InRelease"] = sign(rel_text.encode(), who, clear=True, key=signkey)
		if release_files:	# Release and Release.gpg as walnut-repo.py writes them; a str replaces the Release text
			text_ = rel_text if release_files is True else release_files
			files[f"dists/{suite}/Release"] = text_.encode()
			files[f"dists/{suite}/Release.gpg"] = sign(rel_text.encode(), who, key=signkey)
		for rel, d in (extra_hashed or {}).items():	# more files in dists/<suite>/, hashed into nothing
			files[f"dists/{suite}/{rel}"] = d
		return files

	def test_apt_publish_and_switch(self):
		self.drop("apt", "1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}))
		self.run_watch()
		self.assertTrue(self.result("apt", "1").startswith("ok"), self.result("apt", "1"))
		self.assertEqual(self.www("apt/pool/testing/main/f/foo/foo_1_ppc64.deb"), b"foo1")
		self.assertEqual(os.readlink(f"{self.root}/www/apt/dists/testing"), ".testing-1")
		first_hash = [f for f in os.listdir(f"{self.root}/www/apt/dists/testing/main/binary-ppc64/by-hash/SHA256")]
		# the second publish keeps foo 1 and adds foo 2: only the new pool file is in the bundle
		b = self.apt_bundle({"foo_1_ppc64.deb": b"foo1", "foo_2_ppc64.deb": b"foo2"})
		del b["pool/testing/main/f/foo/foo_1_ppc64.deb"]
		self.drop("apt", "2", b)
		self.run_watch()
		self.assertTrue(self.result("apt", "2").startswith("ok"), self.result("apt", "2"))
		self.assertEqual(os.readlink(f"{self.root}/www/apt/dists/testing"), ".testing-2")
		self.assertEqual(self.www("apt/pool/testing/main/f/foo/foo_2_ppc64.deb"), b"foo2")
		now = os.listdir(f"{self.root}/www/apt/dists/testing/main/binary-ppc64/by-hash/SHA256")
		self.assertTrue(set(first_hash) < set(now))	# the old index is still there by hash

	def test_malformed_release_line_refuses_without_wedging(self):
		# a signed Release with a SHA256 line of the wrong shape: this bundle is refused, and the next one
		# (and one after a line with a size that is not a number) is still processed
		for i, bad in enumerate((" onlytwo fields\n", " " + "0" * 64 + " notanumber name\n", " a b c d\n", " zz 1 x\n"), 1):
			self.drop("apt", f"bad{i}", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}, bad_line=bad))
		self.drop("apt", "good", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}))
		self.run_watch()
		for i in range(1, 5):
			self.assertIn("malformed SHA256 line", self.result("apt", f"bad{i}"))
		self.assertTrue(self.result("apt", "good").startswith("ok"), self.result("apt", "good"))
		self.assertEqual(os.readlink(f"{self.root}/www/apt/dists/testing"), ".testing-good")

	def test_truncated_index_refuses_without_wedging(self):
		self.drop("apt", "cut", self.apt_bundle({"foo_1_ppc64.deb": b"foo1" * 100}, bad_gz=True))
		self.drop("apt", "good", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}))
		self.run_watch()
		self.assertTrue(self.result("apt", "cut").startswith("refused"), self.result("apt", "cut"))
		self.assertNotIn("Traceback", self.result("apt", "cut"))	# the public status file has no traceback
		self.assertTrue(self.result("apt", "good").startswith("ok"), self.result("apt", "good"))
		self.run_watch()	# and a later run is not stopped by it
		self.assertTrue(self.result("apt", "cut").startswith("refused"))

	def test_apt_pool_never_changes(self):
		self.drop("apt", "1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}))
		self.run_watch()
		self.drop("apt", "2", self.apt_bundle({"foo_1_ppc64.deb": b"EVIL"}))
		self.run_watch()
		self.assertIn("other contents", self.result("apt", "2"))
		self.assertEqual(self.www("apt/pool/testing/main/f/foo/foo_1_ppc64.deb"), b"foo1")
		self.assertEqual(os.readlink(f"{self.root}/www/apt/dists/testing"), ".testing-1")

	def test_apt_wrong_key(self):
		self.drop("apt", "1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}, who="other"))
		self.drop("apt", "2", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}, who="good"))	# archive key: not for testing
		self.drop("apt", "3", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}, suite="stable", who="ci"))	# CI key: not for stable
		self.run_watch()
		self.assertIn("not signed by the testing-keyring.gpg key", self.result("apt", "1"))
		self.assertIn("not signed by the testing-keyring.gpg key", self.result("apt", "2"))
		self.assertIn("not signed by the archive-keyring.gpg key", self.result("apt", "3"))
		self.assertFalse(os.path.lexists(f"{self.root}/www/apt/dists/testing"))
		self.assertFalse(os.path.lexists(f"{self.root}/www/apt/dists/stable"))

	def test_apt_stale_base(self):
		first = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		second = self.apt_bundle({"bar_1_ppc64.deb": b"bar1"})	# made from the same (empty) state
		self.drop("apt", "1", first)
		self.drop("apt", "2", second)
		self.run_watch()
		self.assertTrue(self.result("apt", "1").startswith("ok"))
		self.assertIn("stale base", self.result("apt", "2"))
		self.drop("apt", "3", self.apt_bundle({"bar_1_ppc64.deb": b"bar1"}))	# again, on the new state
		self.run_watch()
		self.assertTrue(self.result("apt", "3").startswith("ok"), self.result("apt", "3"))

	def test_status_files(self):
		first = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		second = self.apt_bundle({"bar_1_ppc64.deb": b"bar1"})	# same base: stale
		self.drop("apt", "a", first)
		self.drop("apt", "b", second)
		self.drop("download", "c", {"a": b"unsigned"})
		self.run_watch()
		st = lambda k, i: slurp(f"{self.root}/www/status/{k}__{i}")
		self.assertTrue(st("apt", "a").startswith("ok "))
		self.assertTrue(st("apt", "b").startswith("stale\n"))
		self.assertTrue(st("download", "c").startswith("refused\n"))
		self.assertEqual([f for f in os.listdir(f"{self.root}/www/status") if f.startswith(".")], [])

	def test_two_suites_switch_together(self):
		b = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		dbg = self.apt_bundle({"foo-dbgsym_1_ppc64.deb": b"dbg1"}, suite="testing-debug")
		b.update({k: v for k, v in dbg.items() if k.startswith("dists/")})
		b.update({k: v for k, v in dbg.items() if k.startswith("pool/")})
		os.makedirs(f"{self.root}/www/apt/dists/.testing-debug-x")	# the second suite can't be staged
		self.drop("apt", "x", b)
		self.run_watch()
		self.assertIn("generation .testing-debug-x exists", self.result("apt", "x"))
		self.assertFalse(os.path.lexists(f"{self.root}/www/apt/dists/testing"))	# the first didn't switch
		self.assertFalse(os.path.lexists(f"{self.root}/www/apt/dists/.testing-x"))	# nor stayed staged
		shutil.rmtree(f"{self.root}/www/apt/dists/.testing-debug-x")
		b2 = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		dbg2 = self.apt_bundle({"foo-dbgsym_1_ppc64.deb": b"dbg1"}, suite="testing-debug")
		b2.update(dbg2)
		self.drop("apt", "y", b2)
		self.run_watch()
		self.assertTrue(self.result("apt", "y").startswith("ok"), self.result("apt", "y"))
		self.assertTrue(os.path.islink(f"{self.root}/www/apt/dists/testing"))
		self.assertTrue(os.path.islink(f"{self.root}/www/apt/dists/testing-debug"))

	def test_unknown_suite(self):
		self.drop("apt", "1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}, suite="unstable", who="good"))
		self.run_watch()
		self.assertIn("not a suite of ours", self.result("apt", "1"))

	def test_apt_index_swapped(self):
		# a signed suite whose Packages is replaced by one naming another deb
		b = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		evil = b"Package: evil\nFilename: pool/main/e/evil/evil_1_ppc64.deb\n"
		b["dists/testing/main/binary-ppc64/Packages"] = evil
		b["pool/testing/main/e/evil/evil_1_ppc64.deb"] = b"evil"
		self.drop("apt", "1", b)
		b2 = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		b2["dists/testing/main/Contents-ppc64"] = b"unsigned extra index"
		self.drop("apt", "2", b2)
		self.run_watch()
		self.assertIn("does not match the signed Release", self.result("apt", "1"))
		self.assertIn("not named in the signed Release", self.result("apt", "2"))
		self.assertFalse(os.path.exists(f"{self.root}/www/apt/pool/testing/main/e"))

	def test_pool_sweep(self):
		self.drop("apt", "1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}))
		self.run_watch()
		old = f"{self.root}/www/apt/pool/testing/main/f/foo/foo_1_ppc64.deb"
		b = self.apt_bundle({"foo_2_ppc64.deb": b"foo2"})	# the new index names foo 2 only
		self.drop("apt", "2", b)
		self.run_watch()
		self.assertTrue(os.path.exists(old))	# unnamed, but within the grace period
		self.run_watch(WALNUT_PRUNE_DAYS="0")
		self.assertFalse(os.path.exists(old))
		self.assertTrue(os.path.exists(f"{self.root}/www/apt/pool/testing/main/f/foo/foo_2_ppc64.deb"))
		# a suite whose index can't be read: nothing is deleted, not even what the others don't name
		self.drop("apt", "3", self.apt_bundle({"bar_1_ppc64.deb": b"bar1"}, suite="stable"))
		self.drop("apt", "4", self.apt_bundle({"foo_2_ppc64.deb": b"foo2"}))
		self.run_watch()
		self.assertTrue(self.result("apt", "3").startswith("ok"), self.result("apt", "3"))
		for f in ("Packages", "Packages.gz"):
			os.unlink(os.path.realpath(f"{self.root}/www/apt/dists/testing") + f"/main/binary-ppc64/{f}")
		self.run_watch(WALNUT_PRUNE_DAYS="0")
		self.assertTrue(os.path.exists(f"{self.root}/www/apt/pool/testing/main/f/foo/foo_2_ppc64.deb"))
		self.assertTrue(os.path.exists(f"{self.root}/www/apt/pool/stable/main/b/bar/bar_1_ppc64.deb"))

	def test_pool_files_must_be_indexed_with_their_hash(self):
		b = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		b["pool/testing/main/f/foo/environ"] = b"WALNUT_SIGN_KEY=secret"	# not named by any index
		self.drop("apt", "1", b)
		b2 = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		b2["pool/testing/main/f/foo/foo_1_ppc64.deb"] = b"EVIL"	# not the bytes the index gives
		self.drop("apt", "2", b2)
		self.run_watch()
		self.assertIn("no signed index of the bundle names it", self.result("apt", "1"))
		self.assertIn("not the sha256 the signed index gives", self.result("apt", "2"))
		self.assertFalse(os.path.exists(f"{self.root}/www/apt/pool/testing/main/f/foo/environ"))
		self.assertFalse(os.path.exists(f"{self.root}/www/apt/pool/testing/main/f/foo/foo_1_ppc64.deb"))

	def test_pruning_generations_keeps_the_other_suite(self):
		"""testing's prune matched ".testing-" against testing-debug's generations and deleted the live one."""
		b = self.apt_bundle({"foo_0_ppc64.deb": b"foo0"})
		b.update(self.apt_bundle({"foo-dbgsym_0_ppc64.deb": b"dbg0"}, suite="testing-debug"))
		self.drop("apt", "g0", b)
		self.run_watch()
		self.assertTrue(self.result("apt", "g0").startswith("ok"), self.result("apt", "g0"))
		for i in range(1, 7):	# testing alone, more often than KEEP_GENS
			self.drop("apt", f"g{i}", self.apt_bundle({f"foo_{i}_ppc64.deb": b"foo%d" % i}))
			self.run_watch()
			self.assertTrue(self.result("apt", f"g{i}").startswith("ok"), self.result("apt", f"g{i}"))
			for suite in ("testing", "testing-debug"):
				self.assertTrue(os.path.isfile(f"{self.root}/www/apt/dists/{suite}/InRelease"), (i, suite))
		gens = [g for g in os.listdir(f"{self.root}/www/apt/dists") if g.startswith(".testing-g")]
		self.assertLessEqual(len(gens), 4)	# testing's own are still pruned

	def test_generation_names_belong_to_one_suite(self):
		import importlib.machinery, importlib.util
		os.environ["WALNUT_ROOT"] = self.root
		spec = importlib.util.spec_from_loader("ww", importlib.machinery.SourceFileLoader("ww", WATCH))
		ww = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(ww)
		self.assertEqual(ww.generation_suite(".testing-debug-20261102-x"), "testing-debug")
		self.assertEqual(ww.generation_suite(".testing-20261102-x"), "testing")
		self.assertEqual(ww.generation_suite(".stable-debug-1"), "stable-debug")
		self.assertIsNone(ww.generation_suite(".unknown-1"))
		os.makedirs(f"{self.root}/www/apt/dists/.testing-debug-live")
		os.symlink(".testing-debug-live", f"{self.root}/www/apt/dists/testing-debug")
		for i in range(6):
			os.makedirs(f"{self.root}/www/apt/dists/.testing-old{i}")
		ww.prune_generations(f"{self.root}/www/apt", "testing", ".testing-new")
		self.assertTrue(os.path.isdir(f"{self.root}/www/apt/dists/.testing-debug-live"))
		self.assertEqual(len([g for g in os.listdir(f"{self.root}/www/apt/dists") if g.startswith(".testing-old")]), 3)

	def test_apt_index_needs_pool(self):
		b = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		del b["pool/testing/main/f/foo/foo_1_ppc64.deb"]
		self.drop("apt", "1", b)
		self.run_watch()
		self.assertIn("not in the pool", self.result("apt", "1"))
		self.assertFalse(os.path.lexists(f"{self.root}/www/apt/dists/testing"))

	def test_apt_shape(self):
		self.drop("apt", "1", {"index.html": b"x", "dists/testing/InRelease": b"x"})
		self.drop("licensed", "1", self.apt_bundle({"q_1_all.deb": b"q"}, suite="licensed"))
		self.run_watch()
		self.assertIn("pool/ and dists/", self.result("apt", "1"))
		self.assertTrue(self.result("licensed", "1").startswith("ok"))
		self.assertEqual(self.www("licensed/pool/licensed/main/q/q/q_1_all.deb"), b"q")

	# --- V1: the upload keys ---

	def drop_key(self, keydir, kind, bid, files, manifest=None):
		os.makedirs(f"{self.root}/incoming/{keydir}/{kind}", exist_ok=True)
		base = f"{self.root}/incoming/{keydir}/{kind}/{bid}"
		for rel, data in files.items():
			os.makedirs(os.path.dirname(f"{base}/{rel}"), exist_ok=True)
			with open(f"{base}/{rel}", "wb") as f:
				f.write(data)
		man = manifest or {rel: sha(d) for rel, d in files.items()}
		with open(f"{base}.ready", "w") as f:
			f.writelines(f"{h}  {rel}\n" for rel, h in sorted(man.items()))

	def gone_key(self, keydir, kind, bid):
		return not os.path.lexists(f"{self.root}/incoming/{keydir}/{kind}/{bid}") and \
			not os.path.lexists(f"{self.root}/incoming/{keydir}/{kind}/{bid}.ready")

	def status(self, kind, bid):
		p = f"{self.root}/www/status/{kind}__{bid}"
		return slurp(p) if os.path.exists(p) else None

	def test_each_key_posts_only_its_own(self):
		self.drop_key("testing-key", "apt", "20261104-000000-abcdef0-b1-testing-1-u1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}))
		self.drop_key("stable-key", "download", "0.2.0", release({"a": b"1"}))
		self.drop_key("site-key", "site", "20261104-000000-abcdef0", {"index.html": b"<html>"})
		self.drop_key("pc-key", "licensed", "20261104-000000-licensed-1", self.apt_bundle({"q_1_all.deb": b"q"}, suite="licensed"))
		self.run_watch()
		for k, i in (("apt", "20261104-000000-abcdef0-b1-testing-1-u1"), ("download", "0.2.0"), ("site", "20261104-000000-abcdef0"),
		             ("licensed", "20261104-000000-licensed-1")):
			self.assertTrue(self.status(k, i).startswith("ok "), (k, i, self.status(k, i)))

	def test_a_key_cannot_post_what_is_not_its_own(self):
		stable_id = "20261104-000000-abcdef0-b1-stable-1-u1"
		cases = [
			("testing-key", "site", "20261104-000000-abcdef0", {"index.html": b"<html>"}),							# the unsigned kind
			("testing-key", "apt", stable_id, self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}, suite="stable")),			# another key's id shape and suite
			("stable-key", "licensed", "20261104-000000-licensed-1", self.apt_bundle({"q_1_all.deb": b"q"}, suite="licensed")),
			("site-key", "download", "0.2.0", release({"a": b"1"})),
			("pc-key", "apt", "20261104-000000-abcdef0-b1-testing-1-u1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})),
			("testing-key", "download", "0.2.0", release({"a": b"1"})),												# a release id from the nightly key
		]
		for n, (k, kind, bid, files) in enumerate(cases):
			self.drop_key(k, kind, bid, files)
		self.run_watch()
		for k, kind, bid, _ in cases:
			self.assertTrue(self.gone_key(k, kind, bid), (k, kind, bid))	# deleted
			self.assertIsNone(self.status(kind, bid), (k, kind, bid))		# and no verdict, which could answer for another publisher
			self.assertFalse(os.path.exists(f"{self.root}/state/done/{kind}__{bid}"), (k, kind, bid))	# and no burned id
		self.assertFalse(os.path.exists(f"{self.root}/www/site") and os.listdir(f"{self.root}/www/site"))
		self.assertFalse(os.path.exists(f"{self.root}/www/apt/dists/stable"))

	def test_a_refused_foreign_upload_does_not_burn_the_real_ids(self):
		self.drop_key("testing-key", "download", "0.2.0", release({"a": b"evil"}))
		self.run_watch()
		self.drop_key("stable-key", "download", "0.2.0", release({"a": b"real"}))
		self.run_watch()
		self.assertTrue(self.status("download", "0.2.0").startswith("ok "), self.status("download", "0.2.0"))
		self.assertEqual(self.www("download/0.2.0/a"), b"real")

	def test_foreign_kinds_and_loose_files_in_a_key_dir_are_swept(self):
		os.makedirs(f"{self.root}/incoming/testing-key/site/junk")
		with open(f"{self.root}/incoming/testing-key/site/junk/big", "w") as f:
			f.write("x" * 1000)
		with open(f"{self.root}/incoming/testing-key/loose", "w") as f:
			f.write("y")
		os.makedirs(f"{self.root}/incoming/testing-key/apt")
		self.run_watch()
		self.assertEqual(sorted(os.listdir(f"{self.root}/incoming/testing-key")), ["apt"])

	def test_the_legacy_root_still_takes_everything(self):
		self.drop("site", "legacy1", {"index.html": b"<html>"})
		self.run_watch()
		self.assertTrue(self.result("site", "legacy1").startswith("ok"))

	# --- V2: ownership, pool paths, relations ---

	def refused_code(self, kind, bid):
		st = self.status(kind, bid)
		self.assertTrue(st.startswith("refused\n"), st)
		return st.split("\n")[1]

	def apt_case(self, bid, debs, **kw):
		self.drop("apt", bid, self.apt_bundle(debs, **kw))
		self.run_watch()
		return self.status("apt", bid)

	def test_owner_rules(self):
		code = lambda bid: self.refused_code("apt", bid)
		self.apt_case("unknown", {"evil_1_ppc64.deb": b"e"})
		self.assertEqual(code("unknown"), "E-OWNER")
		self.apt_case("wrongsrc", {"foo_1_ppc64.deb": b"f"}, fields={"foo": {"Source": "bar"}})	# a binary under another Source
		self.assertEqual(code("wrongsrc"), "E-OWNER")
		self.apt_case("crosssrc", {"bar-data_1_ppc64.deb": b"d"}, fields={"bar-data": {"Source": "foo"}})
		self.assertEqual(code("crosssrc"), "E-OWNER")
		self.apt_case("reserved", {"linux-image-6.12.111+ps3.9_1_ppc64.deb": b"k"})	# an unlisted name under a reserved prefix
		self.assertEqual(code("reserved"), "E-OWNER")
		st = self.apt_case("withversion", {"foo_1_ppc64.deb": b"f"}, fields={"foo": {"Source": "foo (1.0-1)"}})
		self.assertTrue(st.startswith("ok "), st)	# Source: name (version) is read by its name

	def test_retired_names_already_published_stay_in_the_index(self):
		"""only what a bundle introduces is checked: foo, published, may later be retired and still be listed"""
		self.drop("apt", "1", self.apt_bundle({"foo_1_ppc64.deb": b"f"}))
		self.run_watch()
		self.install_rules(ownership=self.OWNERSHIP.replace('binaries = ["foo"]', 'binaries = []\nretired = ["foo"]'))
		b = self.apt_bundle({"foo_1_ppc64.deb": b"f", "bar_1_ppc64.deb": b"b"})
		del b["pool/testing/main/f/foo/foo_1_ppc64.deb"]	# the published file stays where it is
		self.drop("apt", "2", b)
		self.run_watch()
		self.assertTrue(self.status("apt", "2").startswith("ok "), self.status("apt", "2"))
		self.drop("apt", "3", self.apt_bundle({"foo_2_ppc64.deb": b"f2"}))	# a new foo: retired, refused
		self.run_watch()
		self.assertEqual(self.refused_code("apt", "3"), "E-OWNER")

	def test_a_stanza_pointing_at_a_published_file_is_checked_in_full(self):
		"""H1: a testing stanza naming protected packages in Conflicts, pointed at a deb already published in stable"""
		self.drop("apt", "st", self.apt_bundle({"bar_1_ppc64.deb": b"b"}, suite="stable"))
		self.run_watch()
		fields = {"foo": {"Conflicts": "sudo", "Filename": "pool/stable/main/b/bar/bar_1_ppc64.deb"}}
		b = self.apt_bundle({"foo_1_ppc64.deb": b"f"}, fields=fields)
		del b["pool/testing/main/f/foo/foo_1_ppc64.deb"]
		self.drop("apt", "evil", b)
		self.run_watch()
		self.assertEqual(self.refused_code("apt", "evil"), "E-RELATION")
		# the same stanza as published, unchanged, still goes through: a second testing bundle keeps the first's stanza
		self.drop("apt", "ok1", self.apt_bundle({"foo_1_ppc64.deb": b"f"}))
		self.run_watch()
		b2 = self.apt_bundle({"foo_1_ppc64.deb": b"f", "bar_1_ppc64.deb": b"b"})
		del b2["pool/testing/main/f/foo/foo_1_ppc64.deb"]
		self.drop("apt", "ok2", b2)
		self.run_watch()
		self.assertTrue(self.status("apt", "ok2").startswith("ok "), self.status("apt", "ok2"))
		# and the same published stanza with one field changed is checked again
		b3 = self.apt_bundle({"foo_1_ppc64.deb": b"f", "bar_1_ppc64.deb": b"b"}, fields={"foo": {"Replaces": "bar"}})
		del b3["pool/testing/main/f/foo/foo_1_ppc64.deb"]
		del b3["pool/testing/main/b/bar/bar_1_ppc64.deb"]
		self.drop("apt", "chg", b3)
		self.run_watch()
		self.assertEqual(self.refused_code("apt", "chg"), "E-RELATION")

	def test_every_form_of_an_index_is_the_checked_text(self):
		"""H2: apt reads Packages.xz; a plain Packages that differs from it is refused"""
		import lzma
		evil = b"Package: openssh-server\nConflicts: sudo\nFilename: pool/testing/main/o/openssh/openssh_1_ppc64.deb\n"
		self.drop("apt", "diff", self.apt_bundle({"foo_1_ppc64.deb": b"f"}, idx_extra={"main/binary-ppc64/Packages.xz": lzma.compress(evil)}))
		self.run_watch()
		self.assertEqual(self.refused_code("apt", "diff"), "E-INDEX")
		good = self.apt_bundle({"foo_1_ppc64.deb": b"f"})
		text = good["dists/testing/main/binary-ppc64/Packages"]
		import bz2
		self.drop("apt", "same", self.apt_bundle({"foo_1_ppc64.deb": b"f"}, idx_extra={"main/binary-ppc64/Packages.xz": lzma.compress(text),
		                                                                                 "main/binary-ppc64/Packages.bz2": bz2.compress(text)}))
		self.run_watch()
		self.assertTrue(self.status("apt", "same").startswith("ok "), self.status("apt", "same"))

	def test_an_index_bomb_is_refused_while_streaming(self):
		"""N2: a small .gz that expands past the cap is E-INDEX, read only up to the cap"""
		import importlib.machinery, importlib.util
		os.environ["WALNUT_ROOT"] = self.root
		spec = importlib.util.spec_from_loader("ww2", importlib.machinery.SourceFileLoader("ww2", WATCH))
		ww = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(ww)
		ww.INDEX_MAX = 1 << 20
		p = f"{self.root}/Packages"
		with open(p + ".gz", "wb") as f:
			f.write(gzip.compress(b"Package: x\n" * (1 << 20), 9))		# 11 MiB of text in a few KiB
		self.assertLess(os.path.getsize(p + ".gz"), 100000)
		with self.assertRaises(ww.Refused) as cm:
			ww.read_index(p)
		self.assertEqual(cm.exception.code, "E-INDEX")
		with open(p + ".gz", "wb") as f:
			f.write(gzip.compress(b"Package: x\n", 9))
		self.assertEqual(ww.read_index(p), "Package: x\n")

	def test_relations(self):
		code = lambda bid: self.refused_code("apt", bid)
		self.apt_case("prov", {"foo_1_ppc64.deb": b"f"}, fields={"foo": {"Provides": "sudo"}})
		self.assertEqual(code("prov"), "E-RELATION")
		self.apt_case("repl", {"foo_1_ppc64.deb": b"f"}, fields={"foo": {"Replaces": "bar"}})	# another package's
		self.assertEqual(code("repl"), "E-RELATION")
		st = self.apt_case("listed", {"foo_1_ppc64.deb": b"f"}, fields={"foo": {"Replaces": "oldfoo (<< 1)"}})
		self.assertTrue(st.startswith("ok "), st)

	def test_pool_paths(self):
		code = lambda bid: self.refused_code("apt", bid)
		self.apt_case("pathstable", {"foo_1_ppc64.deb": b"f"}, fields={"foo": {"Filename": "pool/stable/main/f/foo/foo_1_ppc64.deb"}})
		self.assertEqual(code("pathstable"), "E-POOLPATH")	# a testing bundle squatting in pool/stable
		self.apt_case("pathother", {"foo_1_ppc64.deb": b"f"}, fields={"foo": {"Filename": "pool/testing/main/b/bar/foo_1_ppc64.deb"}})
		self.assertEqual(code("pathother"), "E-POOLPATH")
		self.apt_case("html", {"foo_1_ppc64.deb": b"<script>"}, fields={"foo": {"Filename": "pool/testing/main/f/foo/foo_1.html"}})
		self.assertEqual(code("html"), "E-POOLPATH")	# a signed index may not name a non-package file
		self.apt_case("version", {"foo_1_ppc64.deb": b"f"}, fields={"foo": {"Version": "2"}})
		self.assertEqual(code("version"), "E-POOLPATH")
		self.assertFalse(os.path.exists(f"{self.root}/www/apt/pool"))

	def test_rules_missing_fails_closed(self):
		shutil.rmtree(f"{self.root}/rules")
		self.drop("apt", "a", self.apt_bundle({"foo_1_ppc64.deb": b"f"}))
		self.drop("download", "d", release({"a": b"1"}))
		self.drop("site", "s", {"index.html": b"<html>"})
		self.run_watch()
		for k, i in (("apt", "a"), ("download", "d"), ("site", "s")):
			self.assertEqual(self.refused_code(k, i), "E-RULE-MISSING")
		self.assertFalse(os.path.exists(f"{self.root}/www/apt/dists/testing"))

	def test_broken_rules_fail_closed(self):
		for bad in ("version = 1\n[package.x]\nsource = 1", "not toml ["):
			self.install_rules(ownership=bad)
			self.drop("apt", "a" + str(abs(hash(bad)) % 99999), self.apt_bundle({"foo_1_ppc64.deb": b"f"}))
			self.run_watch()
		for f in os.listdir(f"{self.root}/www/status"):
			self.assertEqual(slurp(f"{self.root}/www/status/{f}"), "refused\nE-RULE-MISSING\n")

	# --- by-hash, Release ---

	def test_by_hash_is_sha256_of_named_files_only(self):
		code = lambda bid: self.refused_code("apt", bid)
		md5 = hashlib.md5(b"Package: x").hexdigest()
		self.drop("apt", "md5", self.apt_bundle({"foo_1_ppc64.deb": b"f"}, extra_hashed={"main/binary-ppc64/by-hash/MD5Sum/" + md5: b"Package: x"}))
		self.drop("apt", "unnamed", self.apt_bundle({"foo_1_ppc64.deb": b"f"}, extra_hashed={"main/binary-ppc64/by-hash/SHA256/" + sha(b"<html>"): b"<html>"}))
		self.drop("apt", "elsewhere", self.apt_bundle({"foo_1_ppc64.deb": b"f"}, extra_hashed={"by-hash/SHA256/" + sha(b"Package: foo"): b"Package: foo"}))
		self.drop("apt", "upper", self.apt_bundle({"foo_1_ppc64.deb": b"f"}, extra_hashed={"main/binary-ppc64/by-hash/SHA256/" + sha(b"x").upper(): b"x"}))
		self.run_watch()
		for b in ("md5", "unnamed", "elsewhere", "upper"):
			self.assertEqual(code(b), "E-BYHASH", b)

	def test_release_and_release_gpg(self):
		st = self.apt_case("rel", {"foo_1_ppc64.deb": b"f"}, release_files=True)
		self.assertTrue(st.startswith("ok "), st)
		self.apt_case("badrel", {"foo_1_ppc64.deb": b"f"}, release_files="Origin: <script>\nSHA256:\n")
		self.assertEqual(self.refused_code("apt", "badrel"), "E-RELEASE")

	# --- V4: the key that signed ---

	def test_licensed_signed_by_the_subkey_is_refused(self):
		self.drop("licensed", "sub", self.apt_bundle({"q_1_all.deb": b"q"}, suite="licensed", key="sub"))
		self.drop("apt", "stablesub", self.apt_bundle({"bar_1_ppc64.deb": b"b"}, suite="stable", key="sub"))
		self.run_watch()
		self.assertEqual(self.refused_code("licensed", "sub"), "E-KEY")
		self.assertTrue(self.status("apt", "stablesub").startswith("ok "))	# the GitHub-held subkey signs stable
		self.drop("licensed", "prim", self.apt_bundle({"q_1_all.deb": b"q"}, suite="licensed", key="primary"))
		self.run_watch()
		self.assertTrue(self.status("licensed", "prim").startswith("ok "), self.status("licensed", "prim"))

	def test_the_pin_reads_field_one_not_the_primary_field(self):
		import walnut_checks as wc
		sub, prim = GPG["good_fprs"][1], GPG["good_fprs"][0]
		line = f"[GNUPG:] VALIDSIG {sub} 2026-11-04 1 0 4 0 22 8 00 {prim}"
		self.assertEqual(wc.signing_fpr([line]), sub)
		self.assertFalse(wc.signing_ok([line], [prim]))			# the subkey signed: the primary's pin does not let it pass
		self.assertTrue(wc.signing_ok([line], [sub]))
		self.assertFalse(wc.signing_ok([line, line], [sub]))		# exactly one signature
		self.assertFalse(wc.signing_ok([line.replace(sub, sub[:-1])], [sub[:-1]]))	# a short fingerprint is nothing
		self.assertFalse(wc.signing_ok([], [sub]))
		self.assertFalse(wc.signing_ok([line], []))

	def test_pins_file_names_signing_keys_of_the_real_keyrings(self):
		here = os.path.dirname(os.path.abspath(__file__))
		pins = json.loads(slurp(f"{here}/rules/pins.json"))
		def keys(ring):
			out = subprocess.run(["gpg", "--homedir", GPG["tmp"].name + "/ro", "--batch", "--show-keys", "--with-colons",
			                      "--with-subkey-fingerprints", f"{here}/../../apt-repo/keyrings/{ring}"],
			                     capture_output=True, text=True).stdout.splitlines()
			res, caps = {}, None
			for l in out:
				f = l.split(":")
				if f[0] in ("pub", "sub"):
					caps = f[11]
				elif f[0] == "fpr":
					res[f[9]] = caps
			return res
		os.makedirs(GPG["tmp"].name + "/ro", 0o700, exist_ok=True)
		arch, ci = keys("archive-keyring.gpg"), keys("testing-keyring.gpg")
		can_sign = lambda ring, f: "s" in (ring.get(f) or "").lower()
		for name, ring in (("apt/testing", ci), ("apt/testing-debug", ci), ("download/nightly", ci),
		                   ("apt/stable", arch), ("apt/stable-debug", arch), ("download/release", arch), ("licensed/licensed", arch)):
			for f in pins["suites"][name]:
				self.assertTrue(can_sign(ring, f), (name, f))
		self.assertEqual(len(pins["suites"]["licensed/licensed"]), 1)		# the offline primary key alone
		primary = [f for f in arch][0]
		self.assertEqual(pins["suites"]["licensed/licensed"], [primary])
		self.assertFalse(set(pins["anchor"]) - set(arch) - set(ci))

	# --- V8: the public status ---

	def test_status_has_codes_only(self):
		self.apt_case("ok1", {"foo_1_ppc64.deb": b"f"})
		self.apt_case("bad1", {"evil_1_ppc64.deb": b"e"})
		self.drop("download", "t", {"a": b"real"}, manifest={"a": sha(b"other")})
		self.drop("apt", "cut", self.apt_bundle({"foo_1_ppc64.deb": b"f" * 100}, bad_gz=True))
		self.run_watch()
		self.assertRegex(self.status("apt", "ok1"), r"\Aok [0-9a-f]{64}\n\Z")		# ok: its hash, no message
		self.assertEqual(self.status("apt", "bad1"), "refused\nE-OWNER\n")
		self.assertEqual(self.status("download", "t"), "refused\nE-HASH\n")
		for f in os.listdir(f"{self.root}/www/status"):
			t = slurp(f"{self.root}/www/status/{f}")
			self.assertNotIn("/", t.replace("\n", ""), f)
			self.assertNotIn("Traceback", t)
			self.assertRegex(t, r"\A(ok [0-9a-f]{64}\n|(refused|stale)\nE-[A-Z-]+\n)\Z", f)
		self.assertIn("evil", self.result("apt", "bad1"))		# the full text is in state/done and the journal

	# --- the site's trust anchor ---

	def test_site_anchor(self):
		good_asc = subprocess.run(["gpg", "--homedir", GPG["good"], "--armor", "--export"], capture_output=True).stdout
		other_asc = subprocess.run(["gpg", "--homedir", GPG["other"], "--armor", "--export"], capture_output=True).stdout
		spaced = " ".join(GPG["good_fprs"][0][i:i + 4] for i in range(0, 40, 4))
		self.drop("site", "s1", {"index.html": f"<p>Fingerprint: {spaced}</p>".encode(), "walnut-archive.asc": good_asc})
		self.drop("site", "s2", {"index.html": b"<html>", "walnut-archive.asc": other_asc})
		self.drop("site", "s3", {"index.html": b"<p>The key fingerprint is 0123 4567 89AB CDEF 0123 4567 89AB CDEF 0123 4567</p>"})
		self.drop("site", "s4", {"index.html": b"<p>commit " + b"0123456789abcdef" * 2 + b"01234567</p>"})	# 40 hex, no fingerprint word
		self.run_watch()
		self.assertTrue(self.status("site", "s1").startswith("ok "), self.status("site", "s1"))
		self.assertEqual(self.refused_code("site", "s2"), "E-ANCHOR")
		self.assertEqual(self.refused_code("site", "s3"), "E-ANCHOR")
		self.assertTrue(self.status("site", "s4").startswith("ok "))

	# --- site ---

	def test_site(self):
		self.drop("site", "s1", {"index.html": b"<html>", "version.txt": b"s1\n"})
		self.run_watch()
		self.assertTrue(self.result("site", "s1").startswith("ok"), self.result("site", "s1"))
		with open(f"{self.root}/.env") as f:
			self.assertEqual(f.read(), "WALNUT_SITE_TAG=s1\n")
		self.drop("site", "s2", {"index.html": b"<html>", "run.sh": b"#!/bin/sh"})
		self.run_watch()
		self.assertIn("not a static site file", self.result("site", "s2"))
		with open(f"{self.root}/.env") as f:
			self.assertEqual(f.read(), "WALNUT_SITE_TAG=s1\n")

	# --- systemd units ---

	def test_units_never_hit_the_start_limit(self):
		"""47 uploads at once made the path unit fail on the default start limit (2026-10-26)."""
		here = os.path.dirname(os.path.abspath(__file__))
		for u in ("walnut-watch.path", "walnut-watch.service"):
			with open(f"{here}/systemd/{u}") as f:
				self.assertIn("StartLimitIntervalSec=0", f.read(), u)


class DeployKeys(unittest.TestCase):
	"""V1: deploy-keys.pub -> authorized_keys. Only the two legacy comments get the whole /incoming; any other
	comment that is not walnut-deploy-<site|testing|stable|pc> stops the deploy"""
	K = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOR5wvTcdbYjWy7ZLZbMJSnHI5fgkQWMhoapcHlwdcz8 "

	def setUp(self):
		import importlib.util
		spec = importlib.util.spec_from_file_location("deploy_keys", os.path.join(os.path.dirname(WATCH), "deploy_keys.py"))
		self.dk = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(self.dk)

	def test_legacy_and_named(self):
		text = f"# keys\n{self.K}walnut-deploy@pc\n{self.K}walnut-deploy@github\n{self.K}walnut-deploy-testing\n{self.K}walnut-deploy-pc\n"
		a = self.dk.authorized(text).splitlines()
		self.assertEqual(len(a), 4)
		self.assertIn('-no-lock /incoming" ssh-ed25519', a[0])
		self.assertIn('-no-lock /incoming" ssh-ed25519', a[1])
		self.assertIn('-no-lock /incoming/testing-key" ssh-ed25519', a[2])
		self.assertIn('-no-lock /incoming/pc-key" ssh-ed25519', a[3])
		self.assertTrue(all(l.startswith("restrict,command=") for l in a))
		self.assertEqual(self.dk.dirs(text), ["pc-key/licensed", "pc-key/site", "testing-key/apt", "testing-key/download"])

	def test_every_other_comment_stops_the_deploy(self):
		for bad in ("walnut-deploy-Site", "walnut-deploy@github2", "walnut-deploy-", "walnut-deploy-admin", "walnut-deploy-site-key",
		            "my-laptop", "walnut-deploy-legacy", "walnut-deploy@PC"):
			with self.assertRaises(ValueError, msg=bad):
				self.dk.authorized(f"{self.K}{bad}\n")
		with self.assertRaises(ValueError):
			self.dk.authorized(f"{self.K}walnut-deploy@pc\n{self.K}stray\n")		# one bad line stops all of them
		for bad in ("ssh-rsa AAAA walnut-deploy-site", "ssh-ed25519 AAAA", "command=\"x\" ssh-ed25519 AAAA walnut-deploy-site", ""):
			with self.assertRaises(ValueError, msg=bad):
				self.dk.authorized(bad + "\n")

	def test_cli_writes_nothing_on_an_error(self):
		with tempfile.NamedTemporaryFile("w", suffix=".pub") as f:
			f.write(f"{self.K}walnut-deploy-Site\n")
			f.flush()
			r = subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(WATCH), "deploy_keys.py"), "authorized", f.name],
			                   capture_output=True, text=True)
		self.assertNotEqual(r.returncode, 0)
		self.assertEqual(r.stdout, "")

	def test_the_repositorys_keys_parse(self):
		with open(os.path.join(os.path.dirname(WATCH), "deploy-keys.pub")) as f:
			self.dk.authorized(f.read())


class Fail2ban(unittest.TestCase):
	"""V7: the 401 filter matches failed logins on /licensed/ and /ccache/ and nothing else; the jail ships enabled, reads
	both logs and never bans a docker gateway or private address"""
	def regex(self):
		conf = slurp(os.path.join(os.path.dirname(WATCH), "fail2ban/walnut-401.filter"))
		rx = re.search(r"^failregex = (.*)$", conf, re.M).group(1)
		return re.compile(rx.replace("<HOST>", r"(?P<host>[0-9a-fA-F:.]+)"))

	def test_filter(self):
		rx = self.regex()
		line = '%s - - [04/Nov/2026:01:02:03 +0200] "GET %s HTTP/1.1" %d 188 "-" "apt"'
		m = rx.search(line % ("203.0.113.5", "/licensed/dists/licensed/InRelease", 401))
		self.assertEqual(m.group("host"), "203.0.113.5")
		self.assertTrue(rx.search(line % ("2001:db8::7", "/ccache/ab/cdef", 401)))
		for ok in ((  "/licensed/dists/licensed/InRelease", 200), ("/apt/dists/stable/InRelease", 401), ("/ccache/x", 404),
		           ("/other/licensed/x", 401), ("/", 401)):
			self.assertFalse(rx.search(line % ("203.0.113.5", *ok)), ok)

	def test_jail(self):
		jail = slurp(os.path.join(os.path.dirname(WATCH), "fail2ban/walnut-401.jail"))
		self.assertRegex(jail, r"(?m)^enabled\s*=\s*true$")
		ignore = re.search(r"(?m)^ignoreip\s*=(.*)$", jail).group(1).split()
		for net in ("172.16.0.0/12", "10.0.0.0/8", "192.168.0.0/16"):	# a docker gateway, whatever its pool, is never banned
			self.assertIn(net, ignore)
		for log in ("/data/nanobyte/MainProxy/Logs/access.log", "/data/nanobyte/MainProxy/Logs/walnut.log"):
			self.assertRegex(jail, r"(?m)^\s*(logpath\s*=\s*)?%s$" % re.escape(log))


class CheckHeaders(unittest.TestCase):
	"""M1: check-headers.sh, run the way deploy.sh runs it (as a program, its exit status the verdict), against a local server"""
	def serve(self, nosniff=True):
		import http.server, threading, lzma
		packages = lzma.compress(b"Package: x\nFilename: pool/stable/main/x/x/x_1_ppc64.deb\n\n" * 50000)	# long: head closes the pipe early

		class H(http.server.BaseHTTPRequestHandler):
			def log_message(self, *a):
				pass

			def do_GET(self):
				code, ctype, body = 200, "application/octet-stream", b"data"
				if self.path.endswith("Packages.xz"):
					body = packages
				elif self.path == "/apt/":
					ctype, body = "text/html", b"<html>index</html>"
				elif self.path.startswith("/status/"):
					code, ctype, body = 403, "text/plain", b"no"
				self.send_response(code)
				self.send_header("Content-Type", ctype)
				if nosniff:
					self.send_header("X-Content-Type-Options", "nosniff")
				self.send_header("Content-Security-Policy", "default-src 'none'; sandbox")
				self.send_header("Content-Length", str(len(body)))
				self.end_headers()
				if self.command != "HEAD":
					self.wfile.write(body)
			do_HEAD = do_GET

		srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
		threading.Thread(target=srv.serve_forever, daemon=True).start()
		self.addCleanup(srv.shutdown)
		return f"http://127.0.0.1:{srv.server_address[1]}"

	def run_script(self, base):
		here = os.path.dirname(WATCH)
		return subprocess.run([f"{here}/check-headers.sh", base], capture_output=True, text=True, timeout=60)

	def test_good_server_exits_zero(self):
		r = self.run_script(self.serve())
		self.assertEqual(r.returncode, 0, r.stderr)
		self.assertIn("headers ok", r.stdout)

	def test_missing_header_exits_one(self):
		r = self.run_script(self.serve(nosniff=False))
		self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
		self.assertIn("no nosniff", r.stderr)

	def test_deploy_sh_runs_it_last(self):
		d = slurp(f"{os.path.dirname(WATCH)}/deploy.sh").rstrip().splitlines()
		self.assertEqual(d[-1], "./check-headers.sh")


class Conf(unittest.TestCase):
	"""V2: every location of repo.conf carries the headers; the compose file mounts directories; the unit protects rules/"""
	here = os.path.dirname(WATCH)

	def test_every_location_has_the_headers(self):
		conf = slurp(f"{self.here}/repo.conf")
		inc = slurp(f"{self.here}/repo-headers.inc")
		for need in ("default_type application/octet-stream;", "types { }", "X-Content-Type-Options \"nosniff\" always",
		             "Content-Security-Policy \"default-src 'none'; sandbox\" always"):
			self.assertIn(need, inc)
		# every location that serves files (not the snapshot proxy, /health, the 404s) includes the file
		for loc in ("location /apt/", "location /download/", "location /licensed/", "location /status/"):
			body = conf[conf.index(loc):]
			body = body[:body.index("\n    }\n")]
			self.assertIn("include /etc/nginx/conf.d/repo-headers.inc;", body, loc)
		for m in re.finditer(r"location ~ /dists/ \{([^}]*)\}", conf):
			self.assertIn("repo-headers.inc", m.group(1))		# a location that sets its own add_header drops the inherited ones
		cc = slurp(f"{self.here}/ccache/nginx.conf")
		self.assertIn("nosniff", cc)
		self.assertIn("default-src 'none'; sandbox", cc)

	def test_site_csp(self):
		"""M5 interim: the site allows script from itself only, in the server and in the locations that add their own headers"""
		c = slurp(f"{self.here}/site.conf")
		self.assertEqual(c.count("script-src 'self'"), 2)
		self.assertIn("always", c)

	def test_the_site_has_no_inline_script_or_handler(self):
		"""script-src 'self' drops inline script and handlers silently: none may be in the layouts, pages or site files"""
		top = os.path.join(self.here, "..", "..", "website")
		bad = []
		for d, _, fs in os.walk(top):
			if "/node_modules" in d or "/resources/_gen" in d:
				continue
			for f in fs:
				if not f.endswith((".html", ".md", ".js")):
					continue
				text = slurp(os.path.join(d, f))
				if f.endswith(".js"):
					continue
				for m in re.finditer(r"<script\b([^>]*)>", text):
					if "src=" not in m.group(1):
						bad.append((f, "inline <script>"))
				if re.search(r"(?i)\s on[a-z]+\s*=\s*[\"']", text) or "javascript:" in text.lower():
					bad.append((f, "event handler or javascript: url"))
		self.assertEqual(bad, [])
		base = slurp(os.path.join(top, "gamedb/layouts/baseof.html"))
		self.assertIn('resources.Get "walnut-os.js"', base)
		self.assertIn("walnut-os", slurp(os.path.join(top, "gamedb/assets/walnut-os.js")))

	def test_compose_mounts_directories_for_the_configs(self):
		c = slurp(f"{self.here}/docker-compose.yml")
		self.assertIn("./repo-conf:/etc/nginx/conf.d:ro", c)
		self.assertNotIn("./repo.conf:", c)
		self.assertIn("./ccache:/etc/nginx/ccache-conf:ro", c)
		self.assertIn("/etc/nginx/ccache-conf/nginx.conf", c)

	def test_the_unit(self):
		u = slurp(f"{self.here}/systemd/walnut-watch.service")
		self.assertRegex(u, r"(?m)^ReadOnlyPaths=.*Walnut/rules")
		self.assertIn("MemoryAccounting=yes", u)
		self.assertRegex(u, r"(?m)^MemoryMax=[0-9]+[GM]$")
		p = slurp(f"{self.here}/systemd/walnut-watch.path")
		sys.path.insert(0, self.here)
		import walnut_checks as wc
		for d in wc.POLICY:
			for k in wc.POLICY[d]:
				self.assertIn(f"incoming/{d}/{k}\n", p)

	def unit_sections(self, name):
		"""{section: [lines]} of a unit file"""
		out, cur = {}, None
		for line in slurp(f"{self.here}/systemd/{name}").splitlines():
			if line.startswith("[") and line.endswith("]"):
				cur = line
				out[cur] = []
			elif cur and line.strip() and not line.startswith("#"):
				out[cur].append(line)
		return out

	def test_incoming_mount_units(self):
		"""RequiresMountsFor= is a [Unit] setting (systemd ignores it in [Service]); only walnut-watch and
		walnut-upload depend on the incoming/ mount, each also checks mountpoint before it starts, and nothing
		makes docker.service or the main proxy depend on it"""
		mount = "/data/nanobyte/Walnut/incoming"
		for name in ("walnut-watch.service", "walnut-upload.service"):
			sec = self.unit_sections(name)
			self.assertIn("RequiresMountsFor=" + mount, sec["[Unit]"], name)
			self.assertNotIn("RequiresMountsFor=" + mount, sec["[Service]"], name)
			self.assertIn("ExecStartPre=/usr/bin/mountpoint -q " + mount, sec["[Service]"], name)
		up = self.unit_sections("walnut-upload.service")
		self.assertIn("ExecStart=/usr/bin/docker start -a walnut-upload", up["[Service]"])
		self.assertIn("Requires=docker.service", up["[Unit]"])
		self.assertIn("WantedBy=multi-user.target", up["[Install]"])
		# nothing in the repository makes docker.service require the mount
		for f in os.listdir(f"{self.here}/systemd"):
			self.assertNotIn("docker.service.d", f)

	def mode(self, what, mode, text):
		return subprocess.run([sys.executable, "-I", f"{self.here}/deploy_mode.py", what, mode], input=text, capture_output=True,
		                      text=True, check=True).stdout

	def test_deploy_modes_legacy_is_todays_behaviour(self):
		"""incoming/ not a mount point: docker's restart policy, walnut-watch without a mount requirement, no upload unit"""
		compose = slurp(f"{self.here}/docker-compose.yml")
		unit = slurp(f"{self.here}/systemd/walnut-watch.service")
		out = self.mode("compose", "legacy", compose)
		self.assertEqual(out, compose)
		self.assertRegex(compose[compose.index("  walnut-upload:"):].split("\nnetworks:")[0], r"(?m)^    restart: unless-stopped$")
		w = self.mode("watch", "legacy", unit)
		self.assertNotRegex(w, r"(?m)^RequiresMountsFor=")
		self.assertNotRegex(w, r"(?m)^ExecStartPre=")
		self.assertIn("ExecStart=/data/nanobyte/Walnut/bin/walnut-watch", w)
		self.assertEqual(w, "".join(l for l in unit.splitlines(True) if not l.startswith("RequiresMountsFor=") and "mountpoint" not in l))

	def test_deploy_modes_managed_uses_the_units(self):
		compose = slurp(f"{self.here}/docker-compose.yml")
		unit = slurp(f"{self.here}/systemd/walnut-watch.service")
		out = self.mode("compose", "managed", compose)
		up = out[out.index("  walnut-upload:"):].split("\nnetworks:")[0]
		self.assertRegex(up, r"(?m)^    restart: \"no\"$")
		self.assertNotIn("restart: unless-stopped", up)
		# the other services keep their policy
		self.assertEqual(out[:out.index("  walnut-upload:")], compose[:compose.index("  walnut-upload:")])
		self.assertEqual(self.mode("watch", "managed", unit), unit)
		sec = self.unit_sections("walnut-watch.service")
		self.assertIn("RequiresMountsFor=/data/nanobyte/Walnut/incoming", sec["[Unit]"])

	def test_deploy_sh_decides_the_mode_on_the_server(self):
		d = slurp(f"{self.here}/deploy.sh")
		self.assertIn('mountpoint -q $DIR/incoming', d)
		self.assertIn("echo \">> incoming/ mode: $MODE", d)
		# a switched-over server whose mount is missing: refuse, never the legacy setup
		self.assertRegex(d, r"is-enabled -q walnut-upload\.service[^\n]*\n\techo \"!! [^\n]*not a mount point, but walnut-upload\.service is enabled")
		# the upload unit is installed and enabled only in managed mode, and compose never starts the container then
		self.assertIn("if [ $MODE = managed ]; then\n\trsync -t systemd/walnut-upload.service", d)
		self.assertIn("docker compose create walnut-upload", d)
		self.assertIn("MODE=$MODE UNITS='$units' $DIR/bin/server-apply.sh units", d)
		apply = slurp(f"{self.here}/server-apply.sh")
		self.assertIn('if [ "$MODE" = managed ]; then\n\t\troot systemctl enable walnut-upload.service', apply)
		self.assertNotIn("systemd/walnut-upload.service systemd/walnut-ccache-trim", d)

	def test_deploy_sh_order(self):
		d = slurp(f"{self.here}/deploy.sh")
		# the rules are installed before walnut-watch is, and walnut-watch is not installed without them
		self.assertLess(d.index("--rules"), d.index("bin/walnut-watch"))
		self.assertIn("no rules/ownership.toml", d)
		self.assertLess(d.index("nginx -t"), d.index("up -d"))


class SnapshotCache(unittest.TestCase):
	"""V9, log-only: walnut-watch says when the snapshot cache volume is over its cap; nothing is refused or removed"""
	# the allowlist itself (repo.conf's per-archive map) is checked by test_snapshot_allowlist.py

	def test_cap_is_log_only(self):
		with tempfile.TemporaryDirectory() as root:
			os.makedirs(f"{root}/aptcache-data/x")
			with open(f"{root}/aptcache-data/x/big", "wb") as f:
				f.write(b"\0" * (3 << 20))
			env = dict(os.environ, WALNUT_ROOT=root, WALNUT_NO_DOCKER="1", WALNUT_APTCACHE_CAP_GB="0.001")
			r = subprocess.run([WATCH], env=env, capture_output=True, text=True)
			self.assertEqual(r.returncode, 0, r.stderr)
			self.assertIn("over the 1 MiB cap (log-only", r.stdout)
			self.assertTrue(os.path.exists(f"{root}/aptcache-data/x/big"))
			r = subprocess.run([WATCH], env=env, capture_output=True, text=True)
			self.assertNotIn("snapshot cache holds", r.stdout)	# checked once a day


if __name__ == "__main__":
	unittest.main()
