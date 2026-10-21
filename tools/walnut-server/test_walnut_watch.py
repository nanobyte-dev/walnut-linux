#!/usr/bin/env python3
"""Tests for walnut-watch: runs it against a temporary root (WALNUT_ROOT, no docker) with bundles
dropped the way walnut-upload.sh drops them, good ones and hostile ones.
  python3 tools/walnut-server/test_walnut_watch.py"""
import gzip, hashlib, os, shutil, subprocess, tempfile, time, unittest

WATCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "walnut-watch")
GPG = {}	# good, other: GNUPGHOMEs; keyring: the good key exported


def sha(b):
	return hashlib.sha256(b).hexdigest()


def setUpModule():
	GPG["tmp"] = tempfile.TemporaryDirectory()
	for who in ("good", "other", "ci"):
		home = f"{GPG['tmp'].name}/{who}"
		os.makedirs(home, 0o700)
		subprocess.run(["gpg", "--homedir", home, "--batch", "--passphrase", "", "--quick-gen-key",
		                f"Walnut test {who} <{who}@test.invalid>", "ed25519", "sign", "never"],
		               check=True, capture_output=True)
		GPG[who] = home
	for who, name in (("good", "keyring"), ("ci", "testing_keyring")):
		GPG[name] = f"{GPG['tmp'].name}/{name}.gpg"
		with open(GPG[name], "wb") as f:
			f.write(subprocess.run(["gpg", "--homedir", GPG[who], "--export"], check=True, capture_output=True).stdout)


def tearDownModule():
	GPG["tmp"].cleanup()


