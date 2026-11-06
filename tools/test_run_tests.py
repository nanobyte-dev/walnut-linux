"""Tests of tools/run-tests.py and tools/tests.toml (suite run-tests): how a suite's exit and log become pass, skip,
FAIL, xfail or XPASS, the --ci rule that a skip fails unless the suite's ci_skip names it, the registry's checks, and
that every file the registry runs exists."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("run_tests", HERE / "run-tests.py")
rt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rt)


class Runner(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.logs = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def run_sh(self, script, **fields):
        return rt.run_suite(dict(name="t", run=["sh", "-c", script], **fields), self.logs)

    def test_results(self):
        self.assertEqual(self.run_sh("echo fine")["res"], "pass")
        r = self.run_sh("echo broke; exit 3")
        self.assertEqual((r["res"], r["fail"]), ("FAIL", True))
        self.assertIn("broke", r["msg"][0])
        r = self.run_sh("echo 'skip: no thing'; exit 77")
        self.assertEqual((r["res"], r["fail"], r["skips"]), ("skip", False, ["SKIP t no thing"]))

    def test_steps_stop_at_the_first_failure(self):
        s = dict(name="t", run=[["sh", "-c", "echo one"], ["sh", "-c", "exit 2"], ["sh", "-c", "echo three"]])
        r = rt.run_suite(s, self.logs)
        self.assertEqual(r["res"], "FAIL")
        self.assertNotIn("three", (self.logs / "t.log").read_text())

    def test_skip_lines_and_unittest_skips(self):
        r = self.run_sh("echo 'SKIP t:ppc64 no qemu'; echo 'OK (skipped=2)'")
        self.assertEqual(r["res"], "pass")
        self.assertEqual(r["skips"], ["SKIP t:ppc64 no qemu", "SKIP t:unittest 2 tests skipped (see its log)"])

    def test_prerequisites_skip(self):
        r = self.run_sh("exit 1", needs_cmd=["no-such-command-xyz"])
        self.assertEqual((r["res"], r["skips"]), ("skip", ["SKIP t no no-such-command-xyz"]))
        r = self.run_sh("exit 1", needs_path=["no/such/path"], skip_note="the tree is missing")
        self.assertEqual((r["res"], r["skips"]), ("skip", ["SKIP t the tree is missing"]))

    def test_xfail(self):
        self.assertEqual(self.run_sh("echo known bug; exit 4", xfail="4|known bug")["res"], "xfail")
        self.assertEqual(self.run_sh("echo fine", xfail="4|known bug")["res"], "XPASS")
        self.assertEqual(self.run_sh("echo other; exit 4", xfail="4|known bug")["res"], "FAIL")
        self.assertEqual(self.run_sh("echo known bug; exit 5", xfail="4|known bug")["res"], "FAIL")

    def test_warn_unless(self):
        r = self.run_sh("echo '3 unresolved'", warn_unless=" 0 unresolved")
        self.assertEqual((r["res"], len(r["msg"])), ("pass", 1))
        self.assertEqual(self.run_sh("echo 'x: 0 unresolved'", warn_unless=" 0 unresolved")["msg"], [])

    def test_ci_rule(self):
        suites = [dict(name="a", ci_skip={"a": "no tree", "a:part": "no qemu"}), dict(name="b")]
        skipped = {"name": "a", "skips": ["SKIP a no tree", "SKIP a:part x"]}
        failed, notes = rt.ci_check(suites[:1], [skipped], "")
        self.assertEqual((failed, notes), (False, []))
        failed, notes = rt.ci_check(suites[:1], [{"name": "a", "skips": ["SKIP a:other x"]}], "")
        self.assertTrue(failed)
        self.assertIn("a:other", notes[0])
        # a missing prerequisite listed for another suite is not this suite's to skip
        failed, _ = rt.ci_check(suites, [{"name": "a", "skips": []}, {"name": "b", "skips": ["SKIP a no tree"]}], "")
        self.assertTrue(failed)
        # a listed skip that did not happen is only mentioned
        failed, notes = rt.ci_check(suites[:1], [{"name": "a", "skips": []}], "")
        self.assertEqual((failed, len(notes)), (False, 2))

    def test_a_missing_program_fails_only_its_suite(self):
        r = rt.run_suite(dict(name="t", run=["no-such-program-xyz"]), self.logs)
        self.assertEqual((r["res"], r["fail"]), ("FAIL", True))
        self.assertIn("no-such-program-xyz", r["tail"])

    def test_field_types(self):
        for bad in ('ci_skip = "mesa"', 'needs_cmd = "gcc"', 'run = "sh -c x"', 'ci_skip = { a = 1 }', 'slow = "yes"',
                    'run = [["a"], "b"]', 'xfail = 3'):
            f = self.logs / "tests.toml"
            f.write_text('[[suite]]\nname = "x"\nrun = ["a"]\n' .replace('run = ["a"]\n', '') + ('run = ["a"]\n' if not bad.startswith("run") else '') + bad + "\n")
            with self.assertRaises(SystemExit, msg=bad):
                rt.load_suites(f)

    def test_unknown_only_is_an_error(self):
        self.assertEqual(rt.main(["--only", "no-such-suite-xyz"]), 2)

    def test_registry_checks(self):
        for bad in ('[[suite]]\nname = "x"\n', '[[suite]]\nname = "x"\nrun = ["a"]\nfoo = 1\n',
                    '[[suite]]\nname = "x"\nrun = ["a"]\n[[suite]]\nname = "x"\nrun = ["b"]\n'):
            f = self.logs / "tests.toml"
            f.write_text(bad)
            with self.assertRaises(SystemExit):
                rt.load_suites(f)


class Registry(unittest.TestCase):
    def test_every_suite_runs_a_file_that_exists(self):
        suites = rt.load_suites()
        self.assertGreater(len(suites), 60)
        for s in suites:
            self.assertIn("what", s, s["name"])
            for cmd in rt.steps(s):
                for arg in cmd[1:]:
                    if arg.endswith((".py", ".sh")):
                        self.assertTrue((rt.ROOT / arg).exists(), "%s runs %s, which is not there" % (s["name"], arg))

    def test_ci_skips_name_their_own_suite(self):
        for s in rt.load_suites():
            for key in s.get("ci_skip", {}):
                # the key is what the suite's SKIP line starts with: the suite, suite:part (sdl2-compat is one for sdl2-compat-close-locked)
                self.assertTrue(s["name"].startswith(key.split(":")[0]), (s["name"], key))


if __name__ == "__main__":
    unittest.main()
