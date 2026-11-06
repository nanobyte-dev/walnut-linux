#!/usr/bin/env python3
"""test_checks.py: each commit test on good and bad changes in small scratch repositories (make test's
commit-tests suite; test_e2e.py commits the same cases through the installed hooks of a clone).

Every fixture is written here at run time, never committed as a file: a bad shell script or a private
string committed as a fixture would be a real finding of the checks (and of the public export). The
private pattern and the secrets are put together from pieces for the same reason."""
import os
import secrets
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent.parent
sys.path.insert(0, str(HERE.parent))
import core  # noqa: E402
import run as runner  # noqa: E402

ENV = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
ENV.update(GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.invalid", GIT_COMMITTER_NAME="t",
           GIT_COMMITTER_EMAIL="t@example.invalid", GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)


def png(w, h):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    raw = b"".join(b"\0" + b"\0\0\0" * w for _ in range(h))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def private_token():
    """a pattern that is private only inside a test run"""
    return "pv" + "-" + secrets.token_hex(6)


class Scratch:
    """a git repository in a temporary folder, with the few repository files the checks read"""

    def __init__(self, token=None):
        self.dir = Path(tempfile.mkdtemp(prefix="commit-tests-"))
        self.git("init", "-q", "-b", "main")
        self.token = token or private_token()
        for f in ("tools/licensed.py", "tools/licensed-files.txt", "tools/ruff-version"):
            self.write(f, (REPO / f).read_bytes())
        self.write(".gitignore", "tools/public-replace.local.txt\n")
        self.write("tools/public-replace.local.txt", "regex:%s==>(private)\n" % self.token)
        self.commit("base")

    def git(self, *args, check=True):
        r = subprocess.run(["git", "-C", str(self.dir), *args], capture_output=True, text=True, env=ENV)
        if check and r.returncode:
            raise RuntimeError("git %s: %s" % (args, r.stderr))
        return r.stdout

    def write(self, rel, data, mode=None):
        p = self.dir / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        (p.write_bytes if isinstance(data, bytes) else p.write_text)(data)
        if mode:
            p.chmod(mode)

    def add(self, *rels):
        self.git("add", "--", *rels)

    def commit(self, msg):
        self.git("add", "-A")
        self.git("commit", "-q", "-m", msg)

    def run(self, only=None, message="a change\n"):
        """the findings of the staged changes: [(check, kind, path, text)]"""
        head, changes = runner.staged_changes(self.dir)
        ctx = core.Ctx(self.dir, "hook", changes, base=head, message=message)
        core.run_checks(ctx, set(only) if only else None)
        return ctx.findings

    def errors(self, only=None, message="a change\n"):
        return [f for f in self.run(only, message) if f[1] == "error"]

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)


class GeneratedBlocks(unittest.TestCase):
    def test_no_licensed_directories_no_block_check(self):
        """the public export (CI's checkout) has no licensed package directories: the blocks that list them are not judged"""
        sys.path.insert(0, str(HERE.parent / "checks"))
        gen = core.load_check("generated")
        r = Scratch()
        try:
            ctx = core.Ctx(r.dir, "all", [], message="x\n")
            ctx.check = "generated"
            gen.run(ctx, {"blocks"})
            self.assertEqual([f for f in ctx.findings if f[1] == "error"], [])
            self.assertTrue(any("licensed package directories" in f[3] for f in ctx.findings if f[1] == "notice"), ctx.findings)
            (r.dir / "gamedb/games/g/licensed/p").mkdir(parents=True)
            ctx = core.Ctx(r.dir, "all", [], message="x\n")
            ctx.check = "generated"
            gen.run(ctx, {"blocks"})
            self.assertTrue([f for f in ctx.findings if f[1] == "error"])	# with them, the check runs (and here finds no tool)
        finally:
            r.close()


class Base(unittest.TestCase):
    def setUp(self):
        self.r = Scratch()

    def tearDown(self):
        self.r.close()

    def bad(self, only, needle, message="a change\n"):
        errs = self.r.errors([only], message)
        self.assertTrue(any(needle in f[3] for f in errs), "%s: no error with %r in %r" % (only, needle, errs))
        return errs

    def good(self, only, message="a change\n"):
        errs = self.r.errors([only], message)
        self.assertEqual(errs, [], "%s: %r" % (only, errs))


GAME = '''+++
id = "%(id)s"
title = "A Game"
%(extra)s+++
%(body)s'''


def game(gid, extra="", body="A page.\n"):
    return GAME % {"id": gid, "extra": extra, "body": body}


COMPLETE = 'known_issues = []\nlicense_source = "debian/copyright of the Debian package"\n'


class Selection(unittest.TestCase):
    def test_globs(self):
        self.assertTrue(core.matches("a/b/c.toml", ("**/*.toml",)))
        self.assertTrue(core.matches("c.toml", ("**/*.toml",)))
        self.assertFalse(core.matches("gamedb/games/x/y/index.md", ("gamedb/games/*/index.md",)))

    def test_shared_input_runs_all_targets(self):
        class C:
            NAME = "toml"
            TARGETS = ("**/*.toml",)
            SHARED_INPUTS = ("schema.txt",)
        ctx = core.Ctx(REPO, "hook", [core.Change("M", "schema.txt")])
        ctx._tracked = ["a.toml", "b/c.toml", "x.md"]
        go, files = core.select(C, ctx)
        self.assertTrue(go)
        self.assertEqual(sorted(c.path for c in files), ["a.toml", "b/c.toml"])
        ctx = core.Ctx(REPO, "hook", [core.Change("M", "a.toml")])
        ctx._tracked = ["a.toml", "b/c.toml"]
        self.assertEqual([c.path for c in core.select(C, ctx)[1]], ["a.toml"])

    def test_renames_and_deletes(self):
        ch = core.parse_name_status("R087\0old/a.toml\0new/a.toml\0D\0gone.txt\0M\0m.txt\0")
        self.assertEqual([(c.status, c.path, c.old) for c in ch],
                         [("R", "new/a.toml", "old/a.toml"), ("D", "gone.txt", None), ("M", "m.txt", None)])

    def test_skip_lines(self):
        ctx = core.Ctx(REPO, "hook", [], message="x\n\nCommit-Tests-Skip: palette: the palette itself\n")
        self.assertEqual(ctx.skips(), {"palette": "the palette itself"})


