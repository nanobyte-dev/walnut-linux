#!/usr/bin/env python3
"""Host tests of the repository's own tools (suite repo-tools): tools/pkg-dir.sh (--all, --order, two
packages of one name), tools/licensed.py (names, globs, check, --since, failing closed),
tools/deb-version.sh's :(glob) patterns, tools/doclinks.py (links, anchors, gamedb references,
--rewrite), tools/gamedb/blocks.py (--write, --check) and tools/image-packages.sh. Each test builds a
small tree (a git repository where it needs one) in a temporary directory."""
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


licensed = load("licensed", HERE / "licensed.py")
doclinks = load("doclinks", HERE / "doclinks.py")
blocks = load("blocks", HERE / "gamedb" / "blocks.py")


def write(root, rel, text=""):
    p = Path(root) / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)
    return p


def git(root, *args):
    return subprocess.run(["git", "-C", str(root)] + list(args), check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True,
                          env=dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.org",
                                   GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.org",
                                   GIT_CONFIG_GLOBAL="/dev/null", GIT_CONFIG_SYSTEM="/dev/null")).stdout


class Tree(unittest.TestCase):
    """a fake repository with this checkout's tools/ scripts copied in"""
    # the games' licensed data without a directory of ours (gamedb build.toml [[licensed]])
    DECL = ('[[licensed]]\nname = "quake-shareware"\nbuilder = "game-data-packager:quake"\n'
            '[[licensed]]\nname = "rct2-data"  # from the CD\nbuilder = "script"\n')

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "repo"
        for f in ("pkg-dir.sh", "licensed.py", "deb-version.sh", "image-packages.sh", "gamedb/build-info.py"):
            write(self.root, "tools/" + f)
            shutil.copy(HERE / f, self.root / "tools" / f)
        write(self.root, "gamedb/games/q/build.toml", self.DECL)
        write(self.root, "tools/licensed-files.txt", "# globs\nmisc/cc_audio.zip\n*id1/pak[0-9].pak\n")

    def tearDown(self):
        self.tmp.cleanup()

    def pkg(self, rel, conf="", control=None):
        write(self.root, rel + "/package.conf", conf)
        if control:
            write(self.root, rel + "/debian/control", control)

    def run_tool(self, *args, env=None):
        return subprocess.run(list(args), cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                              env=dict(os.environ, **(env or {})))


class PkgDir(Tree):
    def test_all_order_and_lookup(self):
        self.pkg("packages/patched/kernel/linux")
        self.pkg("packages/walnut/drivers/ps3rsx")
        self.pkg("packages/patched/libs/luajit")
        self.pkg("packages/patched/games/luanti", 'LOCAL_DEPS="luajit"')
        self.pkg("packages/licensed/game-data")
        self.pkg("gamedb/games/foo/packages/foo")
        self.pkg("gamedb/games/foo/licensed/foo-data")
        self.pkg("gamedb/engines/eng/packages/aengine", "LOCAL_DEPS=zlast")
        self.pkg("packages/repackaged/zlast")
        r = self.run_tool("tools/pkg-dir.sh", "--all")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.split(), [
            "packages/licensed/game-data", "packages/repackaged/zlast", "packages/patched/games/luanti",
            "packages/patched/kernel/linux", "packages/patched/libs/luajit", "packages/walnut/drivers/ps3rsx",
            "gamedb/games/foo/packages/foo", "gamedb/games/foo/licensed/foo-data", "gamedb/engines/eng/packages/aengine"])
        order = self.run_tool("tools/pkg-dir.sh", "--order").stdout.split()
        self.assertEqual(order[0], "linux")
        self.assertEqual(order[-1], "ps3rsx")
        self.assertLess(order.index("luajit"), order.index("luanti"))
        self.assertLess(order.index("zlast"), order.index("aengine"))
        self.assertEqual(sorted(order), sorted(os.path.basename(d) for d in r.stdout.split()))
        dirs = self.run_tool("tools/pkg-dir.sh", "--all", "--order").stdout.split()
        self.assertEqual([os.path.basename(d) for d in dirs], order)
        self.assertEqual(self.run_tool("tools/pkg-dir.sh", "foo-data").stdout.strip(), "gamedb/games/foo/licensed/foo-data")
        self.assertEqual(self.run_tool("tools/pkg-dir.sh", "nope").returncode, 1)
        self.assertEqual(self.run_tool("tools/pkg-dir.sh", "Bad_Name").returncode, 2)

    def test_two_of_one_name(self):
        self.pkg("packages/patched/games/foo")
        self.pkg("gamedb/games/foo/packages/foo")
        for args in (["--all"], ["--order"], ["foo"], ["other"]):
            r = self.run_tool("tools/pkg-dir.sh", *args)
            self.assertEqual(r.returncode, 1, args)
            self.assertIn("two packages named foo", r.stderr)

    def test_bad_dependencies(self):
        self.pkg("packages/a/x", "LOCAL_DEPS=y")
        self.pkg("packages/a/y", "LOCAL_DEPS=x")
        r = self.run_tool("tools/pkg-dir.sh", "--order")
        self.assertEqual(r.returncode, 1)
        self.assertIn("cycle", r.stderr)
        self.pkg("packages/a/y", "LOCAL_DEPS=missing")
        self.assertIn("isn't a package", self.run_tool("tools/pkg-dir.sh", "--order").stderr)

    def test_other_root(self):
        self.pkg("packages/a/x")
        r = subprocess.run([str(HERE / "pkg-dir.sh"), "--all"], stdout=subprocess.PIPE, text=True,
                           env=dict(os.environ, PKG_DIR_ROOT=str(self.root)))
        self.assertEqual(r.stdout.split(), ["packages/a/x"])


