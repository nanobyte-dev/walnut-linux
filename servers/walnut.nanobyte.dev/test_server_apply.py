"""server-apply.sh in its dry mode (APPLY_DRY=1: it prints what it would change, runs nothing) on a scratch DIR; and that
deploy.sh uploads it and runs each step with only the values the step names. No server involved."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent


def apply(step, env, dry=True):
    e = dict(os.environ, **env)
    if dry:
        e["APPLY_DRY"] = "1"
    return subprocess.run(["bash", str(HERE / "server-apply.sh"), step], capture_output=True, text=True, env=e, timeout=60)


class Dry(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name) / "Walnut"
        (self.dir / "state").mkdir(parents=True)

    def test_fail2ban(self):
        r = apply("fail2ban", {"DIR": str(self.dir)})
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = r.stderr.splitlines()
        for conf in ("filter.d/walnut-upload.conf", "jail.d/walnut-upload.conf", "filter.d/walnut-401.conf", "jail.d/walnut-401.conf"):
            self.assertTrue(any(l.startswith("+ sudo -n install -m 644 ") and l.endswith("/etc/fail2ban/" + conf) for l in lines), conf)
        self.assertIn("+ sudo -n fail2ban-client reload", lines)
        self.assertTrue(any(l.startswith("+ rm ") and "walnut-401.jail" in l for l in lines))
        self.assertEqual([l for l in lines if "sudo" in l and "install" not in l and "fail2ban-client" not in l], [])

    def test_units(self):
        env = {"DIR": str(self.dir), "UNITS": "walnut-watch.service walnut-watch.path", "MODE": "legacy"}
        r = apply("units", env)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("/etc/systemd/system/walnut-watch.path", r.stderr)
        self.assertIn("+ sudo -n systemctl daemon-reload", r.stderr)
        self.assertNotIn("walnut-upload.service", r.stderr)
        r = apply("units", dict(env, MODE="managed"))
        self.assertIn("+ sudo -n systemctl restart walnut-upload.service", r.stderr)
        self.assertIn("+ sudo -n systemctl enable walnut-upload.service", r.stderr)

    def test_a_unit_already_installed_is_not_installed_again(self):
        etc = self.dir / "etc.service"
        etc.write_text("same")
        (self.dir / "state" / "x.service").write_text("same")
        # install_if_changed compares the state copy with the installed one: the same file needs no install
        script = 'source %s; APPLY_DRY=1; install_if_changed "%s" "%s"; install_if_changed "%s" /nonexistent/x' % (
            HERE / "server-apply.sh", self.dir / "state/x.service", etc, self.dir / "state/x.service")
        r = subprocess.run(["bash", "-c", script], capture_output=True, text=True)
        self.assertEqual(r.stderr.splitlines(), ["+ sudo -n install -m 644 %s/state/x.service /nonexistent/x" % self.dir])

    def test_proxy(self):
        (self.dir / "walnut.conf").write_text("conf")
        proxy = Path(self.tmp.name) / "conf.d"
        proxy.mkdir()
        r = apply("proxy", {"DIR": str(self.dir), "PROXY": str(proxy), "CONF": "walnut.conf"})
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("+ cp %s/walnut.conf %s/walnut.conf.new" % (self.dir, proxy), r.stderr)
        self.assertIn("+ mv %s/walnut.conf.new %s/walnut.conf" % (proxy, proxy), r.stderr)
        self.assertFalse((proxy / "walnut.conf").exists())     # dry: nothing was copied
        (proxy / "walnut.conf").write_text("old")
        self.assertIn("+ cp -p %s/walnut.conf %s/walnut.conf.bak" % (proxy, proxy), apply("proxy", {"DIR": str(self.dir), "PROXY": str(proxy), "CONF": "walnut.conf"}).stderr)

    def real_proxy(self, nginx_ok, old, stale_bak):
        """server-apply.sh proxy for real against a stub docker; -> (rc, live file text or None, docker calls)"""
        proxy = Path(self.tmp.name) / "conf.d"
        proxy.mkdir()
        (self.dir / "walnut.conf").write_text("new")
        if old:
            (proxy / "walnut.conf").write_text("old")
        if stale_bak:
            (proxy / "walnut.conf.bak").write_text("stale")
        stub = Path(self.tmp.name) / "bin"
        stub.mkdir()
        calls = Path(self.tmp.name) / "calls"
        (stub / "docker").write_text('#!/bin/sh\necho "$*" >> %s\n[ "$3 $4" != "nginx -t" ] || { echo "[emerg] bad"; [ %s = 1 ]; exit; }\n' % (calls, int(nginx_ok)))
        (stub / "docker").chmod(0o755)
        r = apply("proxy", {"DIR": str(self.dir), "PROXY": str(proxy), "CONF": "walnut.conf",
                            "PATH": "%s:%s" % (stub, os.environ["PATH"])}, dry=False)
        live = proxy / "walnut.conf"
        return r, (live.read_text() if live.exists() else None), (calls.read_text() if calls.exists() else ""), proxy

    def test_proxy_for_real(self):
        r, live, calls, proxy = self.real_proxy(True, old=True, stale_bak=False)
        self.assertEqual((r.returncode, live), (0, "new"), r.stderr)
        self.assertIn("nginx -s reload", calls)
        self.assertFalse((proxy / "walnut.conf.new").exists())

    def test_proxy_rollback_restores_the_old_config(self):
        r, live, calls, _ = self.real_proxy(False, old=True, stale_bak=True)
        self.assertEqual((r.returncode, live), (1, "old"), r.stderr)
        self.assertNotIn("reload", calls)
        self.assertIn("[emerg] bad", r.stderr)          # the whole nginx -t output, not its last line

    def test_proxy_rollback_without_an_old_config_removes_the_new_one(self):
        r, live, calls, _ = self.real_proxy(False, old=False, stale_bak=True)
        self.assertEqual((r.returncode, live), (1, None), r.stderr)    # not the stale .bak
        self.assertNotIn("reload", calls)

    def test_refusals(self):
        self.assertEqual(apply("nonsense", {"DIR": str(self.dir)}).returncode, 2)
        self.assertNotEqual(apply("units", {"DIR": str(self.dir)}).returncode, 0)    # no UNITS, no MODE
        self.assertNotEqual(apply("proxy", {"DIR": str(self.dir)}).returncode, 0)
        r = subprocess.run(["bash", str(HERE / "server-apply.sh"), "fail2ban"], capture_output=True, text=True, env={"PATH": os.environ["PATH"]})
        self.assertNotEqual(r.returncode, 0)                                         # no DIR


class Deploy(unittest.TestCase):
    def test_deploy_runs_the_steps_from_the_uploaded_file(self):
        text = (HERE / "deploy.sh").read_text()
        self.assertIn("server-apply.sh", text)
        for step in ("fail2ban", "units", "proxy"):
            self.assertIn("$DIR/bin/server-apply.sh %s" % step, text)
        for old in ("fail2ban-client reload", "systemctl daemon-reload", "nginx -s reload", "fail2ban/jail.d"):
            self.assertNotIn(old, text, "%s is server-apply.sh's now" % old)


if __name__ == "__main__":
    unittest.main()