class Trailers(unittest.TestCase):
    def test_strip(self):
        msg = "Fix it\n\nBody.\n\nCo-Authored-By: Claude Opus <noreply@anthropic.com>\nClaude-Session: https://x/y\n"
        out, n = runner.strip_trailers(msg)
        self.assertEqual((out, n), ("Fix it\n\nBody.\n", 2))
        human = "Fix\n\nCo-Authored-By: Jane Doe <jane@example.org>\n"
        self.assertEqual(runner.strip_trailers(human), (human, 0))


class Toml(Base):
    def test_bad(self):
        self.r.write("a.toml", "x = \n")
        self.r.add("a.toml")
        self.bad("toml", "doesn't parse")

    def test_good(self):
        self.r.write("a.toml", "x = 1\n")
        self.r.add("a.toml")
        self.good("toml")


class Python(Base):
    def test_bad_compile_and_ruff(self):
        self.r.write("a.py", "def f(:\n")
        self.r.write("b.py", "import os\n\nx = undefined_name\n")
        self.r.add("a.py", "b.py")
        errs = self.bad("python", "doesn't compile")
        self.assertTrue(any("F401" in e[3] for e in errs) and any("F821" in e[3] for e in errs), errs)
        self.assertFalse((self.r.dir / "__pycache__").exists())

    def test_script_without_extension(self):
        self.r.write("tool", "#!/usr/bin/env python3\nimport sys\n")
        self.r.add("tool")
        self.bad("python", "F401")

    def test_good(self):
        self.r.write("a.py", "import os\n\nprint(os.sep)\n")
        self.r.add("a.py")
        self.good("python")


class Shell(Base):
    def script(self, body, name="s.sh"):
        self.r.write(name, "#!/bin/bash\nset -euo pipefail\n" + body, 0o755)
        self.r.add(name)

    def test_pkill_f(self):
        self.script("p" + "kill -f some-game\n")
        self.bad("shell", "pkill -f")

    def test_pgrep_f_in_comment_is_fine(self):
        self.script("# don't use p" + "grep -f here\necho ok\n")
        self.good("shell")

    def test_anchored_pgrep_f_is_fine(self):
        self.script("p" + "grep -u 1000 -x -f '(/[^ ]*/)?pcmanfm --desktop' >/dev/null || :\n"
                    "p" + "kill -u 1000 -f '^sleep 600$' || :\n")
        self.good("shell")

    def test_count_or_true_is_fine(self):
        self.script("n=$(apt-get -s install x | grep -cE '^Inst' || true)\necho \"$n\"\n")
        self.good("shell")

    def test_or_true_after_apt_get(self):
        self.script("apt-get install -y foo || true\n")
        self.bad("shell", "apt-get")

    def test_or_true_after_docker_run_continued(self):
        self.script("docker run --rm \\\n  img true || true\n")
        self.bad("shell", "docker run")

    def test_or_true_after_cp_of_build_output(self):
        self.script('cp "$BUILD"/debs/x.deb /tmp/ || true\n')
        self.bad("shell", "cp of build output")

    def test_find_root(self):
        self.script("fi" + "nd / -name x\n")
        self.bad("shell", "find /")
        self.r.write("s.sh", "#!/bin/bash\nfi" + "nd /mnt -name x\n")
        self.r.add("s.sh")
        self.bad("shell", "find /")

    def test_grep_q_under_pipefail_warns(self):
        self.script('x=$(echo a)\necho "$x" | grep -q a && echo yes\n')
        self.good("shell")
        self.assertTrue(any(f[1] == "warning" and "grep -q" in f[3] for f in self.r.run(["shell"])))

    def test_syntax(self):
        self.script("if then\n")
        self.bad("shell", "doesn't parse")

    def test_shellcheck_error(self):
        self.script('for f in $(ls /tmp); do echo "$f"; done\n')
        self.bad("shell", "shellcheck")

    def test_good(self):
        self.script('find "${1:-.}" -name "*.txt" -print\napt-get install -y foo\n')
        self.good("shell")


