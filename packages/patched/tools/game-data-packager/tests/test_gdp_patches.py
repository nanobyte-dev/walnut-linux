#!/usr/bin/python3
"""Our game-data-packager patches against the installed package (run-in-builder.sh installs the
deb make game-data-packager-deb built into the deb builder image and runs this there):

- 0003 --progress-fd: the writer itself (lines, control characters, one error line, a reader
  that goes away), then whole runs of game-data-packager: the step/file/generated lines of a
  build, each error kind where game-data-packager detects it (not-found, unknown-version,
  needs-download, no-space), and the normal output the same with and without the option;
- 0004 --no-launchers: a Doom-family package without .desktop, pixmaps and icons, and with
  them by default;
- 0006 the early releases: Doom II 1.666 and Heretic 1.0 as the last alternative of doom2.wad and
  heretic.wad, with their sizes and checksums; Hexen 1.0 still unsuitable (gdp's own loader, no
  game data).

game-data-packager checks every file against its checksums, so the builds use a game of our own,
"walnuttest" (plugin doom_common, so DoomTask writes its launchers), whose checksums are those of
a fixture made here; its YAML goes into a copy of the package's data directory (GDP_PKGDATADIR).
The real games (doom2, quake) are only used where no file is needed: an empty directory, no
downloads. No network is used.
"""

import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

PKGDATADIR = next(d for d in ('/usr/share/games/game-data-packager', '/usr/share/game-data-packager')
                  if os.path.isdir(d))
GDP = shutil.which('game-data-packager') or '/usr/games/game-data-packager'
sys.path.insert(0, PKGDATADIR)

from game_data_packager.progress_fd import ERROR_KINDS, ProgressWriter  # noqa: E402

WAD = b'IWAD' + bytes(range(256)) * 16	# the fixture: walnuttest.wad
DL = b'PWAD' + b'walnut' * 100		# walnuttest-dl.wad: known, only by download
HUGE = 1 << 50				# huge.wad: a download bigger than any disk here


def sums(data):
    return {h: hashlib.new(h, data).hexdigest() for h in ('md5', 'sha1', 'sha256')}


def game_yaml():
    w, d = sums(WAD), sums(DL)
    return f"""---
longname: Walnut test game
copyright: "\\u00a9 2026 nobody"
plugin: doom_common
wiki: Doom

packages:
  walnut-test-wad:
    longname: Walnut test
    install:
    - walnuttest.wad
  walnut-test-dl-wad:
    longname: Walnut test (download)
    install:
    - walnuttest-dl.wad
  walnut-test-huge-wad:
    longname: Walnut test (huge download)
    install:
    - huge.wad

files:
  walnuttest-dl.wad:
    download: http://127.0.0.1:9/walnuttest-dl.wad
  huge.wad:
    download: http://127.0.0.1:9/huge.wad

groups:
  main: |
    {len(WAD)} {w['md5']} walnuttest.wad
    {len(DL)} {d['md5']} walnuttest-dl.wad
    {HUGE} {'0' * 32} huge.wad

sha1sums: |
  {w['sha1']} walnuttest.wad
  {d['sha1']} walnuttest-dl.wad

sha256sums: |
  {w['sha256']} walnuttest.wad
  {d['sha256']} walnuttest-dl.wad
"""


class Writer(unittest.TestCase):
    def pipe_writer(self):
        r, w = os.pipe()
        p = ProgressWriter()
        p.open(w)
        return p, r, w

    def read_all(self, r, w):
        os.close(w)
        with os.fdopen(r, encoding='utf-8') as f:
            return f.read().splitlines()

    def test_lines(self):
        p, r, w = self.pipe_writer()
        self.assertTrue(p.enabled)
        p.step('searching /media/cd')
        p.file('/media/cd/My Game/DOOM2.WAD', 14604584)
        p.file('archive.zip//x', None)
        p.generated('/tmp/out/doom2-wad_1.9+89_all.deb')
        p.error('not-found', 'no files\nfound\tfor\rdoom2')
        p.error('no-space', 'a second error is dropped')
        self.assertEqual(self.read_all(r, w), [
            'step searching /media/cd',
            'file /media/cd/My Game/DOOM2.WAD 14604584',
            'file archive.zip//x -1',
            'generated /tmp/out/doom2-wad_1.9+89_all.deb',
            'error not-found no files found for doom2',
        ])

    def test_kinds(self):
        self.assertEqual(ERROR_KINDS, (
            'not-found', 'unknown-version', 'needs-download', 'no-space', 'failed'))
        p, r, w = self.pipe_writer()
        with self.assertRaises(AssertionError):
            p.error('oops', 'not a kind')
        self.read_all(r, w)

    def test_disabled(self):
        p = ProgressWriter()
        self.assertFalse(p.enabled)
        p.step('nothing happens')
        p.error('failed', 'nothing happens')

    def test_bad_fd(self):
        r, w = os.pipe()
        os.close(r)
        os.close(w)
        with self.assertRaises(OSError):
            ProgressWriter().open(w)

    def test_reader_gone(self):
        p, r, w = self.pipe_writer()
        os.close(r)
        p.step('the reader closed its end')	# EPIPE: writing stops, no exception
        self.assertFalse(p.enabled)
        p.step('still nothing raised')
        os.close(w)


