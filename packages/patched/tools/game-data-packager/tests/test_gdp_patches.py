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
  game data);
- 0007 transport-tycoon-deluxe-music-fm: the loader's 22 fm files and the plugin's orig_win_fm.obm
  (OpenTTD's slots, the files' MD5s, no [timingtrim]), then a build of the package from a fixture
  (ttd.yaml replaced by one whose fm checksums are the fixture's): the files and the set file in
  baseset/fm, the set file's bytes the ones generated_files() gives (what gdp-manifest.py records).

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
OLD = b'IWAD' + b'old release' * 50	# walnuttest-old.wad: a known release gdp refuses
HUGE = 1 << 50				# huge.wad: a download bigger than any disk here


def sums(data):
    return {h: hashlib.new(h, data).hexdigest() for h in ('md5', 'sha1', 'sha256')}


def game_yaml():
    w, d, o = sums(WAD), sums(DL), sums(OLD)
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
  walnut-test-old-wad:
    longname: Walnut test (old release)
    install:
    - walnuttest-old.wad
  walnut-test-dl-wad:
    longname: Walnut test (download)
    install:
    - walnuttest-dl.wad
  walnut-test-huge-wad:
    longname: Walnut test (huge download)
    install:
    - huge.wad

files:
  walnuttest-old.wad:
    alternatives:
    - walnuttest-old.wad?2.0
  walnuttest-old.wad?1.0:
    look_for: [walnuttest-old.wad]
    unsuitable: Walnut test 2.0 is required
  walnuttest-dl.wad:
    download: http://127.0.0.1:9/walnuttest-dl.wad
  huge.wad:
    download: http://127.0.0.1:9/huge.wad

groups:
  main: |
    {len(WAD)} {w['md5']} walnuttest.wad
    {len(DL)} {d['md5']} walnuttest-dl.wad
    {len(OLD)} {o['md5']} walnuttest-old.wad?1.0
    {len(OLD) + 1} {'1' * 32} walnuttest-old.wad?2.0
    {HUGE} {'0' * 32} huge.wad

sha1sums: |
  {w['sha1']} walnuttest.wad
  {d['sha1']} walnuttest-dl.wad
  {o['sha1']} walnuttest-old.wad?1.0

sha256sums: |
  {w['sha256']} walnuttest.wad
  {d['sha256']} walnuttest-dl.wad
  {o['sha256']} walnuttest-old.wad?1.0
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
            'not-found', 'unknown-version', 'unsuitable', 'needs-download', 'no-space', 'failed'))
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
        cls.old = os.path.join(cls.tmp, 'old')
        os.mkdir(cls.old)
        with open(os.path.join(cls.old, 'WALNUTTEST-OLD.WAD'), 'wb') as f:
            f.write(OLD)
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

    def test_unsuitable(self):
        rc, out, err, lines, d = self.gdp('walnuttest', '-p', 'walnut-test-old-wad', self.old)
        self.assertEqual(rc, 1)
        self.assertEqual(self.errors(lines), [
            'error unsuitable walnuttest-old.wad Walnuttest-old 1.0: Walnut test 2.0 is required'])
        self.assertIn('cannot be used', err)

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


FM_PKG = 'transport-tycoon-deluxe-music-fm'
FM = [b'MThd' + bytes([n]) * (100 + n) for n in range(22)]	# the fixture: fm/9_tt00.fm ...


def fm_yaml():
    lines = '\n'.join('    %d %s 9_tt%02d.fm' % (len(d), sums(d)['md5'], n) for n, d in enumerate(FM))
    sha256 = '\n'.join('  %s 9_tt%02d.fm' % (sums(d)['sha256'], n) for n, d in enumerate(FM))
    return f"""---
longname: Transport Tycoon Deluxe
engine: openttd
copyright: "\\u00a9 1995 Microprose"

packages:
  {FM_PKG}:
    install_to: $assets/openttd/baseset/fm
    install:
    - fm musics

groups:
  fm musics: |
{lines}

sha256sums: |
{sha256}
"""


def obm(text):
    """{section: [(key, value)]} of an OpenTTD .obm"""
    out, sec = {}, None
    for line in text.splitlines():
        if line.startswith('['):
            sec = line.strip('[]')
            out[sec] = []
        elif sec and '=' in line and not line.startswith(';'):
            k, _, v = line.partition('=')
            out[sec].append((k.strip(), v.strip()))
    return out