class NorDumpContent(Base):
    """the NOR dump by content: a fake dump (random, 2 MiB) renamed, compressed, zipped or tarred"""

    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(REPO / "tools"))
        import nor_fingerprint
        cls.nf = nor_fingerprint
        cls.dump = os.urandom(2 << 20)

    def setUp(self):
        super().setUp()
        d = Path(tempfile.mkdtemp(prefix="nor-fp-"))
        self.addCleanup(shutil.rmtree, d, True)
        (d / "dump").write_bytes(self.dump)
        (self.r.dir / "tools/nor-fingerprint.local.txt").write_text(self.nf.make(str(d / "dump")))

    def stage(self, name, data):
        self.r.write(name, data)
        self.r.add(name)

    def test_renamed_copy(self):
        self.stage("tests/fixtures/flash.dat", self.dump)
        self.bad("private", "the NOR dump by content")

    def test_compressed_and_archived_copies(self):
        import bz2, gzip, io, lzma, tarfile, zipfile
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("x/nor", self.dump)
        tb = io.BytesIO()
        with tarfile.open(fileobj=tb, mode="w:gz") as t:
            ti = tarfile.TarInfo("backups/nor")
            ti.size = len(self.dump)
            t.addfile(ti, io.BytesIO(self.dump))
        for name, data in (("a.gz", gzip.compress(self.dump)), ("a.xz", lzma.compress(self.dump)), ("a.bz2", bz2.compress(self.dump)),
                           ("a.zip", buf.getvalue()), ("a.tgz", tb.getvalue()), ("noext", gzip.compress(self.dump))):
            with self.subTest(name):
                self.r.git("reset", "-q")
                self.stage("t/" + name, data)
                errs = self.r.errors(["private"])
                self.assertTrue(any("the NOR dump by content" in f[3] and f[2] == "t/" + name for f in errs), (name, errs))

    def test_truncated_copy_with_a_window(self):
        self.stage("t/part.dat", self.dump[:1 << 21])
        self.bad("private", "the NOR dump by content")

    def test_every_member_of_multi_member_files_is_read(self):
        import bz2, gzip, lzma
        a = os.urandom(70000)
        for name, data in (("m.gz", gzip.compress(a) + gzip.compress(self.dump)), ("m.xz", lzma.compress(a) + lzma.compress(self.dump)),
                           ("m.bz2", bz2.compress(a) + bz2.compress(self.dump))):
            with self.subTest(name):
                self.r.git("reset", "-q")
                self.stage("t/" + name, data)
                self.assertTrue(any("the NOR dump by content" in f[3] for f in self.r.errors(["private"])), name)

    def _zstd_cases(self):
        try:
            from compression import zstd
        except ImportError:
            return ()
        return (("g.zst", zstd.compress(self.dump) + b"junk" * 50),)

    def test_trailing_padding_or_garbage_keeps_earlier_hits(self):
        import bz2, gzip, lzma
        for name, data in (("p.gz", gzip.compress(self.dump) + b"\0" * 512), ("g.gz", gzip.compress(self.dump) + b"junk" * 50),
                           ("p.xz", lzma.compress(self.dump) + b"\0" * 512), ("p.bz2", bz2.compress(self.dump) + b"\0" * 512),
                           ("two.xz", lzma.compress(os.urandom(70000)) + b"\0" * 8 + lzma.compress(self.dump))) + self._zstd_cases():
            with self.subTest(name):
                self.r.git("reset", "-q")
                self.stage("t/" + name, data)
                self.assertTrue(any("the NOR dump by content" in f[3] for f in self.r.errors(["private"])), name)

    def test_walk_budget_covers_all_members_and_levels(self):
        """a small zip that expands far past the cap: the walk stops with a note and never holds more than the cap"""
        import io, tracemalloc, zipfile
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
            for i in range(40):
                z.writestr("m%d" % i, b"\0" * (4 << 20))
        self.assertLess(len(buf.getvalue()), 400 << 10)
        nf = self.nf
        tracemalloc.start()
        hits, notes = nf.check_blob([], buf.getvalue(), "bomb.zip", budget=nf.Budget(4 << 20))
        peak = tracemalloc.get_traced_memory()[1]
        tracemalloc.stop()
        self.assertLess(peak, 24 << 20, peak)
        self.assertTrue(any("the rest was not looked into" in n for n in notes), notes)
        # the same through the hook: a warning, not a crash
        self.stage("t/bomb.zip", buf.getvalue())
        self.assertEqual(self.r.errors(["private"]), [])

    def test_lfs_pointer_follows_the_object(self):
        import hashlib
        oid = hashlib.sha256(self.dump).hexdigest()
        obj = self.r.dir / ".git/lfs/objects" / oid[:2] / oid[2:4] / oid
        obj.parent.mkdir(parents=True)
        obj.write_bytes(self.dump)
        ptr = "version https://git-lfs.github.com/spec/v1\noid sha256:%s\nsize %d\n" % (oid, len(self.dump))
        self.stage("gamedb/games/g/rating/sessions/s/shots/a.jpg", ptr)
        self.bad("private", "the NOR dump by content")

    def test_lfs_pointer_without_object_warns(self):
        ptr = "version https://git-lfs.github.com/spec/v1\noid sha256:%s\nsize 9\n" % ("ab" * 32)
        self.stage("gamedb/games/g/rating/sessions/s/shots/a.jpg", ptr)
        self.assertEqual(self.r.errors(["private"]), [])
        self.assertTrue(any("LFS store" in f[3] for f in self.r.run(["private"])))

    def test_other_binary_is_fine(self):
        self.stage("t/other.dat", os.urandom(2 << 20))
        self.assertEqual(self.r.errors(["private"]), [])

    def test_dump_in_backups_without_a_fingerprint_warns_loudly(self):
        (self.r.dir / "tools/nor-fingerprint.local.txt").unlink()
        self.r.write("backups/dump.bin", os.urandom(1 << 20))
        self.stage("t/a.txt", "x\n")
        w = [f for f in self.r.run(["private"]) if f[1] == "warning"]
        self.assertTrue(any("content guard is OFF" in f[3] for f in w), w)
        self.assertEqual(self.r.errors(["private"]), [])

    def test_no_fingerprint_only_warns(self):
        (self.r.dir / "tools/nor-fingerprint.local.txt").unlink()
        self.stage("t/flash.dat", self.dump)
        self.assertEqual(self.r.errors(["private"]), [])
        self.assertTrue(any("no tools/nor-fingerprint.local.txt" in f[3] for f in self.r.run(["private"])))


