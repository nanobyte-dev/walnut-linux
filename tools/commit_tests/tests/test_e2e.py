#!/usr/bin/env python3
"""test_e2e.py: the commit hooks end to end (make test's commit-tests suite). A clone of this repository's
HEAD in build/tmp/commit-tests-e2e with the working tree's tools/commit_tests and tools/hooks copied in and
the hooks installed (tools/hooks/install.sh); then each bad case is committed and must be refused with its
reason, each good case must go in, and an AI trailer must leave the message. The clone is deleted after.

The private pattern is made up at run time and written to the clone's own untracked replace list, so no
private text is in this file or in the repository."""
import os
import secrets
import shutil
import subprocess
import tomllib
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent.parent
WORK = REPO / "build" / "tmp" / "commit-tests-e2e"
ENV = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
ENV.update(GIT_LFS_SKIP_SMUDGE="1", GIT_AUTHOR_NAME="e2e", GIT_AUTHOR_EMAIL="e2e@example.invalid",
           GIT_COMMITTER_NAME="e2e", GIT_COMMITTER_EMAIL="e2e@example.invalid")
COPY = ("tools/commit_tests", "tools/hooks", "tools/gamedb/gdp-manifest.py", "tools/gamedb/gdp-manifest.series")


def git(*args, check=True, cwd=WORK):
    r = subprocess.run(["git", "-C", str(cwd), *args], capture_output=True, text=True, env=ENV)
    if check and r.returncode:
        raise RuntimeError("git %s: %s%s" % (" ".join(args), r.stdout, r.stderr))
    return r


def front(path):
    t = path.read_text()
    return tomllib.loads(t.split("+++")[1]), t


