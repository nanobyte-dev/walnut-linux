#!/usr/bin/python3 -I
"""github-ci/gen-workflows.py's checks: the tier rules (a trusted package must not be upstream or fetch) and the
lint over every workflow file (secrets: inherit, a checkout that keeps the token, stable without a main check),
and what the generated workflows give each tier. python3 github-ci/test_gen_workflows.py"""
import importlib.machinery, importlib.util, os, re, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_loader("gw", importlib.machinery.SourceFileLoader("gw", f"{HERE}/gen-workflows.py"))
gw = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gw)

TOML = """version = 1
[package.up]
dir = "packages/up"
source = "up"
tier = "trusted"
binaries = ["up"]
[package.fetchy]
dir = "packages/fetchy"
source = "fetchy"
tier = "trusted"
binaries = ["fetchy"]
[package.ext]
dir = "packages/ext"
source = "ext"
tier = "external"
binaries = ["ext"]
[package.fine]
dir = "packages/fine"
source = "fine"
tier = "trusted"
binaries = ["fine"]
[package.wrongdir]
dir = "packages/elsewhere"
source = "wrongdir"
tier = "trusted"
binaries = ["wrongdir"]
[package.lic]
dir = "packages/lic"
source = "lic"
tier = "trusted"
binaries = ["lic"]
"""


