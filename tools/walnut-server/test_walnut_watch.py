#!/usr/bin/env python3
"""Tests for walnut-watch: runs it against a temporary root (WALNUT_ROOT, no docker) with bundles
dropped the way walnut-upload.sh drops them, good ones and hostile ones.
  python3 tools/walnut-server/test_walnut_watch.py"""
import gzip, hashlib, os, subprocess, tempfile, time, unittest

WATCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "walnut-watch")


def sha(b):
	return hashlib.sha256(b).hexdigest()


class Watch(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.root = self.tmp.name
		for k in ("site", "download", "apt", "licensed"):
			os.makedirs(f"{self.root}/incoming/{k}")
		os.makedirs(f"{self.root}/www")
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

	def run_watch(self):
		env = dict(os.environ, WALNUT_ROOT=self.root, WALNUT_NO_DOCKER="1")
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
		self.drop("download", "0.1-alpha", {"walnut.tar.xz": b"x" * 5000, "SHA256SUMS": b"s\n"})
		self.run_watch()
		self.assertTrue(self.result("download", "0.1-alpha").startswith("ok"))
		self.assertEqual(self.www("download/0.1-alpha/walnut.tar.xz"), b"x" * 5000)
		self.assertGone("download", "0.1-alpha")
		self.assertEqual(os.listdir(f"{self.root}/www/download"), ["0.1-alpha"])	# no staging left

	def test_download_never_replaced(self):
		self.drop("download", "0.1", {"a": b"one"})
		self.run_watch()
		self.drop("download", "0.1b", {"a": b"two"})
		os.rename(f"{self.root}/incoming/download/0.1b", f"{self.root}/incoming/download/x")
		self.drop("download", "0.1", {"a": b"two"})	# the same id again
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
		base = self.drop("download", "s", {"a": b"1"}, manifest={"a": sha(b"1"), "b": sha(b"private\n")})
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

	def apt_bundle(self, debs, suite="testing"):
		files, paras = {}, []
		for name, data in debs.items():
			rel = f"pool/main/{name[0]}/{name.split('_')[0]}/{name}"
			files[rel] = data
			paras.append(f"Package: {name.split('_')[0]}\nFilename: {rel}\nSHA256: {sha(data)}\n")
		text = "\n".join(paras).encode()
		files[f"dists/{suite}/main/binary-ppc64/Packages"] = text
		files[f"dists/{suite}/main/binary-ppc64/Packages.gz"] = gzip.compress(text, mtime=0)
		files[f"dists/{suite}/main/binary-ppc64/by-hash/SHA256/{sha(text)}"] = text
		files[f"dists/{suite}/InRelease"] = b"signed " + sha(text).encode()
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
