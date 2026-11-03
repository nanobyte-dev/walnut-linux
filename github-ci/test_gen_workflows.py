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

	def test_pull_request_workflows_hold_nothing(self):
		"""M11: a workflow on pull_request has no secret, no environment, no write permission, no called workflow"""
		def wf(body, perms="permissions:\n  contents: read\n"):
			return f"on:\n  pull_request:\n{perms}jobs:\n  t:\n    runs-on: x\n{body}"
		ok = wf("    steps:\n      - run: make test\n")
		self.assertEqual(self.lint({"a.yml": ok}, yaml=True), [])
		for bad in (wf("    steps:\n      - run: echo\n        env:\n          K: ${{ secrets.K }}\n"),
		            wf("    environment: walnut-testing\n    steps:\n      - run: echo\n"),
		            wf("    steps:\n      - run: echo\n", perms="permissions:\n  contents: write\n"),
		            wf("    permissions:\n      id-token: write\n    steps:\n      - run: echo\n"),
		            "on:\n  pull_request:\njobs:\n  t:\n    uses: ./.github/workflows/x.yml\n    secrets: inherit\n"):
			self.assertTrue(self.lint({"a.yml": bad}, yaml=True), bad)
		# the same things are fine on a push workflow (they are other rules' business)
		self.assertEqual(self.lint({"a.yml": "on:\n  push:\njobs:\n  t:\n    runs-on: x\n    environment: walnut-testing\n    steps:\n      - run: echo\n        env:\n          K: ${{ secrets.K }}\n"}, yaml=True), [])

	def test_the_tests_workflow_exists_and_is_secret_free(self):
		import yaml
		doc = yaml.safe_load(open(f"{HERE}/../.github/workflows/walnut-tests.yml"))
		self.assertIn("pull_request", doc[True])
		self.assertEqual(gw.secret_uses(doc), [])
		run = "\n".join(st.get("run", "") for st in doc["jobs"]["tests"]["steps"])
		self.assertIn("tools/run-tests.sh --ci", run)
		self.assertIn("commit_tests/run.py --all --only shell", run)

	def test_secrets_stay_out_of_package_code(self):
		def wf(jobs, top=""):
			return f"{top}on:\n  push:\njobs:\n{jobs}"
		ok = wf("  build:\n    runs-on: x\n    steps:\n      - run: make all\n        env:\n          WALNUT_CCACHE_AUTH: ${{ secrets.WALNUT_CCACHE_AUTH }}\n"
		        "      - run: cache-mac.sh verify a b\n        env:\n          SALT: ${{ secrets.WALNUT_CACHE_SALT }}\n")
		self.assertEqual(self.lint({"a.yml": ok}, yaml=True), [])
		# a step that runs package code names a secret other than the ccache login
		bad = ok.replace("WALNUT_CCACHE_AUTH: ${{ secrets.WALNUT_CCACHE_AUTH }}", "SALT: ${{ secrets.WALNUT_CACHE_SALT }}")
		self.assertIn("runs package code references", "\n".join(self.lint({"a.yml": bad}, yaml=True)))
		# the external build, the release image's build and layer: no secret reference at all
		ext = wf("  build-external:\n    runs-on: x\n    steps:\n      - run: echo hi\n        env:\n          X: ${{ secrets.WALNUT_DEPLOY_KEY }}\n")
		self.assertIn("references a secret", "\n".join(self.lint({"walnut-package.yml": ext}, yaml=True)))
		self.assertEqual(self.lint({"other.yml": ext}, yaml=True), [])	# the rule names these jobs
		for job in ("build", "layer"):
			t = wf(f"  {job}:\n    runs-on: x\n    steps:\n      - run: echo\n        env:\n          X: ${{{{ secrets.WALNUT_CACHE_SALT }}}}\n")
			self.assertIn("references a secret", "\n".join(self.lint({"release-image.yml": t}, yaml=True)), job)
		# every form of the secrets context, not only secrets.NAME
		forms = ["toJSON(secrets)", "secrets['WALNUT_DEPLOY_KEY']", "secrets[format('WALNUT_{0}', 'DEPLOY_KEY')]",
		         "format('{0}', secrets)", "secrets[ 'X' ]"]
		for form in forms:
			t = wf(f"  build-external:\n    runs-on: x\n    steps:\n      - run: echo\n        env:\n          X: ${{{{ {form} }}}}\n")
			self.assertIn("references a secret", "\n".join(self.lint({"walnut-package.yml": t}, yaml=True)), form)
			t = wf(f"  j:\n    runs-on: x\n    steps:\n      - run: make all\n        env:\n          X: ${{{{ {form} }}}}\n")
			self.assertIn("runs package code references ['<dynamic>']", "\n".join(self.lint({"a.yml": t}, yaml=True)), form)
		# a job that passes secrets on: only the callers of walnut-package.yml
		call = "  j:\n    uses: ./.github/workflows/walnut-package.yml\n    secrets: inherit\n"
		self.assertEqual(self.lint({"pkg-x.yml": wf(call)}, yaml=True), [])
		for sec in ("secrets: inherit", "secrets:\n      X: ${{ secrets.Y }}"):
			other = f"  j:\n    uses: ./.github/workflows/other.yml\n    {sec}\n"
			self.assertIn("passes secrets to something other", "\n".join(self.lint({"a.yml": wf(other)}, yaml=True)), sec)
		inherit_in_build = "  layer:\n    uses: ./.github/workflows/walnut-package.yml\n    secrets: inherit\n"
		self.assertIn("references a secret", "\n".join(self.lint({"release-image.yml": wf(inherit_in_build)}, yaml=True)))
		# a secret in an env block above the steps reaches every step of the job or workflow
		self.assertIn("job-level env", "\n".join(self.lint({"a.yml": wf("  j:\n    runs-on: x\n    env:\n      K: ${{ secrets.X }}\n    steps:\n      - run: echo\n")}, yaml=True)))
		self.assertIn("workflow-level env", "\n".join(self.lint({"a.yml": wf("  j:\n    runs-on: x\n    steps:\n      - run: echo\n", top="env:\n  K: ${{ secrets.X }}\n")}, yaml=True)))

	def test_stable_needs_the_gate(self):
		A = gw.ALLOWED
		gate = ("  stable-gate:\n    needs: publish-check\n    if: always() && needs.publish-check.result == 'success' && inputs.target == 'stable' && !" + A
		        + "\n    runs-on: x\n    environment: walnut-stable-review\n    steps:\n      - run: echo ok\n")
		check = "  publish-check:\n    runs-on: x\n    steps:\n      - run: echo ok\n"
		outcome = ("  stable-outcome:\n    needs: [publish-check, stable-gate, publish]\n    if: always() && needs.publish.result != 'success'\n"
		           "    runs-on: x\n    steps:\n      - run: |\n          echo no\n          exit 1\n")
		recheck = ("      - env:\n          GATE: ${{ needs.stable-gate.result }}\n          PUBLISHERS: ${{ vars.WALNUT_STABLE_PUBLISHERS }}\n"
		           "          ACTOR: ${{ github.triggering_actor }}\n        run: |\n" + gw.RECHECK_RUN)
		pub = ("  publish:\n    needs: [publish-check, stable-gate]\n    if: always() && (needs.stable-gate.result == 'success' || "
		       "(needs.stable-gate.result == 'skipped' && " + A + "))\n    runs-on: x\n    environment: walnut-stable\n    steps:\n"
		       + recheck + "      - run: test $GITHUB_REF = refs/heads/main && echo publish\n")
		wf = lambda *jobs: "on:\n  push:\njobs:\n" + check + "".join(jobs)
		self.assertEqual(self.lint({"a.yml": wf(gate, pub, outcome)}, yaml=True), [])
		errs = lambda *jobs: "\n".join(self.lint({"a.yml": wf(*(jobs if outcome in jobs or not pub in jobs else jobs + (outcome,)))}, yaml=True))
		# the skipped-ancestor case: a gate without always() is skipped whenever build-external was, and the run stays green
		self.assertIn("needs always()", errs(gate.replace("always() && ", ""), pub))
		self.assertIn("needs always()", errs(gate.replace("needs.publish-check.result == 'success' && ", ""), pub))
		# a run that wanted a stable publish and didn't get one must end red
		self.assertIn("no stable-outcome", "\n".join(self.lint({"a.yml": wf(gate, pub)}, yaml=True)))
		self.assertIn("no stable-outcome", "\n".join(self.lint({"a.yml": wf(gate, pub, outcome.replace("exit 1", "exit 0"))}, yaml=True)))
		self.assertIn("no stable-outcome", "\n".join(self.lint({"a.yml": wf(gate, pub, outcome.replace("always() && ", ""))}, yaml=True)))
		self.assertIn("no stable-outcome", "\n".join(self.lint({"a.yml": wf(gate, pub, outcome.replace("!= 'success'", "== 'failure'"))}, yaml=True)))
		self.assertIn("no stable-gate", errs(pub))	# the gate is missing
		self.assertIn("no stable-gate", errs(gate.replace("environment: walnut-stable-review", "environment: walnut-stable"), pub))
		self.assertIn("no stable-gate", errs(gate.replace("!" + A, A), pub))	# the wrong way round
		self.assertIn("without needing stable-gate", errs(gate, pub.replace("    needs: [publish-check, stable-gate]\n", "")))
		self.assertIn("must accept the gate's success", errs(gate, pub.replace("needs.stable-gate.result == 'skipped'", "true")))
		self.assertIn("must accept the gate's success", errs(gate, pub.replace(A, "true")))	# any skipped gate would do
		self.assertIn("first step must check", errs(gate, pub.replace(recheck, "")))
		self.assertIn("first step must check", errs(gate, pub.replace("any(.[]; . == $a)", "any(.[]; contains($a))")))
		self.assertIn("first step must check", errs(gate, pub.replace("PUBLISHERS: ${{ vars.WALNUT_STABLE_PUBLISHERS }}", "PUBLISHERS: x")))
		self.assertIn("first step must check", errs(gate, pub.replace(recheck, "      - run: echo hi\n" + recheck)))	# not the first step
		# the release image's channel-dependent environment counts as stable
		self.assertIn("no stable-gate", errs(pub.replace("environment: walnut-stable\n", "environment: walnut-${{ inputs.channel }}\n")))
		# the review environment holds no secret and calls nothing
		self.assertIn("references secrets", errs(gate.replace("echo ok", "echo ${{ secrets.X }}"), pub))
		self.assertIn("references secrets", errs(gate.replace("    runs-on: x\n", "    secrets: inherit\n    uses: ./x.yml\n"), pub))
		self.assertIn("runs only for someone not on the allowlist", errs(gate.replace("!" + A, "true")))

	def test_the_recheck_is_strict(self):
		"""The step that decides: exact login in a JSON array of strings (contains() would match a substring of a string)."""
		import subprocess
		import yaml
		for src in (gw.generate()["walnut-package.yml"], open(f"{HERE}/../.github/workflows/release-image.yml").read()):
			jobs = yaml.safe_load(src)["jobs"]
			step = jobs["publish"]["steps"][0]
			run = lambda gate, publishers, actor: subprocess.run(["bash", "-c", step["run"]], capture_output=True, text=True, env={
				"PATH": os.environ["PATH"], "GATE": gate, "PUBLISHERS": publishers, "ACTOR": actor}).returncode
			self.assertEqual(run("success", "", "anyone"), 0)	# a reviewer approved
			self.assertEqual(run("skipped", '["me"]', "me"), 0)
			self.assertEqual(run("skipped", '["you", "me"]', "me"), 0)
			for gate, pubs, actor in (("skipped", "", "me"), ("skipped", "[]", "me"), ("skipped", '"me"', "me"),	# a string, not a list
			                          ("skipped", '"someone, me"', "me"), ("skipped", '["mee"]', "me"), ("skipped", '["me"]', "m"),
			                          ("skipped", '["ME"]', "me"), ("skipped", '["me", 5]', "me"), ("skipped", "{\"me\": 1}", "me"),
			                          ("skipped", "not json", "me"), ("skipped", '["me"]', ""), ("failure", '["me"]', "me"),
			                          ("cancelled", '["me"]', "me"), ("", '["me"]', "me")):
				self.assertNotEqual(run(gate, pubs, actor), 0, (gate, pubs, actor))

	def test_the_generated_workflows_have_the_gate(self):
		t = gw.generate()["walnut-package.yml"]
		import yaml
		for src in (t, open(f"{HERE}/../.github/workflows/release-image.yml").read()):
			jobs = yaml.safe_load(src)["jobs"]
			self.assertTrue(jobs["stable-gate"]["if"].startswith("always() && needs."))
			self.assertEqual(jobs["stable-outcome"]["steps"][0]["run"].splitlines()[-1].strip(), "exit 1")
		self.assertIn("environment: walnut-stable-review", t)
		self.assertIn("!" + gw.ALLOWED, t)
		r = open(f"{HERE}/../.github/workflows/release-image.yml").read()
		self.assertIn("environment: walnut-stable-review", r)
		self.assertEqual(gw.workflow_lint(), [])

	def test_write_permissions_and_cache_steps(self):
		self.assertTrue(self.lint({"a.yml": "    permissions:\n      contents: write\n"}))
		self.assertEqual(self.lint({"release-image.yml": "    permissions:\n      contents: write\n"}), [])
		good = ("      - name: c\n        if: steps.keys.outputs.chroot != ''\n        uses: actions/cache@v6\n"
		        "        with:\n          key: ${{ steps.keys.outputs.chroot }}\n")
		self.assertEqual(self.lint({"a.yml": good}), [])
		self.assertTrue(self.lint({"a.yml": good.replace("        if: steps.keys.outputs.chroot != ''\n", "")}))	# unguarded
		self.assertTrue(self.lint({"a.yml": good.replace("steps.keys.outputs.chroot }}", "hashFiles('x') }}")}))	# unsalted key
		self.assertTrue(self.lint({"a.yml": good + "          restore-keys: chroot-\n"}))

	def test_every_workflow_in_the_tree_parses(self):
		self.assertEqual([e for e in gw.workflow_lint() if "YAML" in e or "duplicate" in e or "top" in e], [])

	def test_rules(self):
		checkout = "      - uses: actions/checkout@v7\n        with:\n          persist-credentials: %s\n"
		self.assertEqual(self.lint({"a.yml": checkout % "false"}), [])
		self.assertEqual(self.lint({"a.yml": "# secrets: inherit and pull_request_target are words here\n" + checkout % "false"}), [])
		self.assertEqual(self.lint({"a.yml": "    secrets: inherit\n"}), [])	# the callers of walnut-package.yml: the rules below keep it safe
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
			self.assertIn("secrets: inherit", t, pkg)	# environment secrets reach the called jobs only so
		t = self.files["pkg-mesa.yml"]
		self.assertIn("tier: trusted", t)
		self.assertIn("WALNUT_CCACHE_AUTH", self.files["walnut-package.yml"])	# referenced by the trusted build step only

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
		chk = t[t.index("\n  publish-check:\n"):t.index("\n  stable-gate:\n")]
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