class FMMusic(unittest.TestCase):
    """0007: the FM music package and its OpenTTD music set"""

    @classmethod
    def setUpClass(cls):
        from game_data_packager.game import load_games
        cls.ttd = load_games()['ttd']
        cls.ttd.load_file_data()
        cls.tmp = tempfile.mkdtemp(prefix='gdp-test-fm.')
        cls.datadir = os.path.join(cls.tmp, 'data')
        os.mkdir(cls.datadir)
        for f in os.listdir(PKGDATADIR):
            if f not in ('ttd.json', 'ttd.yaml'):
                os.symlink(os.path.join(PKGDATADIR, f), os.path.join(cls.datadir, f))
        with open(os.path.join(cls.datadir, 'ttd.yaml'), 'w', encoding='utf-8') as f:
            f.write(fm_yaml())
        with open(os.path.join(cls.datadir, 'ttd.json'), 'w', encoding='utf-8') as f:
            f.write('{}')
        cls.files = os.path.join(cls.tmp, 'files', 'fm')
        os.makedirs(cls.files)
        for n, d in enumerate(FM):
            with open(os.path.join(cls.files, '9_tt%02d.fm' % n), 'wb') as f:
                f.write(d)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def test_package_and_set(self):
        p = self.ttd.packages[FM_PKG]
        self.assertEqual(p.install_to, '$assets/openttd/baseset/fm')
        names = sorted(w.name for w in p.install_files)
        self.assertEqual(names, ['9_tt%02d.fm' % n for n in range(22)])
        f = self.ttd.files['9_tt00.fm']
        self.assertEqual((f.size, f.md5), (23117, 'ea625599b9cadd813509208b3ccf5c8d'))
        self.assertEqual(f.sha256, 'afb121c30dbd51d1effdf86e7eab3584adc0291c16fa898d4e103da213c2c286')
        gen = self.ttd.generated_files(p)
        self.assertEqual(list(gen), ['orig_win_fm.obm'])
        s = obm(gen['orig_win_fm.obm'].decode('utf-8'))
        meta = dict(s['metadata'])
        self.assertEqual((meta['name'], meta['shortname'], meta['version']), ('original_windows_fm', 'TTFM', '1'))
        self.assertTrue(meta['description'])
        files = dict(s['files'])
        self.assertEqual(len(files), 31)
        self.assertEqual(files['theme'], '9_tt00.fm')
        self.assertEqual((files['old_0'], files['new_1'], files['ezy_5']), ('9_tt02.fm', '9_tt01.fm', '9_tt07.fm'))
        used = sorted(v for v in files.values() if v)
        self.assertEqual(used, names)
        self.assertEqual(dict(s['md5s']), {n: self.ttd.files[n].md5 for n in names})
        self.assertEqual(dict(s['names'])['9_tt00.fm'], 'Tycoon DELUXE Theme')
        self.assertEqual(len(s['names']), 22)
        self.assertNotIn('timingtrim', s)
        self.assertEqual(self.ttd.generated_files(self.ttd.packages['transport-tycoon-deluxe-music']), {})

    def test_build(self):
        out = os.path.join(self.tmp, 'out')
        os.mkdir(out)
        env = dict(os.environ, LANGUAGE='en', GDP_PKGDATADIR=self.datadir)
        p = subprocess.run([GDP, '-n', '-d', out, '--no-search', '--no-compress', 'ttd', '-p', FM_PKG,
                            os.path.dirname(self.files)], env=env, capture_output=True, text=True, timeout=600)
        self.assertEqual(p.returncode, 0, p.stderr)
        debs = glob.glob(os.path.join(out, FM_PKG + '_*_all.deb'))
        self.assertEqual(len(debs), 1, os.listdir(out))
        c = subprocess.run(['dpkg-deb', '-c', debs[0]], check=True, capture_output=True, text=True).stdout
        base = './usr/share/games/openttd/baseset/fm/'
        for n in range(22):
            self.assertIn(base + '9_tt%02d.fm' % n, c)
        data = subprocess.run(['sh', '-c', 'dpkg-deb --fsys-tarfile "$1" | tar -xOf - "$2"', 'x', debs[0],
                               base + 'orig_win_fm.obm'], check=True, capture_output=True).stdout
        # what gdp-manifest.py records: generated_files() of the same game data, in a fresh loader
        code = ('import sys; sys.path.insert(0, %r)\n'
                'from game_data_packager.game import load_games\n'
                'g = load_games()["ttd"]; g.load_file_data()\n'
                'sys.stdout.buffer.write(g.generated_files(g.packages[%r])["orig_win_fm.obm"])' % (PKGDATADIR, FM_PKG))
        want = subprocess.run([sys.executable, '-I', '-c', code], env=env, check=True, capture_output=True).stdout
        self.assertEqual(data, want)
        s = obm(data.decode('utf-8'))
        self.assertEqual(dict(s['md5s'])['9_tt05.fm'], sums(FM[5])['md5'])


if __name__ == '__main__':
    unittest.main()