def sign(data, who="good", clear=False):
	mode = ["--clearsign"] if clear else ["--detach-sign", "--armor"]
	return subprocess.run(["gpg", "--homedir", GPG[who], "--batch", *mode], input=data,
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
		os.makedirs(f"{self.root}/outside")
		with open(f"{self.root}/outside/secret", "w") as f:
			f.write("private\n")

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
		return sha(open(p, "rb").read()) if os.path.exists(p) else "none"

	def apt_bundle(self, debs, suite="testing", who=None, base=None):
		who = who or ("ci" if suite == "testing" else "good")
		base = base or self.base("licensed" if suite == "licensed" else "apt", suite)
		files, paras = {}, []
		for name, data in debs.items():
			rel = f"pool/main/{name[0]}/{name.split('_')[0]}/{name}"
			files[rel] = data
			paras.append(f"Package: {name.split('_')[0]}\nFilename: {rel}\nSHA256: {sha(data)}\n")
		text = "\n".join(paras).encode()
		gz = gzip.compress(text, mtime=0)
		idx = {"main/binary-ppc64/Packages": text, "main/binary-ppc64/Packages.gz": gz}
		for rel, d in idx.items():
			files[f"dists/{suite}/{rel}"] = d
		files[f"dists/{suite}/main/binary-ppc64/by-hash/SHA256/{sha(text)}"] = text
		rel_text = f"Origin: Walnut\nSuite: {suite}\nX-Walnut-Base: {base}\nSHA256:\n" + "".join(
			f" {sha(d)} {len(d)} {rel}\n" for rel, d in idx.items())
		files[f"dists/{suite}/InRelease"] = sign(rel_text.encode(), who, clear=True)
		return files

	def test_apt_publish_and_switch(self):
		self.drop("apt", "1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}))
		self.run_watch()
		self.assertTrue(self.result("apt", "1").startswith("ok"), self.result("apt", "1"))
		self.assertEqual(self.www("apt/pool/main/f/foo/foo_1_ppc64.deb"), b"foo1")
		self.assertEqual(os.readlink(f"{self.root}/www/apt/dists/testing"), ".testing-1")
		first_hash = [f for f in os.listdir(f"{self.root}/www/apt/dists/testing/main/binary-ppc64/by-hash/SHA256")]
		# the second publish keeps foo 1 and adds foo 2: only the new pool file is in the bundle
		b = self.apt_bundle({"foo_1_ppc64.deb": b"foo1", "foo_2_ppc64.deb": b"foo2"})
		del b["pool/main/f/foo/foo_1_ppc64.deb"]
		self.drop("apt", "2", b)
		self.run_watch()
		self.assertTrue(self.result("apt", "2").startswith("ok"), self.result("apt", "2"))
		self.assertEqual(os.readlink(f"{self.root}/www/apt/dists/testing"), ".testing-2")
		self.assertEqual(self.www("apt/pool/main/f/foo/foo_2_ppc64.deb"), b"foo2")
		now = os.listdir(f"{self.root}/www/apt/dists/testing/main/binary-ppc64/by-hash/SHA256")
		self.assertTrue(set(first_hash) < set(now))	# the old index is still there by hash

	def test_apt_pool_never_changes(self):
		self.drop("apt", "1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}))
		self.run_watch()
		self.drop("apt", "2", self.apt_bundle({"foo_1_ppc64.deb": b"EVIL"}))
		self.run_watch()
		self.assertIn("other contents", self.result("apt", "2"))
		self.assertEqual(self.www("apt/pool/main/f/foo/foo_1_ppc64.deb"), b"foo1")
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

	def test_unknown_suite(self):
		self.drop("apt", "1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}, suite="unstable", who="good"))
		self.run_watch()
		self.assertIn("not a suite of ours", self.result("apt", "1"))

	def test_apt_index_swapped(self):
		# a signed suite whose Packages is replaced by one naming another deb
		b = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		evil = b"Package: evil\nFilename: pool/main/e/evil/evil_1_ppc64.deb\n"
		b["dists/testing/main/binary-ppc64/Packages"] = evil
		b["pool/main/e/evil/evil_1_ppc64.deb"] = b"evil"
		self.drop("apt", "1", b)
		b2 = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		b2["dists/testing/main/Contents-ppc64"] = b"unsigned extra index"
		self.drop("apt", "2", b2)
		self.run_watch()
		self.assertIn("does not match the signed Release", self.result("apt", "1"))
		self.assertIn("not named in the signed Release", self.result("apt", "2"))
		self.assertFalse(os.path.exists(f"{self.root}/www/apt/pool/main/e"))

	def test_pool_sweep(self):
		self.drop("apt", "1", self.apt_bundle({"foo_1_ppc64.deb": b"foo1"}))
		self.run_watch()
		old = f"{self.root}/www/apt/pool/main/f/foo/foo_1_ppc64.deb"
		b = self.apt_bundle({"foo_2_ppc64.deb": b"foo2"})	# the new index names foo 2 only
		self.drop("apt", "2", b)
		self.run_watch()
		self.assertTrue(os.path.exists(old))	# unnamed, but within the grace period
		self.run_watch(WALNUT_PRUNE_DAYS="0")
		self.assertFalse(os.path.exists(old))
		self.assertTrue(os.path.exists(f"{self.root}/www/apt/pool/main/f/foo/foo_2_ppc64.deb"))
		# a suite whose index can't be read: nothing is deleted, not even what the others don't name
		self.drop("apt", "3", self.apt_bundle({"bar_1_ppc64.deb": b"bar1"}, suite="stable"))
		self.drop("apt", "4", self.apt_bundle({"foo_2_ppc64.deb": b"foo2"}))
		self.run_watch()
		self.assertTrue(self.result("apt", "3").startswith("ok"), self.result("apt", "3"))
		for f in ("Packages", "Packages.gz"):
			os.unlink(os.path.realpath(f"{self.root}/www/apt/dists/testing") + f"/main/binary-ppc64/{f}")
		self.run_watch(WALNUT_PRUNE_DAYS="0")
		self.assertTrue(os.path.exists(f"{self.root}/www/apt/pool/main/f/foo/foo_2_ppc64.deb"))
		self.assertTrue(os.path.exists(f"{self.root}/www/apt/pool/main/b/bar/bar_1_ppc64.deb"))

	def test_apt_index_needs_pool(self):
		b = self.apt_bundle({"foo_1_ppc64.deb": b"foo1"})
		del b["pool/main/f/foo/foo_1_ppc64.deb"]
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
		self.assertEqual(self.www("licensed/pool/main/q/q/q_1_all.deb"), b"q")

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


if __name__ == "__main__":
	unittest.main()
