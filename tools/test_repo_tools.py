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
              "# ours\nlinux-image-ps3\nlinux-image-ps3-debug\nps3-games\nps3-games-licensed\nrct2-data\nclassicube\n"
              "ps3-desktop\n")
        # classicube: a game whose data is licensed (suite = "licensed"), so not in the public image
        write(self.root, "gamedb/games/cc/build.toml", 'packages = ["classicube"]\nsuite = "licensed"\n')
        dev = self.run_tool("tools/image-packages.sh", "dev").stdout.split()
        self.assertEqual(dev, ["linux-image-ps3", "linux-image-ps3-debug", "ps3-games", "ps3-games-licensed", "rct2-data",
                               "classicube", "ps3-desktop"])
        self.assertEqual(self.run_tool("tools/image-packages.sh", "public").stdout.split(),
                         ["linux-image-ps3", "ps3-games-free", "ps3-desktop"])
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


if __name__ == "__main__":
    unittest.main()