class GameData(Base):
    def stage(self, name, data=b"x\n"):
        self.r.write(name, data)
        self.r.add(name)

    def test_names_in_any_case(self):
        for n in ("a/DOOM.WAD", "a/Diablo/DIABDAT.MPQ", "a/heroes2.agg", "b/pak0.PK3", "c/DUKE3D.GRP", "d/ID1/PAK0.PAK", "e/MONKEY.000"):
            with self.subTest(n):
                self.r.git("reset", "-q")
                self.stage(n)
                self.assertTrue(any("game data" in f[3] for f in self.r.errors(["private"])), n)

    def test_magic_under_another_name(self):
        pack = b"PACK" + struct.pack("<ii", 12, 64) + b"\0" * 64
        for n, data in (("a/d.bin2", b"IWAD" + b"\0" * 20), ("a/e.dat2", b"MPQ\x1a" + b"\0" * 20), ("a/f.dat2", pack)):
            with self.subTest(n):
                self.r.git("reset", "-q")
                self.stage(n, data)
                self.assertTrue(any("first bytes" in f[3] for f in self.r.errors(["private"])), n)

    def test_new_names(self):
        for n in ("a/GM_TT00.GM", "a/fm/9_tt_a.fm", "rct2/Data/CSS1.DAT", "rct2/ObjData/X.DAT", "saves/park.SV6", "saves/p.td6"):
            with self.subTest(n):
                self.r.git("reset", "-q")
                self.stage(n)
                self.assertTrue(any("game data" in f[3] for f in self.r.errors(["private"])), n)

    def test_changed_file_gets_the_magic_check(self):
        self.stage("a/blob.dat", b"just data\n")
        self.r.commit("base data")
        self.stage("a/blob.dat", b"IWAD" + b"\0" * 20)
        self.assertTrue(any("first bytes" in f[3] for f in self.r.errors(["private"])))

    def test_inside_archives(self):
        import io, zipfile
        pack = b"PACK" + struct.pack("<ii", 12, 64) + b"\0" * 64
        for label, members in (("by name", {"ID1/PAK0.PAK": b"x"}), ("by magic", {"q/a.bin": pack}), ("name and magic", {"w.wad": b"IWAD0000"})):
            with self.subTest(label):
                buf = io.BytesIO()
                with zipfile.ZipFile(buf, "w") as z:
                    for k, v in members.items():
                        z.writestr(k, v)
                    z.writestr("pad.bin", os.urandom(70000))
                data = buf.getvalue()
                self.r.git("reset", "-q")
                self.stage("t/%s.zip" % label.replace(" ", "-"), data)
                self.assertTrue(any("game data" in f[3] or "first bytes" in f[3] for f in self.r.errors(["private"])), label)

    def test_local_files_cannot_be_forced_in(self):
        self.r.write("tools/public-replace.local.txt", "x\n")
        self.r.write("tools/other.local.txt", "x\n")
        self.r.git("add", "-f", "tools/other.local.txt")
        errs = self.bad("private", "a private local file")
        self.assertTrue(any(e[2] == "tools/other.local.txt" for e in errs))
        self.r.git("reset", "-q")
        self.r.write("config.local.mk.example", "x\n")
        self.r.add("config.local.mk.example")
        self.assertFalse(any("private local file" in e[3] for e in self.r.errors(["private"])))

    def test_text_starting_with_pack_is_fine(self):
        self.stage("a/notes.txt", b"PACKAGING notes\n")
        self.assertEqual(self.r.errors(["private"]), [])

    def test_allow_list_by_exact_path(self):
        self.stage("tools/game-data-allow.txt", "# ours\ntests/own.wad\n")
        self.stage("tests/own.wad")
        self.assertEqual(self.r.errors(["private"]), [])
        self.stage("tests/other.wad")
        self.assertTrue(self.r.errors(["private"]))


