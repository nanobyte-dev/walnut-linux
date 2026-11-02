#!/usr/bin/python3 -I
"""github-ci/packages.toml against the tree: every package directory has exactly one entry (dir and Source
from its own files), the file passes walnut_lists.py's validation, kernel names and a few ownership
facts hold, and the relation checks refuse what they should. python3 github-ci/test_walnut_lists.py"""
import importlib.util, os, re, subprocess, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
_spec = importlib.util.spec_from_file_location("walnut_lists", f"{HERE}/walnut_lists.py")
wl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(wl)


class Real(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.lists = wl.Lists(open(f"{HERE}/packages.toml").read())
		cls.protected = wl.load_protected(open(f"{HERE}/protected-names.txt").read())
		cls.dirs = subprocess.run([f"{ROOT}/tools/pkg-dir.sh", "--all"], capture_output=True, text=True, check=True,
		                          cwd=ROOT).stdout.split()

	def test_every_package_has_an_entry(self):
		self.assertEqual(sorted(os.path.basename(d) for d in self.dirs), sorted(self.lists.entries))
		for d in self.dirs:
			self.assertEqual(self.lists.entries[os.path.basename(d)].dir, d)

	def test_source_is_what_the_package_makes(self):
		for d in self.dirs:
			e = self.lists.entries[os.path.basename(d)]
			ctl = f"{ROOT}/{d}/debian/control"
			if os.path.exists(ctl):
				m = re.search(r"^Source: (\S+)", open(ctl).read(), re.M)
				self.assertEqual(e.source, m.group(1), d)

	def test_ownership(self):
		L = self.lists
		self.assertEqual(L.owner("linux-image-6.12.111+ps3.1224"), ("linux", False))
		self.assertEqual(L.owner("linux-image-6.12.111+ps3.1224-debug202611010405-dbgsym"), ("linux", False))
		self.assertEqual(L.owner("ps3rsx-modules-6.12.111+ps3.1224"), ("ps3rsx", False))
		self.assertEqual(L.owner("ps3-games"), ("ps3-desktop", True))	# retired
		self.assertIsNone(L.owner("libc6"))
		self.assertEqual(L.owner("linux-image-evil")[0], "linux")	# reserved prefix: nobody else, and not listed
		self.assertTrue(L.owner("linux-image-evil")[1])

	def test_tiers(self):
		ext = {n for n, e in self.lists.entries.items() if e.tier == "external"}
		self.assertEqual(ext, {"badwolf", "tk-html3", "tkinterweb", "zlib-ng", "classicube", "devilutionx",
		                       "openrct2", "warzone2100", "darkplaces-gl2"})
		self.assertEqual({n for n, e in self.lists.entries.items() if e.tier == "licensed"},
		                 {"classicube-data", "sauerbraten-data", "sauerbraten-nonfree-data"})

	def test_relation_rules(self):
		L, P = self.lists, self.protected
		bw, mesa, plym = L.entries["badwolf"], L.entries["mesa"], L.entries["plymouth"]
		R = lambda e, **f: wl.check_relations(L, P, e, f)
		self.assertEqual(R(bw), ([], []))
		self.assertTrue(R(bw, Replaces="libc6")[0])	# protected
		self.assertTrue(R(bw, Provides="initramfs-tools-core")[0])
		self.assertTrue(R(bw, Breaks="mesa-libgallium")[0])	# another package's
		self.assertTrue(R(bw, Conflicts="something-foreign")[0])	# external: no foreign name at all
		self.assertEqual(R(mesa, Conflicts="something-foreign"), ([], ["Conflicts: something-foreign is not in the package's relations"]))
		self.assertTrue(R(mesa, Essential="yes")[0])
		self.assertEqual(R(plym, Breaks="mawk (<< 1.3)")[0], [])	# listed, and only a Breaks
		self.assertTrue(R(plym, Replaces="mawk")[0])
		self.assertEqual(R(mesa, Depends="libc6")[0], [])	# a Depends is no relation of this kind


if __name__ == "__main__":
	unittest.main()