class Licensed(Tree):
    CONTROL_LIC = "Source: game-data\nSection: local/games\n\nPackage: game-data\nArchitecture: all\n"
    CONTROL_FREE = "Source: engine\nSection: games\n\nPackage: engine\nArchitecture: any\n"

    def check(self, since=None):
        return licensed.check(self.root, since)

    def test_names_and_globs(self):
        self.pkg("packages/licensed/game-data", control=self.CONTROL_LIC)
        self.pkg("gamedb/games/g/licensed/g-data")
        write(self.root, "gamedb/games/g/build.toml", '[[licensed]]\nname = "g-data"\nleak_files = ["data/*.pk3"]\n'
              '[[licensed]]\nname = "g-music"\n')
        write(self.root, "gamedb/games/g/packages/g/dfsg-exclude.txt", "misc/sounds\nmisc/x.wav\n")
        self.assertEqual(licensed.names(self.root),
                         {"quake-shareware", "rct2-data", "game-data", "g-data", "g-music", "ps3-games-licensed"})
        self.assertEqual(licensed.globs(self.root), ["misc/cc_audio.zip", "*id1/pak[0-9].pak", "data/*.pk3",
                                                     "misc/sounds", "misc/sounds/*", "misc/x.wav", "misc/x.wav/*"])
        self.assertEqual(licensed.glob_hit("top-1/misc/sounds/a.ogg", licensed.globs(self.root)), "misc/sounds/*")
        self.assertIsNone(licensed.glob_hit("top/misc/other", licensed.globs(self.root)))
        self.assertTrue(licensed.is_licensed("x", "non-free/games", set()))
        self.assertTrue(licensed.is_licensed("rct2-data", "games", licensed.names(self.root)))
        self.assertFalse(licensed.is_licensed("engine", "games", licensed.names(self.root)))
        r = self.run_tool(sys.executable, "-I", "tools/licensed.py", "is-licensed", "g-music")
        self.assertEqual(r.returncode, 0)
        r = self.run_tool(sys.executable, "-I", "tools/licensed.py", "is-licensed", "engine")
        self.assertEqual(r.returncode, 1)

    def test_fails_closed(self):
        write(self.root, "gamedb/games/g/build.toml", "[[licensed]\n")
        with self.assertRaises(licensed.Error):
            licensed.names(self.root)
        r = self.run_tool(sys.executable, "-I", "tools/licensed.py", "is-licensed", "engine")
        self.assertEqual(r.returncode, 2)
        write(self.root, "gamedb/games/g/build.toml", '[[licensed]]\nname = "Bad Name"\n')
        self.assertEqual(self.run_tool(sys.executable, "-I", "tools/licensed.py", "names").returncode, 2)
        write(self.root, "gamedb/games/g/build.toml", 'licensed = "g-data"\n')
        self.assertEqual(self.run_tool(sys.executable, "-I", "tools/licensed.py", "names").returncode, 2)
        os.unlink(self.root / "gamedb/games/g/build.toml")
        self.assertEqual(self.run_tool(sys.executable, "-I", "tools/licensed.py", "names").returncode, 0)
        (self.root / "tools/licensed-files.txt").unlink()
        self.assertEqual(self.run_tool(sys.executable, "-I", "tools/licensed.py", "globs").returncode, 2)

    def test_check(self):
        git(self.root, "init", "-q")
        self.pkg("packages/licensed/game-data", control=self.CONTROL_LIC)
        self.pkg("packages/patched/games/engine", control=self.CONTROL_FREE)
        write(self.root, "gamedb/games/g/build.toml", '[[licensed]]\nname = "g-music"\n')
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "one")
        # packages/licensed/game-data is licensed by where it is: no declaration
        self.assertEqual(self.check(), [])
        # a gamedb licensed directory its own bundle doesn't declare (another bundle's declaration doesn't count)
        self.pkg("gamedb/games/g/licensed/g-data")
        self.assertIn("doesn't declare", " ".join(self.check()))
        write(self.root, "gamedb/games/q/build.toml", self.DECL + '[[licensed]]\nname = "g-data"\n')
        self.assertIn("doesn't declare", " ".join(self.check()))
        write(self.root, "gamedb/games/q/build.toml", self.DECL)
        write(self.root, "gamedb/games/g/build.toml", '[[licensed]]\nname = "g-data"\n[[licensed]]\nname = "g-music"\n')
        self.assertEqual(self.check(), [])
        # Section and location rules
        write(self.root, "packages/patched/games/engine/debian/control", self.CONTROL_FREE.replace("Section: games", "Section: local/games"))
        self.assertIn("isn't declared licensed", " ".join(self.check()))
        write(self.root, "packages/patched/games/engine/debian/control", self.CONTROL_FREE)
        write(self.root, "packages/licensed/game-data/debian/control", self.CONTROL_LIC.replace("local/games", "games"))
        self.assertIn("isn't local/* or non-free/*", " ".join(self.check()))
        write(self.root, "packages/licensed/game-data/debian/control", self.CONTROL_LIC)
        self.pkg("packages/patched/games/rct2-data")
        self.assertIn("outside packages/licensed/", " ".join(self.check()))
        shutil.rmtree(self.root / "packages/patched/games/rct2-data")
        # all or none: a tracked licensed package missing from the checkout
        shutil.rmtree(self.root / "packages/licensed/game-data")
        self.assertIn("partial checkout", " ".join(self.check()))
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "two")
        # the set only grows: game-data was licensed at HEAD~1 (its directory) and is still (the directory is
        # gone, but g's build.toml declares it now); g-music's declaration dropped is a shrink
        write(self.root, "gamedb/games/g/build.toml", '[[licensed]]\nname = "g-data"\n[[licensed]]\nname = "g-music"\n'
              '[[licensed]]\nname = "game-data"\n')
        self.assertEqual(self.check("HEAD~1"), [])
        write(self.root, "gamedb/games/g/build.toml", '[[licensed]]\nname = "g-data"\n[[licensed]]\nname = "game-data"\n')
        self.assertIn("g-music", " ".join(self.check("HEAD~1")))

    def test_data_setup_names(self):
        """[data_setup] required and optional are licensed names (game-data-packager's packages of the
        player's own copy); downloads and hint add none; check stays consistent and refuses a package
        directory of that name outside a licensed location"""
        git(self.root, "init", "-q")
        write(self.root, "gamedb/games/d/build.toml", 'packages = ["dsda-doom"]\n[data_setup]\ngdp = "final-doom"\n'
              'required = ["plutonia-wad", "tnt-wad"]\noptional = ["doom-extra"]\ndownloads = ["tnt-wad"]\nhint = "x"\n')
        self.assertEqual(licensed.names(self.root), {"quake-shareware", "rct2-data", "ps3-games-licensed",
                                                     "plutonia-wad", "tnt-wad", "doom-extra"})
        r = self.run_tool(sys.executable, "-I", "tools/licensed.py", "is-licensed", "tnt-wad")
        self.assertEqual(r.returncode, 0)
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "one")
        self.assertEqual(self.check(), [])
        self.pkg("packages/patched/games/tnt-wad")
        self.assertIn("outside packages/licensed/", " ".join(self.check()))
        shutil.rmtree(self.root / "packages/patched/games/tnt-wad")
        write(self.root, "gamedb/games/d/build.toml", '[data_setup]\nrequired = ["Bad Name"]\n')
        self.assertEqual(self.run_tool(sys.executable, "-I", "tools/licensed.py", "names").returncode, 2)
        write(self.root, "gamedb/games/d/build.toml", 'data_setup = "doom-wad"\n')
        self.assertEqual(self.run_tool(sys.executable, "-I", "tools/licensed.py", "names").returncode, 2)

    def test_public_checkout(self):
        git(self.root, "init", "-q")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "one")
        self.assertEqual(self.check(), [])
        self.assertIn("ps3-games-licensed", licensed.names(self.root))   # GENERIC: also in the public copy
        self.pkg("packages/licensed/game-data", control=self.CONTROL_LIC)
        self.assertIn("public checkout", " ".join(self.check()))