class Private(Base):
    def test_added_line(self):
        self.r.write("doc.md", "the console's id is %s\n" % self.r.token)
        self.r.add("doc.md")
        errs = self.bad("private", "private pattern")
        self.assertFalse(any(self.r.token in e[3] for e in errs), "a finding must not repeat the text")

    def test_message(self):
        self.r.write("doc.md", "fine\n")
        self.r.add("doc.md")
        self.bad("private", "private pattern", message="about %s\n" % self.r.token)

    def test_cant_be_skipped(self):
        self.r.write("doc.md", "x %s\n" % self.r.token)
        self.r.add("doc.md")
        self.bad("private", "private pattern", message="x\n\nCommit-Tests-Skip: private: please\n")

    def test_replace_list_required(self):
        (self.r.dir / "tools/public-replace.local.txt").unlink()
        self.r.write("doc.md", "fine\n")
        self.r.add("doc.md")
        self.bad("private", "nothing can be checked")

    def test_local_list(self):
        self.r.write("tools/commit-private.local.txt", "literal:%s-local\n" % self.r.token)
        self.r.write("doc.md", "x %s-local\n" % self.r.token)
        self.r.add("doc.md")
        errs = self.bad("private", "commit-private.local.txt line 1")
        self.assertTrue(errs)

    def test_secret(self):
        self.r.write("k.txt", "-" * 5 + "BEGIN OPENSSH " + "PRIVATE KEY" + "-" * 5 + "\nabc\n")
        self.r.add("k.txt")
        self.bad("private", "private key")

    def test_login_lines(self):
        self.r.write("auth.conf", "machine example.org login me pass" + "word hunter2x\n")
        self.r.write("doc.sh", "printf 'machine h login %s password %s' \"$u\" \"$p\"\n# machine h login <user> password <password>\n")
        self.r.add("auth.conf", "doc.sh")
        self.assertEqual({e[2] for e in self.r.errors(["private"])}, {"auth.conf"})

    def test_paths(self):
        self.r.write("backups/nor.txt", "x\n")
        self.r.write("firmware/x.txt", "x\n")
        self.r.git("add", "-f", "backups/nor.txt", "firmware/x.txt")
        self.bad("private", "a private path")

    def test_rating_image_not_lfs(self):
        self.r.write("gamedb/games/g/rating/sessions/s/shots/ps3-a.jpg", b"\xff\xd8\xff not a pointer")
        self.r.add("gamedb/games/g/rating/sessions/s/shots/ps3-a.jpg")
        self.bad("private", "Git LFS pointer")

    def test_rating_image_pointer_is_fine(self):
        self.r.write("gamedb/games/g/rating/sessions/s/shots/ps3-a.jpg",
                     b"version https://git-lfs.github.com/spec/v1\noid sha256:" + b"0" * 64 + b"\nsize 1\n")
        self.r.add("gamedb/games/g/rating/sessions/s/shots/ps3-a.jpg")
        self.good("private")

    def test_licensed_path(self):
        self.r.write("packages/x/data/eboot.prx", b"x")
        self.r.add("packages/x/data/eboot.prx")
        self.bad("private", "licensed game data")

    def test_message_of_an_empty_commit(self):
        self.bad("private", "private pattern", message="about %s\n" % self.r.token)

    def linked_worktree(self):
        wt = self.r.dir.parent / (self.r.dir.name + "-wt")
        self.r.git("worktree", "add", "-q", "-b", "other", str(wt))
        self.addCleanup(shutil.rmtree, wt, True)
        return wt

    def worktree_errors(self, wt):
        head, changes = runner.staged_changes(wt)
        ctx = core.Ctx(wt, "hook", changes, base=head, message="a change\n")
        core.run_checks(ctx, {"private"})
        return [f for f in ctx.findings if f[1] == "error"]

    def test_linked_worktree_reads_the_main_worktrees_list(self):
        wt = self.linked_worktree()
        self.assertFalse((wt / "tools/public-replace.local.txt").exists())   # untracked: only the main one has it
        (wt / "doc.md").write_text("the console's id is %s\n" % self.r.token)
        subprocess.run(["git", "-C", str(wt), "add", "doc.md"], check=True, env=ENV)
        errs = self.worktree_errors(wt)
        self.assertTrue(any("private pattern" in f[3] for f in errs), errs)
        self.assertFalse(any("nothing can be checked" in f[3] for f in errs), errs)
        (wt / "doc.md").write_text("fine\n")
        subprocess.run(["git", "-C", str(wt), "add", "doc.md"], check=True, env=ENV)
        self.assertEqual(self.worktree_errors(wt), [])

    def test_linked_worktree_without_any_list_is_still_refused(self):
        wt = self.linked_worktree()
        (self.r.dir / "tools/public-replace.local.txt").unlink()
        (wt / "doc.md").write_text("fine\n")
        subprocess.run(["git", "-C", str(wt), "add", "doc.md"], check=True, env=ENV)
        self.assertTrue(any("nothing can be checked" in f[3] for f in self.worktree_errors(wt)))

    # G6: $WALNUT_PUBLIC_REPLACE adds to the default list, never replaces it; the list should be mode 600
    def with_env(self, value):
        old = os.environ.get("WALNUT_PUBLIC_REPLACE")
        os.environ["WALNUT_PUBLIC_REPLACE"] = value

        def restore():
            if old is None:
                os.environ.pop("WALNUT_PUBLIC_REPLACE", None)
            else:
                os.environ["WALNUT_PUBLIC_REPLACE"] = old
        self.addCleanup(restore)

    def test_env_list_adds_to_the_default(self):
        extra = self.r.dir / "extra-list.txt"
        extra.write_text("literal:%s-extra\n" % self.r.token)
        self.with_env(str(extra))
        self.r.write("a.md", "x %s-extra\n" % self.r.token)
        self.r.write("b.md", "y %s\n" % self.r.token)     # the default list's pattern still counts
        self.r.add("a.md", "b.md")
        errs = self.bad("private", "private pattern")
        self.assertEqual({e[2] for e in errs}, {"a.md", "b.md"})

    def test_env_list_alone_when_the_clone_has_none(self):
        extra = self.r.dir / "extra-list.txt"
        extra.write_text("literal:%s-extra\n" % self.r.token)
        (self.r.dir / "tools/public-replace.local.txt").unlink()
        self.with_env(str(extra))
        self.r.write("a.md", "x %s-extra\n" % self.r.token)
        self.r.add("a.md")
        self.bad("private", "private pattern")

    def test_list_readable_by_others_warns(self):
        lst = self.r.dir / "tools/public-replace.local.txt"
        self.r.write("doc.md", "fine\n")
        self.r.add("doc.md")
        lst.chmod(0o644)
        self.assertTrue(any(f[1] == "warning" and "readable by others" in f[3] for f in self.r.run(["private"])))
        lst.chmod(0o600)
        self.assertFalse([f for f in self.r.run(["private"]) if f[1] == "warning"])

    # G5: the report takes private text out of every check's findings
    def test_report_redacts_private_text(self):
        import io
        ctx = core.Ctx(self.r.dir, "hook", [], message="x\n")
        ctx.check = "shell"
        ctx.error("dir-%s/a.sh" % self.r.token, "line 3: echo %s is not allowed" % self.r.token)
        ctx.warn(None, "token ghp_" + "a" * 36 + " in the text")
        ctx.error("clean.sh", "plain finding")
        out = io.StringIO()
        self.assertEqual(core.report(ctx, out=out), 2)
        text = out.getvalue()
        self.assertNotIn(self.r.token, text)
        self.assertNotIn("ghp_", text)
        self.assertIn("dir-<private>/a.sh: line 3: echo <private> is not allowed", text)
        self.assertIn("plain finding", text)

    def test_report_without_a_list_still_prints(self):
        import io
        (self.r.dir / "tools/public-replace.local.txt").unlink()
        ctx = core.Ctx(self.r.dir, "hook", [], message="x\n")
        ctx.check = "shell"
        ctx.error("a.sh", "something")
        out = io.StringIO()
        core.report(ctx, out=out)
        self.assertIn("something", out.getvalue())

    def test_empty_replace_list(self):
        self.r.write("tools/public-replace.local.txt", "\n")
        self.bad("private", "has no pattern")

    def test_nested_private_dir_and_dump_names(self):
        self.r.write("x/backups/a.txt", "x\n")
        self.r.write("dump/flash.bin", b"\0" * 16)
        self.r.git("add", "-f", "x/backups/a.txt", "dump/flash.bin")
        errs = self.bad("private", "a private path")
        self.assertTrue(any("flash dump" in e[3] for e in errs))

    def test_binary_and_odd_paths(self):
        self.r.write("blob.dat", b"\0\1" + self.r.token.encode() + b"\0")
        self.r.write('we"ird\tname.txt', "x %s\n" % self.r.token)
        self.r.write("plus.txt", "++ a line that looks like a header\nx %s\n" % self.r.token)
        self.r.add("blob.dat", 'we"ird\tname.txt', "plus.txt")
        where = {e[2] for e in self.r.errors(["private"])}
        self.assertEqual(where, {"blob.dat", 'we"ird\tname.txt', "plus.txt"})

    def test_scissors(self):
        self.r.write("doc.md", "fine\n")
        self.r.add("doc.md")
        self.good("private", message="msg\n# ------------------------ >8 ------------------------\n-%s\n" % self.r.token)

    def test_good(self):
        self.r.write("doc.md", "nothing to see\n")
        self.r.add("doc.md")
        self.good("private")