class E2E(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if WORK.exists():
            shutil.rmtree(WORK)
        WORK.parent.mkdir(parents=True, exist_ok=True)
        r = subprocess.run(["git", "clone", "-q", "--shared", "--no-tags", str(REPO), str(WORK)], capture_output=True,
                           text=True, env=ENV)
        if r.returncode:
            raise RuntimeError(r.stderr)
        for rel in COPY:
            src, dst = REPO / rel, WORK / rel
            if src.is_dir():
                shutil.rmtree(dst, ignore_errors=True)
                shutil.copytree(src, dst, ignore=shutil.ignore_patterns("__pycache__"))
            elif src.exists():
                shutil.copy2(src, dst)
        git("add", "--", *[p for p in COPY if (WORK / p).exists()])
        if git("diff", "--cached", "--quiet", check=False).returncode:
            git("commit", "-q", "-m", "commit tests from the working tree")
        cls.token = "pv" + "-" + secrets.token_hex(6)
        (WORK / "tools" / "public-replace.local.txt").write_text("regex:%s==>(private)\n" % cls.token)
        r = subprocess.run(["tools/hooks/install.sh"], cwd=WORK, capture_output=True, text=True, env=ENV)
        if r.returncode:
            raise RuntimeError(r.stdout + r.stderr)
        r = subprocess.run(["tools/hooks/install.sh", "--check"], cwd=WORK, capture_output=True, text=True, env=ENV)
        assert r.returncode == 0, r.stdout
        games = WORK / "gamedb" / "games"
        cls.published, cls.drafts = [], []
        for d in sorted(games.iterdir()):
            fm, _ = front(d / "index.md")
            (cls.drafts if fm.get("draft") else cls.published).append(d.name)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(WORK, ignore_errors=True)

    def tearDown(self):
        git("reset", "-q", "--hard", "HEAD")
        git("clean", "-qfd", "-e", "tools/public-replace.local.txt")

    def commit(self, paths, msg="A change\n"):
        if paths:
            git("add", "--", *paths)
        return git("commit", "-q", "-m", msg, check=False)

    def refused(self, paths, needle, msg="A change\n"):
        head = git("rev-parse", "HEAD").stdout
        r = self.commit(paths, msg)
        self.assertNotEqual(r.returncode, 0, "committed, but should be refused (%s)" % needle)
        self.assertIn(needle, r.stderr)
        self.assertEqual(git("rev-parse", "HEAD").stdout, head)
        return r

    def accepted(self, paths, msg="A change\n"):
        r = self.commit(paths, msg)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r

    def edit(self, rel, fn):
        p = WORK / rel
        p.write_text(fn(p.read_text()))
        return rel

    # --- bad cases
    def test_incomplete_published_page(self):
        g = self.published[0]
        rel = "gamedb/games/%s/index.md" % g
        self.edit(rel, lambda t: "+++" + t.split("+++")[1] + "+++\n")
        self.refused([rel], "page text")

    def test_draft_with_sessions(self):
        g = next(d for d in self.drafts if not (WORK / "gamedb/games" / d / "rating" / "sessions").exists())
        rel = "gamedb/games/%s/rating/sessions/2026-10-31-x/result.toml" % g
        (WORK / rel).parent.mkdir(parents=True)
        (WORK / rel).write_text('schema = 1\nkind = "session"\ngame = "%s"\n' % g)
        self.refused([rel], "draft with played sessions")

    def test_batch_without_conclusion(self):
        rel = "gamedb/benchmarks/batches.toml"
        self.edit(rel, lambda t: t + '\n[[batch]]\nname = "2026-10-31-e2e"\nkind = "summary"\ntested = "x"\nsource = "e2e"\n')
        self.refused([rel], "no conclusion")

    def test_hex_colour(self):
        rel = "packages/walnut/apps/walnut-games/files/lib/walnut_games.py"
        self.edit(rel, lambda t: t + '\nE2E_COLOUR = "#12ab34"\n')
        self.refused([rel], "#12ab34")

    def test_duplicate_launcher_name(self):
        src = next(p for p in sorted((WORK / "gamedb/games").glob("*/desktop/launchers/*.desktop"))
                   if "NoDisplay=true" not in p.read_text())
        dup = src.parent / ("e2e-" + src.name)
        dup.write_text("\n".join(ln + " -e2e" if ln.startswith("Exec=") else ln for ln in src.read_text().split("\n")))
        self.refused([str(dup.relative_to(WORK))], "visible menu entries are named")

    def test_epoch_version(self):
        rel = "packages/patched/audio/libvorbis/package.conf"
        self.edit(rel, lambda t: t.replace("VERSION=1.3.7-3", "VERSION=1:1.3.7-3"))
        self.refused([rel], "epoch")

    def test_pkill_f_in_a_script(self):
        rel = "tools/e2e-check.sh"
        (WORK / rel).write_text("#!/bin/bash\nset -eu\np" + "kill -f some-game\n")
        (WORK / rel).chmod(0o755)
        self.refused([rel], "pkill -f")

    def test_staged_private_string(self):
        rel = "docs/e2e-note.txt"
        (WORK / rel).write_text("the console is %s\n" % self.token)
        r = self.refused([rel], "private pattern")
        self.assertNotIn(self.token, r.stderr)

    def test_private_string_in_the_message(self):
        rel = "docs/e2e-note.txt"
        (WORK / rel).write_text("nothing private, %s\n" % secrets.token_hex(4))
        self.refused([rel], "private pattern", msg="A note about %s\n" % self.token)

    def test_private_cant_be_skipped(self):
        rel = "docs/e2e-note.txt"
        (WORK / rel).write_text("x %s\n" % self.token)
        self.refused([rel], "private pattern", msg="A note\n\nCommit-Tests-Skip: private: no\n")

    def test_deleted_session(self):
        s = next(p for p in sorted((WORK / "gamedb/games").glob("*/rating/sessions/*/result.toml")))
        git("rm", "-q", str(s.relative_to(WORK)))
        self.refused([], "played session's evidence")

    def test_staged_file_with_unstaged_changes(self):
        rel = "docs/e2e-note.txt"
        (WORK / rel).write_text("one\n")
        git("add", rel)
        (WORK / rel).write_text("two\n")
        self.refused([], "also have unstaged changes")

    # --- good cases
    def test_a_plain_change_goes_in_and_the_trailer_goes(self):
        rel = "docs/e2e-note.txt"
        (WORK / rel).write_text("nothing private, %s\n" % secrets.token_hex(4))
        self.accepted([rel], msg="A note\n\nBody.\n\nCo-Authored-By: Claude Opus <noreply@anthropic.com>\n"
                                 "Claude-Session: https://claude.ai/code/session_x\n")
        msg = git("log", "-1", "--format=%B").stdout
        self.assertEqual(msg.strip(), "A note\n\nBody.")

    def test_a_skip_line_lets_a_deliberate_exception_in(self):
        rel = "packages/walnut/apps/walnut-games/files/lib/walnut_games.py"
        self.edit(rel, lambda t: t + '\nE2E_COLOUR = "#12ab34"\n')
        r = self.accepted([rel], msg="A colour\n\nCommit-Tests-Skip: palette: the e2e test's deliberate exception\n")
        self.assertIn("skipped by the message", r.stderr)

    def test_a_draft_with_only_a_test(self):
        g = next(d for d in self.drafts if (WORK / "gamedb/games" / d / "rating" / "test.toml").exists()
                 and not (WORK / "gamedb/games" / d / "rating" / "sessions").exists())
        rel = "gamedb/games/%s/index.md" % g
        self.edit(rel, lambda t: t.rstrip("\n") + "\n\nAn e2e line.\n")
        self.accepted([rel])

    def test_a_complete_published_page(self):
        complete = []
        for g in self.published:
            d = WORK / "gamedb/games" / g
            fm, body = front(d / "index.md")
            if isinstance(fm.get("known_issues"), list) and fm.get("license_source") and (d / "rating/test.toml").exists() \
                    and "status" not in fm and body.split("+++", 2)[2].strip():
                complete.append(g)
        if not complete:
            self.skipTest("no published game is complete yet (license_source not backfilled)")
        rel = "gamedb/games/%s/index.md" % complete[0]
        self.edit(rel, lambda t: t.rstrip("\n") + "\n\nAn e2e line.\n")
        self.accepted([rel])


if __name__ == "__main__":
    unittest.main(verbosity=1)