class Runs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='gdp-test.')
        cls.datadir = os.path.join(cls.tmp, 'data')
        os.mkdir(cls.datadir)
        for f in os.listdir(PKGDATADIR):
            os.symlink(os.path.join(PKGDATADIR, f), os.path.join(cls.datadir, f))
        with open(os.path.join(cls.datadir, 'walnuttest.yaml'), 'w', encoding='utf-8') as f:
            f.write(game_yaml())
        # command_line.py loads one game only when <game>.json exists; the YAML shadows it
        with open(os.path.join(cls.datadir, 'walnuttest.json'), 'w', encoding='utf-8') as f:
            f.write('{}')
        cls.files = os.path.join(cls.tmp, 'files')
        os.makedirs(os.path.join(cls.files, 'My Game'))
        with open(os.path.join(cls.files, 'My Game', 'WALNUTTEST.WAD'), 'wb') as f:
            f.write(WAD)
        cls.wrong = os.path.join(cls.tmp, 'wrong')
        os.mkdir(cls.wrong)
        with open(os.path.join(cls.wrong, 'walnuttest.wad'), 'wb') as f:
            f.write(WAD[:-1] + b'!' + b'more')	# its name, not its size or checksums
        cls.empty = os.path.join(cls.tmp, 'empty')
        os.mkdir(cls.empty)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def gdp(self, *args, progress=True, datadir=True):
        """Runs game-data-packager -n -d OUT --no-search --no-compress ARGS; returns (exit status,
        stdout, stderr, progress lines, OUT)."""
        out = tempfile.mkdtemp(dir=self.tmp, prefix='out.')
        env = dict(os.environ, LANGUAGE='en')
        if datadir:
            env['GDP_PKGDATADIR'] = self.datadir
        prog = open(os.path.join(self.tmp, 'progress'), 'w+', encoding='utf-8')
        cmd = [GDP]
        if progress:
            cmd += ['--progress-fd', str(prog.fileno())]
        cmd += ['-n', '-d', out, '--no-search', '--no-compress', *args]
        p = subprocess.run(cmd, env=env, capture_output=True, text=True,
                           pass_fds=(prog.fileno(),), timeout=600)
        prog.seek(0)
        lines = prog.read().splitlines()
        prog.close()
        return p.returncode, p.stdout, p.stderr, lines, out

    def contents(self, deb):
        return subprocess.run(['dpkg-deb', '-c', deb], check=True, capture_output=True,
                              text=True).stdout

    def errors(self, lines):
        return [line for line in lines if line.startswith('error ')]

    def test_build_lines_and_launchers(self):
        rc, out, err, lines, d = self.gdp('walnuttest', '-p', 'walnut-test-wad', self.files)
        self.assertEqual(rc, 0, err)
        debs = glob.glob(os.path.join(d, 'walnut-test-wad_*_all.deb'))
        self.assertEqual(len(debs), 1, os.listdir(d))
        wad = os.path.join(self.files, 'My Game', 'WALNUTTEST.WAD')
        self.assertIn(f'step searching {self.files}', lines)
        self.assertIn(f'file {wad} {len(WAD)}', lines)
        self.assertIn('step building walnut-test-wad', lines)
        self.assertEqual(lines[-1], f'generated {debs[0]}')
        self.assertEqual(self.errors(lines), [])
        self.assertIn(f'generated "{debs[0]}"', out)	# the normal output still says it
        c = self.contents(debs[0])
        self.assertIn('./usr/share/games/doom/walnuttest.wad', c)
        self.assertIn('./usr/share/applications/walnut-test-wad.desktop', c)
        self.assertIn('./usr/share/pixmaps/walnut-test-wad.png', c)

    def test_no_launchers(self):
        rc, out, err, lines, d = self.gdp('walnuttest', '-p', 'walnut-test-wad', '--no-launchers',
                                          self.files)
        self.assertEqual(rc, 0, err)
        deb, = glob.glob(os.path.join(d, 'walnut-test-wad_*_all.deb'))
        c = self.contents(deb)
        self.assertIn('./usr/share/games/doom/walnuttest.wad', c)
        for gone in ('/usr/share/applications', '/usr/share/pixmaps', '/usr/share/icons'):
            self.assertNotIn('.' + gone, c)
        lintian = subprocess.run(
            f'dpkg-deb --fsys-tarfile {deb} | tar -xOf - ./usr/share/lintian/overrides/walnut-test-wad',
            shell=True, capture_output=True, text=True).stdout
        self.assertNotIn('desktop-command-not-in-package', lintian)

    def test_normal_output_unchanged(self):
        def norm(s, d):
            s = s.replace(d, 'OUT')
            return re.sub(r'gdptmp\.\w+', 'gdptmp.X', s)
        runs = []
        for progress in (False, True):
            rc, out, err, lines, d = self.gdp('walnuttest', '-p', 'walnut-test-wad', self.files,
                                              progress=progress)
            self.assertEqual(rc, 0, err)
            runs.append((norm(out, d), norm(err, d)))
        self.assertEqual(runs[0], runs[1])
        rc, out, err, lines, d = self.gdp('walnuttest', '-p', 'walnut-test-wad', self.empty,
                                          progress=False)
        rc2, out2, err2, lines2, d2 = self.gdp('walnuttest', '-p', 'walnut-test-wad', self.empty)
        self.assertEqual((rc, out, norm(err, d)), (rc2, out2, norm(err2, d2)))

    def test_not_found(self):
        rc, out, err, lines, d = self.gdp('walnuttest', '-p', 'walnut-test-wad', self.empty)
        self.assertEqual(rc, 1)
        self.assertEqual(self.errors(lines), ['error not-found no files found for walnut-test-wad'])
        self.assertEqual(lines[-1], 'error not-found no files found for walnut-test-wad')

    def test_not_found_doom2(self):
        # missing unpackers of archives that aren't there don't make it "failed"
        rc, out, err, lines, d = self.gdp('doom2', self.empty, datadir=False)
        self.assertEqual(rc, 1)
        self.assertEqual(self.errors(lines), ['error not-found no files found for doom2'])

    def test_unknown_version(self):
        rc, out, err, lines, d = self.gdp('walnuttest', '-p', 'walnut-test-wad', self.wrong)
        self.assertEqual(rc, 1)
        bad = os.path.join(self.wrong, 'walnuttest.wad')
        self.assertEqual(self.errors(lines), [f'error unknown-version unknown version of {bad}'])
        self.assertIn('not one of the expected versions', err)

    def test_needs_download(self):
        rc, out, err, lines, d = self.gdp('walnuttest', '-p', 'walnut-test-dl-wad', '--no-download',
                                          self.empty)
        self.assertEqual(rc, 1)
        self.assertEqual(self.errors(lines),
                         ['error needs-download downloads are off: walnuttest-dl.wad'])

    def test_needs_download_quake(self):
        rc, out, err, lines, d = self.gdp('quake', '-p', 'quake-shareware', '--no-download',
                                          self.empty, datadir=False)
        self.assertEqual(rc, 1)
        e, = self.errors(lines)
        self.assertTrue(e.startswith('error needs-download downloads are off: '), e)

    def test_no_space(self):
        rc, out, err, lines, d = self.gdp('walnuttest', '-p', 'walnut-test-huge-wad', '--download',
                                          self.empty)
        self.assertEqual(rc, 1)
        e, = self.errors(lines)
        self.assertTrue(e.startswith('error no-space no space in '), e)
        self.assertIn('Out of space', err)