class Deletions(Base):
    def setUp(self):
        super().setUp()
        self.r.write("gamedb/games/pub/index.md", game("pub"))
        self.r.write("gamedb/games/drf/index.md", game("drf", 'draft = true\nstatus = "candidate"\n'))
        self.r.write("gamedb/games/pub/rating/sessions/s1/result.toml", "schema = 1\n")
        self.r.write("gamedb/games/pub/benchmark/results/b1.toml", "x = 1\n")
        self.r.write("gamedb/benchmarks/batches.toml", '[[batch]]\nname = "b1"\n\n[[batch]]\nname = "b2"\n')
        self.r.write("packages/p/patches/series", "0001-a.patch\nlocal/0002-b.patch\n")
        self.r.write("packages/p/patches/0001-a.patch", "x\n")
        self.r.write("packages/p/patches/local/0002-b.patch", "x\n")
        self.r.write("packages/p/patches/0003-old.patch", "x\n")
        self.r.commit("fixtures")

    def test_published_bundle(self):
        self.r.git("rm", "-q", "gamedb/games/pub/index.md")
        self.bad("deletions", "published bundle")

    def test_draft_bundle_is_fine(self):
        self.r.git("rm", "-q", "gamedb/games/drf/index.md")
        self.good("deletions")

    def test_session_and_records(self):
        self.r.git("rm", "-q", "gamedb/games/pub/rating/sessions/s1/result.toml", "gamedb/games/pub/benchmark/results/b1.toml")
        errs = self.bad("deletions", "session")
        self.assertTrue(any("benchmark records" in e[3] for e in errs))

    def test_batch_removed(self):
        self.r.write("gamedb/benchmarks/batches.toml", '[[batch]]\nname = "b1"\n')
        self.r.add("gamedb/benchmarks/batches.toml")
        self.bad("deletions", "'b2' removed")

    def test_patch_in_series(self):
        self.r.git("rm", "-q", "packages/p/patches/local/0002-b.patch")
        self.bad("deletions", "still names it")

    def test_patch_out_of_series_and_rename(self):
        self.r.git("rm", "-q", "packages/p/patches/0003-old.patch")
        self.r.git("mv", "gamedb/games/pub/rating/sessions/s1/result.toml", "gamedb/games/pub/rating/sessions/s1/result2.toml")
        self.good("deletions")


class Gamedb(Base):
    """the new rules only (check.py itself is tested by tools/gamedb/test_gamedb.py, and run end to end)"""

    def errors(self):
        return [e for e in self.r.errors(["gamedb"]) if not e[3].startswith("check.py")]

    def test_incomplete_published(self):
        self.r.write("gamedb/games/g/index.md", game("g", 'status = "tested"\n', body="\n"))
        self.r.add("gamedb/games/g/index.md")
        text = " | ".join(e[3] for e in self.errors())
        for needle in ("page text", "known_issues", "license_source", "status is a draft", "rating/test.toml", "thumb.png"):
            self.assertIn(needle, text)

    def test_complete_published(self):
        self.r.write("gamedb/games/g/index.md", game("g", COMPLETE))
        self.r.write("gamedb/games/g/rating/test.toml", "x = 1\n")
        self.r.write("gamedb/games/g/thumb.png", png(256, 144))
        self.r.add("gamedb/games")
        self.assertEqual(self.errors(), [])

    def test_publish_needs_256x144_thumb(self):
        self.r.write("gamedb/games/g/index.md", game("g", 'draft = true\nstatus = "candidate"\n'))
        self.r.write("gamedb/games/g/rating/test.toml", "x = 1\n")
        self.r.write("gamedb/games/g/thumb.png", png(320, 200))
        self.r.commit("draft")
        self.r.write("gamedb/games/g/index.md", game("g", COMPLETE))
        self.r.add("gamedb/games/g/index.md")
        self.assertTrue(any("256x144" in e[3] for e in self.errors()))
        self.r.write("gamedb/games/g/thumb.png", png(256, 144))
        self.r.add("gamedb/games/g/thumb.png")
        self.assertEqual(self.errors(), [])

    def test_draft_with_sessions(self):
        self.r.write("gamedb/games/g/index.md", game("g", 'draft = true\nstatus = "candidate"\n'))
        self.r.write("gamedb/games/g/rating/sessions/s/result.toml", "schema = 1\n")
        self.r.add("gamedb/games")
        self.assertTrue(any("draft with played sessions" in e[3] for e in self.errors()))

    def test_draft_with_only_a_test(self):
        self.r.write("gamedb/games/g/index.md", game("g", 'draft = true\nstatus = "candidate"\n', body=""))
        self.r.write("gamedb/games/g/rating/test.toml", "x = 1\n")
        self.r.add("gamedb/games")
        self.assertEqual(self.errors(), [])