def secret_uses_of(x):
	return gw.secret_uses(x)


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

	def test_layer_key_covers_everything_the_layer_is_made_from(self):
		"""Q18: both jobs that compute the layer's key hash the builder's Dockerfiles and the games of ROOTFS_GAMES"""
		import yaml
		jobs = yaml.safe_load(open(f"{HERE}/../.github/workflows/release-image.yml"))["jobs"]
		for j in ("cache-restore", "cache-save"):
			steps = jobs[j]["steps"]
			names = [st.get("id") for st in steps]
			self.assertLess(names.index("inputs"), names.index("key"), j)
			base = steps[names.index("key")]["env"]["BASE"]
			for need in ("build-containers/Dockerfile'", "build-containers/Dockerfile.deb", "system/image/packages.txt",
			             "tools/build-rootfs.sh", "config.mk", "steps.inputs.outputs.games"):
				self.assertIn(need, base, f"{j}: {need}")
			self.assertIn("build-info.py image-base", steps[names.index("inputs")]["run"])
			self.assertEqual(secret_uses_of(steps[names.index("inputs")]), [], j)	# the step that reads the repository's files holds no secret

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
		# C-2/R-4: the job that installs our testing packages never holds the salt and never hands a layer back; the
		# salt jobs run no package code; the layer cache-save signs comes from the layer job (Debian packages only)
		import yaml
		jobs = yaml.safe_load(r)["jobs"]
		self.assertEqual(list(jobs), ["cache-restore", "layer", "build", "cache-save", "stable-gate", "publish", "stable-outcome"])
		build = yaml.dump(jobs["build"])
		self.assertIn("build-release.sh", build)
		self.assertNotIn("secrets.", build)
		self.assertNotIn("actions/cache", build)
		uploads = [st["with"]["name"] for st in jobs["build"]["steps"] if "upload-artifact" in st.get("uses", "")]
		self.assertEqual(len(uploads), 1)	# the release's files; no layer goes back
		self.assertTrue(uploads[0].startswith("release-${{"))
		self.assertNotIn("base.tar", yaml.dump([st for st in jobs["build"]["steps"] if "upload-artifact" in st.get("uses", "")]))
		layer = yaml.dump(jobs["layer"])
		self.assertIn("make rootfs-base", layer)
		self.assertNotIn("secrets.", layer)
		self.assertNotIn("build-release.sh", layer)
		self.assertEqual(jobs["cache-save"]["needs"], ["cache-restore", "layer"])
		for job in ("cache-restore", "cache-save"):
			body = yaml.dump(jobs[job])
			self.assertNotIn("build-release.sh", body)
			self.assertNotIn("make ", body)
			self.assertIn("secrets.WALNUT_CACHE_SALT", body)
		self.assertIn("sha256", jobs["layer"]["outputs"])	# R-4: cache-save checks the artifact against the job output
		steps = [st.get("name", "") for st in jobs["cache-save"]["steps"]]
		check = next(i for i, n in enumerate(steps) if n.startswith("The layer is the one the layer job built"))
		self.assertLess(steps.index("The new package layer"), check)
		self.assertLess(check, steps.index("Record the layer's MAC"))
		self.assertLess(check, steps.index("Keep the package layer"))
		self.assertIn("needs.layer.outputs.sha256", yaml.dump(jobs["cache-save"]["steps"][check]))
		saved = yaml.dump(jobs["cache-save"])
		self.assertIn("release-base-new", saved)	# the layer job's artifact
		self.assertNotIn("release-base-out", saved)


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