class EarlyReleases(unittest.TestCase):
    """0006: what gdp's loader makes of doom2.yaml, heretic.yaml and hexen.yaml"""
    # size, md5, sha256 of the user's CD releases (the values in the patch)
    ACCEPTED = {
        ('doom2', 'doom2.wad', 'doom2.wad?1.666'): (
            14943400, '30e3c2d0350b67bfbf47271970b74b2f',
            '5e701c806a8d3a03700f7fef97b7ad62d0aed50fd240dfac26cdf39ac3d0a8d5'),
        ('heretic', 'heretic.wad', 'heretic.wad?1.0'): (
            11096488, '3117e399cdb4298eaa3941625f4b2923',
            'a01d69dfdb1fbf081a1d3f589a168e626ea28257ecc7109194471d967c643f56'),
    }

    @classmethod
    def setUpClass(cls):
        from game_data_packager.game import load_games
        cls.games = load_games()
        for name in ('doom2', 'heretic', 'hexen'):
            cls.games[name].load_file_data()

    def test_accepted(self):
        for (game, wanted, alt), (size, md5, sha256) in self.ACCEPTED.items():
            with self.subTest(alt=alt):
                files = self.games[game].files
                alts = files[wanted].alternatives
                self.assertEqual(alts[-1], alt)	# newer releases stay preferred
                f = files[alt]
                self.assertIsNone(f.unsuitable)
                self.assertEqual((f.size, f.md5, f.sha256), (size, md5, sha256))

    def test_hexen_1_0_still_unsuitable(self):
        files = self.games['hexen'].files
        self.assertNotIn('hexen.wad?1.0', files['hexen.wad'].alternatives)
        self.assertTrue(files['hexen.wad?1.0'].unsuitable)


if __name__ == '__main__':
    unittest.main()