class Batches(Base):
    B = '[[batch]]\nname = "%s"\n%s'

    def test_new_without_conclusion(self):
        self.r.write("gamedb/benchmarks/batches.toml", self.B % ("b1", 'tested = "x"\n'))
        self.r.add("gamedb/benchmarks/batches.toml")
        self.bad("batches", "no conclusion")

    def test_old_batch_untouched_is_fine(self):
        self.r.write("gamedb/benchmarks/batches.toml", self.B % ("b1", ""))
        self.r.commit("old")
        self.r.write("gamedb/benchmarks/batches.toml", self.B % ("b1", "") + "\n" + self.B % ("b2", 'tested = "x"\nconclusion = "y"\n'))
        self.r.add("gamedb/benchmarks/batches.toml")
        self.good("batches")
        self.r.write("gamedb/benchmarks/batches.toml", self.B % ("b1", 'runs = 2\n'))
        self.r.add("gamedb/benchmarks/batches.toml")
        self.bad("batches", "changed")


class Desktop(Base):
    D = "[Desktop Entry]\nType=Application\nName=%s\nExec=%s\n%s"

    def test_duplicate_names(self):
        d = "gamedb/games/g/desktop/launchers/"
        self.r.write(d + "a.desktop", self.D % ("G", "ps3-game g -a", ""))
        self.r.write(d + "b.desktop", self.D % ("G", "ps3-game g -b", ""))
        self.r.add("gamedb")
        self.bad("desktop", "named 'G'")

    def test_hidden_duplicate_is_fine(self):
        d = "gamedb/games/g/desktop/launchers/"
        self.r.write(d + "a.desktop", self.D % ("G", "ps3-game g -a", ""))
        self.r.write(d + "b.desktop", self.D % ("G", "ps3-game g -b", "NoDisplay=true\n"))
        self.r.add("gamedb")
        self.good("desktop")

    def test_file_handler_visible(self):
        d = "gamedb/games/g/desktop/launchers/"
        self.r.write(d + "a.desktop", self.D % ("G", "ps3-game g", ""))
        self.r.write(d + "open.desktop", self.D % ("G (open)", "ps3-game g %f", "MimeType=x/y;\n"))
        self.r.add("gamedb")
        self.bad("desktop", "file handler")

    def test_one_entry_with_a_url_is_a_launcher(self):
        self.r.write("packages/a/package.conf", "TYPE=native\n")
        self.r.write("packages/a/files/a.desktop", self.D % ("A", "a %u", "MimeType=x-scheme-handler/a;\n"))
        self.r.add("packages")
        self.good("desktop")


class Versions(Base):
    def conf(self, text, path="packages/patched/x/libfoo/package.conf"):
        self.r.write(path, text)
        self.r.add(path)

    def test_epoch_added(self):
        self.conf("TYPE=debsrc\nSOURCE=libfoo\nVERSION=1.3.7-3\n")
        self.r.commit("pkg")
        self.conf("TYPE=debsrc\nSOURCE=libfoo\nVERSION=1:1.3.7-3\n")
        self.bad("versions", "epoch")

    def test_debian_epoch_kept(self):
        self.conf("TYPE=debsrc\nSOURCE=libfoo\nVERSION=1:1.3.7-3\n")
        self.r.commit("pkg")
        self.conf("TYPE=debsrc\nSOURCE=libfoo\nVERSION=1:1.3.8-1\n")
        self.good("versions")

    def test_epoch_from_the_dsc(self):
        self.r.write("build/sources/libfoo/libfoo_1.3.7-3.dsc", "Format: 3.0\nVersion: 1:1.3.7-3\n")
        self.conf("TYPE=debsrc\nSOURCE=libfoo\nVERSION=1:1.3.7-3\n")
        self.good("versions")

    def test_ps3_in_version(self):
        self.conf("TYPE=debsrc\nSOURCE=libfoo\nVERSION=1.3.7-3+ps3.1\n")
        self.bad("versions", "Debian's version as it is")

    def test_upstream(self):
        self.conf("TYPE=upstream\nSOURCE=foo\nUPVER=2:1.0\nDEBREV=2\n")
        errs = self.bad("versions", "epoch")
        self.assertTrue(any("DEBREV" in e[3] for e in errs))

    def test_native_name(self):
        self.conf("TYPE=native\nSOURCE=libpng\n", "packages/walnut/x/libpng/package.conf")
        self.bad("versions", "only for a name that is ours")

    def test_control_relation(self):
        self.r.write("packages/a/debian/control", "Depends: x (>= 1.0-1ps3)\n")
        self.r.add("packages")
        self.bad("versions", "names a ps3 build")

    def test_good(self):
        self.conf("TYPE=native\nSOURCE=ps3-thing\n", "packages/walnut/x/ps3-thing/package.conf")
        self.r.write("packages/walnut/x/ps3-thing/debian/control", "Depends: plymouth (>= 26.1-1+ps3), y (>= 0.0.1+dfsg-1+ps3.12~)\n")
        self.conf("TYPE=upstream\nSOURCE=foo\nUPVER=1.0~git1+dfsg\nDEBREV=1\n", "packages/repackaged/foo/package.conf")
        self.r.add("packages")
        self.good("versions")