class DebVersion(Tree):
    def test_glob_patterns_stay_in_one_component(self):
        git(self.root, "init", "-q")
        self.pkg("packages/a/p", 'VERSION_PATHS="lib/*.py docs/**/x.md"')
        for f in ("tools/deb-prepare.sh", "tools/deb-build.sh", "tools/deb-chroot.sh", "build-containers/Dockerfile.deb"):
            write(self.root, f, "x\n")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "base")
        version = lambda: self.run_tool("tools/deb-version.sh", "p").stdout.split("\n")[0]
        self.assertEqual(version(), "1")
        for f, bumps in (("lib/sub/deep.py", False), ("lib/top.py", True), ("docs/a/b/x.md", True), ("docs/x.txt", False)):
            write(self.root, f, "y\n")
            git(self.root, "add", "-A")
            git(self.root, "commit", "-qm", f)
            n = int(git(self.root, "rev-list", "--count", "HEAD"))
            self.assertEqual(version() == str(n), bumps, f)
        write(self.root, "lib/sub/deep.py", "dirty\n")
        self.assertNotIn("~dev", version())
        write(self.root, "lib/top.py", "dirty\n")
        self.assertIn("~dev", version())


class ImagePackages(Tree):
    def test_public_profile(self):
        write(self.root, "system/image/packages-ours.txt",
              "# ours\nlinux-image-ps3\nlinux-image-ps3-debug\nps3-games-integration\nps3-games-licensed\nrct2-data\n"
              "classicube\nps3-desktop\n")
        # classicube: a game whose data is licensed (suite = "licensed"), so not in the public image
        write(self.root, "gamedb/games/cc/build.toml", 'packages = ["classicube"]\nsuite = "licensed"\n')
        dev = self.run_tool("tools/image-packages.sh", "dev").stdout.split()
        self.assertEqual(dev, ["linux-image-ps3", "linux-image-ps3-debug", "ps3-games-integration", "ps3-games-licensed",
                               "rct2-data", "classicube", "ps3-desktop"])
        self.assertEqual(self.run_tool("tools/image-packages.sh", "public").stdout.split(),
                         ["linux-image-ps3", "ps3-games-integration", "ps3-desktop"])
        write(self.root, "gamedb/games/cc/build.toml", 'packages = ["classicube"\n')   # fails closed
        self.assertNotEqual(self.run_tool("tools/image-packages.sh", "public").returncode, 0)