class Tiers(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.addCleanup(self.tmp.cleanup)
		self.lists = gw.lists_module().Lists(TOML)
		self.old = (gw.ROOT, dict(gw.DIRS), gw.WF)
		self.addCleanup(lambda: (setattr(gw, "ROOT", self.old[0]), gw.DIRS.clear(), gw.DIRS.update(self.old[1]),
		                         setattr(gw, "WF", self.old[2])))
		gw.ROOT = self.tmp.name
		gw.DIRS.clear()
		for n, conf, rules in (("up", "TYPE=upstream\n", ""), ("fetchy", "TYPE=debsrc\n", "all:\n\tcurl -O http://x/y\n"),
		                       ("ext", "TYPE=upstream\n", "all:\n\tcurl -O http://x/y\n"), ("fine", "TYPE=debsrc\n", "all:\n\techo hi\n"),
		                       ("wrongdir", "TYPE=debsrc\n", ""), ("nolist", "TYPE=debsrc\n", ""), ("lic", "TYPE=upstream\n", "")):
			d = f"packages/{n}"
			os.makedirs(f"{self.tmp.name}/{d}/debian")
			os.makedirs(f"{self.tmp.name}/{d}/patches")
			open(f"{self.tmp.name}/{d}/package.conf", "w").write(conf)
			open(f"{self.tmp.name}/{d}/debian/rules", "w").write(rules)
			open(f"{self.tmp.name}/{d}/patches/0001.patch", "w").write("curl http://evil\n")	# a patch is not a build file
			gw.DIRS[n] = d

	def test_tier_errors(self):
		errs = "\n".join(gw.tier_errors(["up", "fetchy", "ext", "fine", "wrongdir", "nolist"], {"lic"}, self.lists))
		self.assertIn("up: trusted, but TYPE=upstream", errs)
		self.assertIn("fetchy: trusted, but packages/fetchy/debian/rules:2 fetches", errs)
		self.assertIn("wrongdir: packages.toml says packages/elsewhere", errs)
		self.assertIn("nolist: no entry", errs)
		self.assertIn("lic: licensed, but packages.toml has no such entry with tier licensed", errs)
		for ok in ("ext:", "fine:", "patches"):
			self.assertNotIn(ok, errs)

	def test_xcc_exception_is_exact(self):
		open(f"{self.tmp.name}/packages/fine/package.conf", "w").write("TYPE=debsrc\nBUILD=xcc\n")
		self.assertIn("fine: trusted, but BUILD=xcc", "\n".join(gw.tier_errors(["fine"], set(), self.lists)))


class Lint(unittest.TestCase):
	def lint(self, files, yaml=False):
		with tempfile.TemporaryDirectory() as t:
			for n, text in files.items():
				open(f"{t}/{n}", "w").write(text)
			old, gw.WF = gw.WF, t
			try:
				return gw.workflow_lint(check_yaml=yaml)
			finally:
				gw.WF = old

	def test_yaml_is_checked(self):
		ok = "on:\n  push:\njobs:\n  a:\n    runs-on: x\n"
		self.assertEqual(self.lint({"a.yml": ok}, yaml=True), [])
		self.assertTrue(self.lint({"a.yml": ok + "jobs:\n  b:\n    runs-on: x\n"}, yaml=True))	# a second jobs:
		self.assertTrue(self.lint({"a.yml": ok.replace("runs-on: x", "runs-on: x\n    runs-on: y")}, yaml=True))	# a repeated key
		self.assertTrue(self.lint({"a.yml": ok + "      - name: Release: a colon\n"}, yaml=True))	# an unquoted ': '
		self.assertTrue(self.lint({"a.yml": "name: x\n"}, yaml=True))	# not a workflow
		self.assertEqual(self.lint({}), [])

	def test_every_workflow_in_the_tree_parses(self):
		self.assertEqual([e for e in gw.workflow_lint() if "YAML" in e or "duplicate" in e or "top" in e], [])

	def test_rules(self):
		checkout = "      - uses: actions/checkout@v7\n        with:\n          persist-credentials: %s\n"
		self.assertEqual(self.lint({"a.yml": checkout % "false"}), [])
		self.assertEqual(self.lint({"a.yml": "# secrets: inherit and pull_request_target are words here\n" + checkout % "false"}), [])
		self.assertTrue(self.lint({"a.yml": "    secrets: inherit\n"}))
		self.assertTrue(self.lint({"a.yml": checkout % "true"}))
		self.assertTrue(self.lint({"a.yml": "      - uses: actions/checkout@v7\n"}))
		self.assertEqual(self.lint({"walnut-site.yml": checkout % "true"}), [])
		self.assertTrue(self.lint({"a.yml": "on:\n  pull_request_target:\n"}))
		self.assertTrue(self.lint({"a.yml": "    environment: walnut-stable\n"}))
		self.assertEqual(self.lint({"a.yml": "    environment: walnut-stable\n    run: test $GITHUB_REF = refs/heads/main\n"}), [])


class Generated(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.files = gw.generate()

	def test_external_gets_no_credentials(self):
		for pkg in ("badwolf", "tk-html3", "tkinterweb", "zlib-ng", "classicube", "devilutionx", "openrct2",
		            "warzone2100", "darkplaces-gl2"):
			t = self.files[f"pkg-{pkg}.yml"]
			self.assertIn("tier: external", t, pkg)
			self.assertNotIn("CCACHE", t, pkg)
			self.assertNotIn("CACHE_SALT", t, pkg)
			self.assertNotIn("inherit", t, pkg)
		t = self.files["pkg-mesa.yml"]
		self.assertIn("tier: trusted", t)
		self.assertIn("WALNUT_CCACHE_AUTH", t)

	def test_reusable_workflow(self):
		t = self.files["walnut-package.yml"]
		ext = t[t.index("\n  build-external:\n"):t.index("\n  publish:\n")]
		self.assertIn("permissions:\n      contents: read", ext)
		self.assertNotIn("write", ext)
		self.assertNotIn("secrets.", ext)
		self.assertNotIn("actions/cache", ext)
		self.assertNotIn("CCACHE", ext)
		trusted = t[t.index("\n  build:\n"):t.index("\n  build-external:\n")]
		self.assertIn("secrets.WALNUT_CACHE_SALT", trusted)
		self.assertNotIn("restore-keys", t)
		self.assertIn("refs/heads/main", t[t.index("\n  publish:\n"):])
		# stable: the check job holds no key and no environment; the key job checks the same bytes by manifest
		chk = t[t.index("\n  publish-check:\n"):t.index("\n  publish:\n")]
		self.assertNotIn("secrets.", chk)
		self.assertNotIn("environment:", chk)
		self.assertIn("--strict", chk)
		self.assertIn("--manifest-out", chk)
		pub = t[t.index("\n  publish:\n"):]
		self.assertIn("environment: walnut-stable", pub)
		self.assertIn("--manifest ", pub)
		self.assertIn("publish-check", pub[:300])
		self.assertIsNone(re.search(r"key: (chroot|spu)-", t))	# no unsalted key

	def test_no_salt_means_no_actions_cache(self):
		"""Until WALNUT_CACHE_SALT exists the jobs build without any Actions cache: no restore, no save, no failure."""
		for name, text in (("walnut-package.yml", self.files["walnut-package.yml"]),
		                   ("release-image.yml", open(f"{HERE}/../.github/workflows/release-image.yml").read())):
			self.assertIn('[ -n "$SALT" ] || { echo "-- no WALNUT_CACHE_SALT', text, name)	# the key step exits 0, sets nothing
			steps = re.split(r"\n      - ", text)
			caches = [st for st in steps if "actions/cache" in st]
			self.assertTrue(caches, name)
			for st in caches:
				self.assertRegex(st, r"if: .*steps\.(keys|key)\.outputs\.\w+ != ''", f"{name}: {st[:60]}")
				self.assertNotIn("restore-keys", st)
				self.assertRegex(st, r"key: \$\{\{ steps\.(keys|key|base)\.outputs", name)	# only the salted key, never a literal one


class CacheMac(unittest.TestCase):
	"""A cache entry anyone can create is used only when it matches a MAC made with the secret salt (C-1)."""

	def run_mac(self, mode, path, mac, salt="s3cret"):
		import subprocess
		return subprocess.run([f"{HERE}/cache-mac.sh", mode, path, mac], env=dict(os.environ, SALT=salt), capture_output=True, text=True)

	def test_poisoned_entries_are_ignored(self):
		with tempfile.TemporaryDirectory() as t:
			f, d = f"{t}/chroot.tar", f"{t}/install"
			open(f, "w").write("good")
			os.makedirs(d)
			open(f"{d}/bin", "w").write("good")
			for path in (f, d):
				self.assertEqual(self.run_mac("record", path, path + ".mac").returncode, 0)
				self.assertEqual(self.run_mac("verify", path, path + ".mac").returncode, 0)
			# the entry was replaced by another job's: other content, the old MAC (or none, or one made without the salt)
			open(f, "w").write("poisoned")
			open(f"{d}/bin", "w").write("poisoned")
			for path in (f, d):
				r = self.run_mac("verify", path, path + ".mac")
				self.assertEqual(r.returncode, 3, r.stderr)
				self.assertFalse(os.path.exists(path))	# removed: the build starts fresh
				self.assertFalse(os.path.exists(path + ".mac"))
			open(f, "w").write("poisoned")
			self.assertEqual(self.run_mac("verify", f, f + ".mac").returncode, 3)	# no MAC at all
			open(f, "w").write("x")
			self.assertEqual(self.run_mac("record", f, f + ".mac", salt="guess").returncode, 0)
			self.assertEqual(self.run_mac("verify", f, f + ".mac").returncode, 3)	# a MAC with another salt
			self.assertEqual(self.run_mac("verify", f"{t}/absent", f"{t}/absent.mac").returncode, 0)	# a cache miss

	def test_workflows_use_it(self):
		t = gw.generate()["walnut-package.yml"]
		t = t[t.index("\n  build:\n"):t.index("\n  build-external:\n")]
		self.assertLess(t.index("cache-mac.sh verify"), t.index("run: github-ci/build-package.sh"))
		self.assertGreater(t.index("cache-mac.sh record"), t.index("run: github-ci/build-package.sh"))
		self.assertIn("::add-mask::", t)
		r = open(f"{HERE}/../.github/workflows/release-image.yml").read()
		self.assertLess(r.index("cache-mac.sh verify"), r.index("run: github-ci/build-release.sh"))
		self.assertLess(r.index("cache-mac.sh record"), r.index("actions/cache/save"))
		self.assertIn("::add-mask::", r)
		# C-2: the job that installs our testing packages never holds the salt; the salt jobs run no package code
		import yaml
		jobs = yaml.safe_load(r)["jobs"]
		self.assertEqual(list(jobs), ["cache-restore", "build", "cache-save", "publish"])
		build = yaml.dump(jobs["build"])
		self.assertIn("build-release.sh", build)
		self.assertNotIn("secrets.", build)
		self.assertNotIn("actions/cache", build)
		for job in ("cache-restore", "cache-save"):
			body = yaml.dump(jobs[job])
			self.assertNotIn("build-release.sh", body)
			self.assertIn("secrets.WALNUT_CACHE_SALT", body)


class PcRule(unittest.TestCase):
	def test_ccache_remote_gives_externals_nothing(self):
		import subprocess
		env = dict(os.environ, WALNUT_CCACHE_AUTH="u:p")
		run = lambda pkg: subprocess.run([f"{HERE}/../tools/ccache-remote.sh", "--for", pkg, "echo", "ran"], env=env,
		                                 capture_output=True, text=True)
		for pkg in ("badwolf", "no-such-package", "classicube-data"):
			r = run(pkg)
			self.assertEqual(r.stdout.strip(), "ran")
			self.assertIn("no shared cache", r.stderr, pkg)
		self.assertNotIn("not trusted", run("mesa").stderr)


if __name__ == "__main__":
	unittest.main()