class KeyExpiry(Base):
    """keyrings are made here with gpg at run time (a fixture file would be a real key in the tree)"""

    def keyring(self, expiry):
        home = tempfile.mkdtemp(prefix="gnupg-")
        try:
            def gpg(*a):
                return subprocess.run(["gpg", "--homedir", home, "--batch", "--passphrase", "", "--pinentry-mode", "loopback", *a],
                                      capture_output=True, check=True)
            gpg("--quick-gen-key", "fixture <f@example.invalid>", "ed25519", "cert", "never")
            fpr = gpg("--list-keys", "--with-colons").stdout.decode().split("fpr:::::::::")[1].split(":")[0]
            gpg("--quick-add-key", fpr, "ed25519", "sign", expiry)
            return gpg("--export").stdout
        finally:
            shutil.rmtree(home, ignore_errors=True)

    def check(self, today):
        self.r.write("apt-repo/keyrings/archive-keyring.gpg", self.keyring("2027-10-21"))
        self.r.add("apt-repo")
        old = os.environ.get("WALNUT_TODAY")
        os.environ["WALNUT_TODAY"] = today
        try:
            return [f for f in self.r.run(["keyexpiry"]) if f[0] == "keyexpiry"]
        finally:
            if old is None:
                del os.environ["WALNUT_TODAY"]
            else:
                os.environ["WALNUT_TODAY"] = old

    def test_far_off(self):
        self.assertEqual(self.check("2026-11-02"), [])

    def test_warns_inside_120_days(self):
        f = self.check("2027-07-01")
        self.assertEqual([x[1] for x in f], ["warning"])
        self.assertIn("2027-10-21", f[0][3])

    def test_errors_inside_30_days(self):
        self.assertEqual([x[1] for x in self.check("2027-10-01")], ["error"])


class Reminders(Base):
    TOML = '[[reminder]]\ndue = "2026-11-06"\nwhat = "do the thing"\n\n[[reminder]]\ndue = "2027-03-01"\nwhat = "later"\n'

    def check(self, today, text=None):
        self.r.write("docs/reminders.toml", self.TOML if text is None else text)
        self.r.add("docs")
        old = os.environ.get("WALNUT_TODAY")
        os.environ["WALNUT_TODAY"] = today
        try:
            return [f for f in self.r.run(["reminders"]) if f[0] == "reminders"]
        finally:
            if old is None:
                del os.environ["WALNUT_TODAY"]
            else:
                os.environ["WALNUT_TODAY"] = old

    def test_future_is_quiet(self):
        self.assertEqual(self.check("2026-11-05"), [])

    def test_past_warns_only_for_the_past_one(self):
        f = self.check("2026-11-10")
        self.assertEqual([x[1] for x in f], ["warning"])
        self.assertIn("do the thing", f[0][3])

    def test_bad_schema_is_an_error(self):
        for text in ('[[reminder]]\ndue = "soon"\nwhat = "x"\n', '[[reminder]]\ndue = "2026-11-06"\n',
                     '[[reminder]]\ndue = 2026-11-06\nwhat = "x"\n', '[[reminder]]\ndue = "2026-11-06"\nwhat = ""\n'):
            self.assertEqual([x[1] for x in self.check("2026-01-01", text)], ["error"], text)


class Palette(Base):
    def test_bad(self):
        self.r.write("packages/walnut/apps/a/files/lib/a.py", 'BG = "#1a2b3c"\n')
        self.r.add("packages")
        self.bad("palette", "#1a2b3c")

    def test_good(self):
        self.r.write("packages/walnut/apps/a/files/lib/a_palette.py", 'BG = "#1a2b3c"\n')
        self.r.write("packages/walnut/apps/a/tests/test_a.py", 'X = "#fff"\n')
        self.r.write("packages/walnut/apps/a/files/lib/a.py", "# see issue #123 and #12\nBG = P.BG\n")
        self.r.add("packages")
        self.good("palette")


class Skips(Base):
    def test_skip_line(self):
        self.r.write("packages/walnut/apps/a/files/lib/a.py", 'BG = "#1a2b3c"\n')
        self.r.add("packages")
        f = self.r.run(["palette"], message="x\n\nCommit-Tests-Skip: palette: moving the palette\n")
        self.assertEqual([x for x in f if x[1] == "error"], [])
        self.assertTrue(any("skipped by the message" in x[3] for x in f))

    def test_unknown_check_named(self):
        self.r.write("a.toml", "x = 1\n")
        self.r.add("a.toml")
        f = self.r.errors(None, message="x\n\nCommit-Tests-Skip: pallete: typo\n")
        self.assertTrue(any("no such check" in x[3] for x in f))

    def test_a_check_that_raises_blocks(self):
        self.r.write("a.toml", "x = 1\n")
        self.r.add("a.toml")

        class Boom:
            NAME, TARGETS = "toml", ("**/*.toml",)

            @staticmethod
            def run(ctx, files):
                raise ValueError("boom")
        load = core.load_check
        core.load_check = lambda n: Boom
        try:
            f = self.r.errors(["toml"])
        finally:
            core.load_check = load
        self.assertTrue(any("raised" in x[3] and "boom" in x[3] for x in f), f)


class StagedAndUnstaged(Base):
    def test_refused(self):
        self.r.write("a.txt", "one\n")
        self.r.add("a.txt")
        self.r.write("a.txt", "two\n")
        self.assertEqual(runner.staged_and_unstaged(self.r.dir), ["a.txt"])
        self.r.add("a.txt")
        self.assertEqual(runner.staged_and_unstaged(self.r.dir), [])


if __name__ == "__main__":
    unittest.main()