class DocLinks(Tree):
    def test_links_anchors_and_gamedb_refs(self):
        write(self.root, "docs/A.md", "# Top\n## Some `code` here\n[b](B.md) [b2](B.md#second-part) [self](#top)\n"
              "[gone](C.md) [bad](B.md#nope) [web](https://example.org) ![img](pic.png)\n"
              "```\n[not a link](nowhere.md)\n```\n"
              "gamedb:foo and gamedb:bar, gamedb:foo/2026-10-01-crash gamedb:foo/old gamedb:batch/b1 gamedb:batch/b9\n"
              "<!-- gamedb:readme-table --> [x](#some-code-here)\n")
        write(self.root, "docs/B.md", "# B\n## Second part\n")
        write(self.root, "gamedb/games/foo/investigations/2026-10-01-crash.md", "+++\nid = \"2026-10-01-crash\"\n+++\n")
        write(self.root, "gamedb/benchmarks/batches.toml", '[[batch]]\nname = "b1"\n')
        bad = doclinks.scan(self.root, ["docs/A.md"])
        got = sorted((k, t) for _, _, k, t, _ in bad)
        self.assertEqual(got, [("anchor", "B.md#nope"), ("gamedb", "gamedb:bar"), ("gamedb", "gamedb:batch/b9"),
                               ("gamedb", "gamedb:foo/old"), ("link", "C.md"), ("link", "pic.png")])

    def test_rewrite_after_moves(self):
        git(self.root, "init", "-q")
        write(self.root, "docs/games/x.md", "[pkg](../../packages/patched/games/x/package.conf) "
              "[sib](y.md#a) see `packages/patched/games/x/patches/0001-a.patch`\n")
        write(self.root, "docs/games/y.md", "# A\n[back](x.md)\n")
        write(self.root, "packages/patched/games/x/package.conf", "")
        write(self.root, "packages/patched/games/x/patches/0001-a.patch", "Subject: a\n\nsee ../../../../../docs/games/y.md\n")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "before")
        (self.root / "gamedb/games/x/packages").mkdir(parents=True)
        git(self.root, "mv", "packages/patched/games/x", "gamedb/games/x/packages/x")
        git(self.root, "mv", "docs/games/y.md", "gamedb/games/x/technical.md")
        git(self.root, "commit", "-qm", "move")
        mm = git(self.root, "diff", "-M", "--name-status", "HEAD~1", "HEAD")
        doclinks.rewrite(self.root, mm, paths=True)
        x = (self.root / "docs/games/x.md").read_text()
        self.assertIn("[pkg](../../gamedb/games/x/packages/x/package.conf)", x)
        self.assertIn("[sib](../../gamedb/games/x/technical.md#a)", x)
        self.assertIn("`gamedb/games/x/packages/x/patches/0001-a.patch`", x)
        self.assertIn("[back](../../../docs/games/x.md)", (self.root / "gamedb/games/x/technical.md").read_text())
        self.assertEqual([b for b in doclinks.scan(self.root, ["docs/games/x.md", "gamedb/games/x/technical.md"])
                          if b[2] != "anchor"], [])


class Blocks(Tree):
    def test_write_and_check(self):
        write(self.root, "docs/A.md", "a\n<!-- gamedb:licensed-names -->\nstale\n<!-- /gamedb:licensed-names -->\nb\n")
        self.assertEqual(blocks.main(["--check", "--repo", str(self.root)]), 1)
        self.assertEqual(blocks.main(["--write", "--repo", str(self.root)]), 0)
        self.assertEqual((self.root / "docs/A.md").read_text(),
                         "a\n<!-- gamedb:licensed-names -->\n- `ps3-games-licensed`\n- `quake-shareware`\n- `rct2-data`\n"
                         "<!-- /gamedb:licensed-names -->\nb\n")
        self.assertEqual(blocks.main(["--check", "--repo", str(self.root)]), 0)
        write(self.root, "docs/B.md", "<!-- gamedb:no-such -->\n<!-- /gamedb:no-such -->\n<!-- gamedb:licensed-names -->\n")
        self.assertEqual(blocks.main(["--check", "--repo", str(self.root)]), 1)


class GenWorkflows(unittest.TestCase):
    def test_tree_is_current(self):
        r = subprocess.run([sys.executable, str(REPO / "github-ci" / "gen-workflows.py"), "--check"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(r.returncode, 0, r.stdout)


class PublicExport(unittest.TestCase):
    """tools/public-export.sh on a small repository: a commit that only touches left-out paths (a licensed
    package's directory) stays in the public history as an empty commit, so public and private commit
    counts match"""

    def git(self, d, *a):
        return subprocess.run(["git", "-C", d, "-c", "user.name=t", "-c", "user.email=t@example.org", *a],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True).stdout

    def test_only_left_out_paths_commit_is_kept(self):
        if not shutil.which("git-filter-repo") and not os.path.exists(os.path.expanduser("~/.local/bin/git-filter-repo")):
            self.skipTest("no git-filter-repo")
        with tempfile.TemporaryDirectory() as tmp:
            src, out = os.path.join(tmp, "src"), os.path.join(tmp, "out")
            os.makedirs(os.path.join(src, "tools"))
            self.git(src, "init", "-q", "-b", "main")
            for f in ("public-export.sh", "licensed.py", "licensed-files.txt", "public-scan.py", "public-scan-allow.txt"):
                shutil.copy(HERE / f, os.path.join(src, "tools", f))
            os.makedirs(os.path.join(src, "tools", "commit_tests", "checks"))
            shutil.copy(HERE / "commit_tests" / "checks" / "private.py", os.path.join(src, "tools", "commit_tests", "checks", "private.py"))
            Path(src, "tools", "public-strip-blobs.txt").write_text("")
            Path(src, "tools", "public-replace.local.txt").write_text("literal:NOT-IN-THIS-REPO\n")
            self.git(src, "add", "-A")
            self.git(src, "commit", "-qm", "tools")
            os.makedirs(os.path.join(src, "packages", "licensed", "gamex"))
            Path(src, "packages", "licensed", "gamex", "data.txt").write_text("x\n")
            self.git(src, "add", "-A")
            self.git(src, "commit", "-qm", "only a licensed directory")
            Path(src, "a.txt").write_text("a\n")
            self.git(src, "add", "-A")
            self.git(src, "commit", "-qm", "a file")
            env = dict(os.environ, PATH=os.path.expanduser("~/.local/bin") + ":" + os.environ["PATH"])
            r = subprocess.run(["bash", os.path.join(src, "tools", "public-export.sh"), out], env=env, cwd=src,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            self.assertEqual(r.returncode, 0, r.stdout)
            self.assertEqual(self.git(out, "rev-list", "--count", "main").strip(), "3", r.stdout)
            self.assertNotIn("packages/licensed", self.git(out, "log", "--name-only", "--format="))


class PublicScan(unittest.TestCase):
    """tools/public-scan.py on a temporary repository: a key block, a token and a netrc line inside a binary
    blob, a private pattern in another; warn-only by default, --enforce refuses, the allow file lets the
    secret-like fixtures through but never a private pattern"""
    SECRET_BLOB = (b"\0\1\2binary" + b"-" * 5 + b"BEGIN PGP PRIVATE KEY BLOCK" + b"-" * 5 + b"\0"
                   + b"gh" + b"p_" + b"A" * 36 + b"\0machine example.org login me pass" + b"word hunter2x\0")
    PRIVATE = b"some-private-" + b"token-xyz"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = os.path.join(self.tmp.name, "r")
        os.makedirs(self.repo)
        self.git("init", "-q", "-b", "main")
        Path(self.repo, "clean.txt").write_text("nothing here\n")
        self.repl = os.path.join(self.tmp.name, "repl.txt")
        Path(self.repl).write_bytes(b"literal:" + self.PRIVATE + b"\n")
        self.allow = os.path.join(self.tmp.name, "allow.txt")
        Path(self.allow).write_text("# none\n")

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *a):
        subprocess.run(["git", "-C", self.repo, "-c", "user.name=t", "-c", "user.email=t@example.org", *a],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def commit(self, files):
        for name, data in files.items():
            p = Path(self.repo, name)
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        self.git("add", "-A")
        self.git("commit", "-qm", "c")

    def scan(self, *extra):
        r = subprocess.run([sys.executable, "-I", str(HERE / "public-scan.py"), self.repo, "--replace", self.repl,
                            "--allow", self.allow, *extra], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        return r.returncode, r.stdout

    def test_clean(self):
        self.commit({})
        rc, out = self.scan("--enforce")
        self.assertEqual(rc, 0, out)

    def test_secrets_in_a_binary_blob_warn_then_refuse(self):
        self.commit({"data/blob.bin": self.SECRET_BLOB})
        rc, out = self.scan()
        self.assertEqual(rc, 0, out)
        for what in ("a private key block", "a GitHub token", "a netrc or apt auth.conf login"):
            self.assertIn("WARN data/blob.bin: looks like " + what, out)
        self.assertNotIn("hunter2x", out)
        rc, out = self.scan("--enforce")
        self.assertEqual(rc, 1, out)
        self.assertIn("FOUND data/blob.bin", out)

    def test_secret_in_an_old_commit_only(self):
        self.commit({"old.bin": self.SECRET_BLOB})
        self.git("rm", "-q", "old.bin")
        self.git("commit", "-qm", "gone")
        rc, out = self.scan("--enforce")
        self.assertEqual(rc, 1, out)

    def test_allow_file_covers_secrets_not_private_patterns(self):
        self.commit({"fixtures/k.bin": self.SECRET_BLOB, "fixtures/p.txt": b"x " + self.PRIVATE + b"\n"})
        Path(self.allow).write_text("fixtures/k.bin  # fake secrets\n")
        rc, out = self.scan("--enforce")
        self.assertEqual(rc, 1, out)
        self.assertNotIn("k.bin", out)
        self.assertIn("fixtures/p.txt: matches a private pattern", out)
        self.assertNotIn("xyz", out)

    def test_broad_allow_lines_are_refused(self):
        self.commit({"k.bin": self.SECRET_BLOB})
        for line in ("*", "fixtures/*", "**/k.bin", "/k.bin", "../k.bin", "k.[bn]in"):
            Path(self.allow).write_text(line + "\n")
            rc, out = self.scan("--enforce")
            self.assertEqual(rc, 2, line + out)

    def test_allowed_path_does_not_cover_the_same_bytes_elsewhere(self):
        self.commit({"fixtures/k.bin": self.SECRET_BLOB, "src/copy.bin": self.SECRET_BLOB})
        Path(self.allow).write_text("fixtures/k.bin\n")
        rc, out = self.scan("--enforce")
        self.assertEqual(rc, 1, out)

    def test_an_allowed_path_alone_passes(self):
        self.commit({"fixtures/k.bin": self.SECRET_BLOB})
        Path(self.allow).write_text("fixtures/k.bin\n")
        self.assertEqual(self.scan("--enforce")[0], 0)

    def test_file_and_directory_names_are_scanned(self):
        self.commit({"docs/%s-notes.txt" % self.PRIVATE.decode(): b"fine\n"})
        rc, out = self.scan("--enforce")
        self.assertEqual(rc, 1, out)
        self.assertIn("a file or directory name", out)
        self.assertNotIn("xyz", out)
        self.git("rm", "-rq", "docs")
        self.commit({"%s/a.txt" % self.PRIVATE.decode(): b"fine\n"})
        self.assertEqual(self.scan("--enforce")[0], 1)

    def test_commit_message(self):
        Path(self.repo, "a").write_text("a")
        self.git("add", "-A")
        self.git("commit", "-qm", "about " + self.PRIVATE.decode())
        rc, out = self.scan("--enforce")
        self.assertEqual(rc, 1, out)
        self.assertIn("commit ", out)

    def test_unreadable_list_fails_closed(self):
        self.commit({})
        os.remove(self.repl)
        rc, _ = self.scan()
        self.assertEqual(rc, 2)


class DeployAptCheck(unittest.TestCase):
    """tools/deploy.sh: the remote script's `apt-get check` is fatal (run here with a stand-in sudo)"""

    def snippet(self):
        text = (HERE / "deploy.sh").read_text()
        a = text.index("if ! sudo apt-get check")
        b = text.index("echo '>> apt-get check: ok'", a)
        # the script sits inside double quotes in deploy.sh: undo the escapes
        return text[a:b + len("echo '>> apt-get check: ok'")].replace('\\"', '"').replace("\\$", "$")

    def run_with(self, check_rc):
        script = 'sudo() { case "$*" in "apt-get check") return %d;; *) echo "sudo $*";; esac; }\nstopped=1\n%s\n' % (
            check_rc, self.snippet())
        return subprocess.run(["bash", "-ec", script], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def test_clean(self):
        r = self.run_with(0)
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("apt-get check: ok", r.stdout)

    def test_broken_state_fails_the_deploy_and_restarts_the_desktop(self):
        r = self.run_with(100)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("!! apt-get check failed", r.stdout)
        self.assertIn("sudo systemctl start getty@tty1", r.stdout)
        self.assertNotIn("apt-get check: ok", r.stdout)


if __name__ == "__main__":
    unittest.main()
