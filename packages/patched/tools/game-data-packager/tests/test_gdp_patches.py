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
- 0009 roller-coaster-tycoon2: the 2012 build of GOG's setup_rollercoaster_tycoon2_2.0.0.6.exe
  with its sizes and checksums, unpacked and providing what the known build does (loader only).
- 0010 hexen: GOG's setup_hexen_beyond_heretic_1.1_(42801).exe with its sizes and checksums,
  unpacked with innoextract for hexen.wad 1.1 (loader only).
- 0011 hexen: the Macintosh CD's Hexen.WAD as hexen.wad?mac, with its size and checksums
  (loader only).
- 0012 quake-rerelease: the 2021 re-release's id1/pak0.pak and localization in a base directory of
  their own, the CD music (quake-music's Ogg rip) as optional files, and GOG's
  setup_quake_enhanced_1.0_(60828).exe providing them (loader only).
- 0013 quake: --package before the game name survives the quake subparser's shortcut options
  (-m, -s, --music, ...), which had reset it to None (gdp's own parsers, no game data).
- 0014 --no-launchers drops a GOG installer's icon too: not tested here (it needs a real Inno Setup
  installer and python3-pil); the PS3 import of GOG's Quake Enhanced installer is its check (TESTS
  quake-rerelease).
- 0015 the re-release's add-ons: quake-rerelease-armagon, -dissolution, -dopa and
  -dimension-of-the-machine, expansions of quake-rerelease in its base directory, with their paks'
  sizes and checksums, the mission packs' music optional, GOG's Quake Enhanced installer providing
  them (loader only); QuakeTask's tryexec detector only for packages in $assets/quake.
- 0016 secret-of-monkey-island-floppy-es-data and 0017 loom-ega-es-data: the Spanish floppy releases,
  with their files' sizes and checksums, 901-904.lfl shared with the German floppy and Loom's
  unchanged rooms with the other EGA releases (loader only).
- 0018 --no-launchers for the ScummVM games: a build of a fixture ScummVM game ("walnutscumm", plugin
  scummvm_common) has the .desktop link and an icon without it, neither with it.
- 0019 diablo-data: a second 1.09 DIABDAT.MPQ (517501242 bytes) beside GOG's (517501282), with its
  size and checksums, both usable for diablo-data (loader only).

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


class RCT2GOG2012(unittest.TestCase):
    """0009: the 2012 build of GOG's setup_rollercoaster_tycoon2_2.0.0.6.exe beside the known one"""
    NAME = 'setup_rollercoaster_tycoon2_2.0.0.6.exe'
    # size, md5, sha1, sha256 of the user's installer (the values in the patch)
    SUMS = (519393624, '40c7dee00c3451ac0d484df0e384f22e', '7ae5a385b58e79ca7287078329f7e5a191c4c70b',
            '0587ec988c637497b63110e302b91c1d2dbd2d418ee1b5983c872b0c0a410b53')

    def test_accepted(self):
        from game_data_packager.game import load_games
        g = load_games()['roller-coaster-tycoon2']
        g.load_file_data()
        known, f = g.files[self.NAME], g.files[self.NAME + '?gog_2012']
        self.assertEqual((f.size, f.md5, f.sha1, f.sha256), self.SUMS)
        self.assertNotEqual(known.md5, f.md5)
        self.assertEqual(f.unpack['format'], 'innoextract')
        self.assertEqual(f.provides, known.provides)	# base and expansions, as the known build
        self.assertIn('assets expansion packs', f.provides)
        self.assertIn(self.NAME, f.look_for)


class HexenGOG(unittest.TestCase):
    """0010: GOG's Hexen installer, unpacked for hexen.wad 1.1"""
    NAME = 'setup_hexen_beyond_heretic_1.1_(42801).exe'
    # size, md5, sha1, sha256 of the user's installer (the values in the patch)
    SUMS = (148062304, '14fc90ad147cdd834f59da9e4f1fe605', '2a28810ee9333c59ba207e4e55dcdedea0ac8b69',
            '212b8a854ba1dc1cfbeb27c736f62af199057d16dd6625d3f1b54acad4acab42')

    def test_accepted(self):
        from game_data_packager.game import load_games
        g = load_games()['hexen']
        g.load_file_data()
        f = g.files[self.NAME]
        self.assertEqual((f.size, f.md5, f.sha1, f.sha256), self.SUMS)
        self.assertEqual(f.unpack['format'], 'innoextract')
        self.assertEqual(set(f.provides), {'hexen.wad?1.1'})
        self.assertIn('hexen.wad?1.1', g.files['hexen.wad'].alternatives)
        self.assertIsNone(g.files['hexen.wad?1.1'].unsuitable)


class HexenMac(unittest.TestCase):
    """0011: the Macintosh CD's Hexen.WAD, an alternative of hexen.wad"""

    def test_accepted(self):
        from game_data_packager.game import load_games
        g = load_games()['hexen']
        g.load_file_data()
        f = g.files['hexen.wad?mac']
        self.assertEqual((f.size, f.md5, f.sha1, f.sha256),
                         (21078584, 'b68140a796f6fd7f3a5d3226a32b93be',
                          '4343fbe5aef905ef6d077a1517a50c919e5cc906',
                          '06baa5ee71aa87426a6baa7b705cc2ae974da0d4f6ba770c8a87f661f7a55913'))
        self.assertIn('hexen.wad?mac', g.files['hexen.wad'].alternatives)
        self.assertIn('hexen.wad?1.1', g.files['hexen.wad'].alternatives)
        self.assertIsNone(f.unsuitable)



class QuakeRerelease(unittest.TestCase):
    """0012: the 2021 re-release as quake-rerelease, and GOG's Quake Enhanced installer"""
    NAME = 'setup_quake_enhanced_1.0_(60828).exe'
    # size, md5, sha1, sha256 of the user's installer and its id1/pak0.pak (the values in the patch)
    SUMS = (607645056, 'e6bad42772689908d2d84ac2a3360fef', 'f9a61c0a2d0b0ed78cf965da330f8fbb90951adb',
            'da3b4acc6b5c2b4c904232d0e95b1824e445f6f2298b6c5a2ba40d7355b49cff')
    PAK = (180815252, '4cb29cb699d34510da6c465bbe54fa81', '3a4bd39c3ec40f241b12a5681db65e3bddbef934',
           '13e7532da066ab19d951f1ca1d2314af94ab75c7e156bd76006d339e12cb52ed')

    def test_accepted(self):
        from game_data_packager.game import load_games
        g = load_games()['quake']
        g.load_file_data()
        f, pak = g.files[self.NAME], g.files['id1/pak0.pak?rerelease']
        self.assertEqual((f.size, f.md5, f.sha1, f.sha256), self.SUMS)
        self.assertEqual((pak.size, pak.md5, pak.sha1, pak.sha256), self.PAK)
        self.assertEqual(f.unpack['format'], 'innoextract')
        self.assertIn('id1/pak0.pak?rerelease', f.provides)
        self.assertIn('QuakeEX.kpf', f.provides)
        self.assertIn('contents of quake_campaign_soundtrack.zip', f.provides)
        p = g.packages['quake-rerelease']
        self.assertEqual(p.install_to, '$assets/quake-rerelease')
        self.assertEqual({w.name for w in p.install_files}, {'id1/pak0.pak?rerelease', 'localization/loc_english.txt'})
        self.assertIn('id1/music/track02.ogg?johnnylaw', {w.name for w in p.optional_files})
        self.assertEqual(len(p.optional_files), 10)
        # the classic packages keep their files
        self.assertNotIn('id1/pak0.pak?rerelease', {w.name for w in g.packages['quake-registered'].install_files})


class QuakeRereleaseAddons(unittest.TestCase):
    """0015: the re-release's mission packs and episodes as expansions of quake-rerelease"""
    # package: (its pak, size, md5, sha256), the values in the patch (the user's GOG installer)
    PAKS = {
        'quake-rerelease-armagon': ('hipnotic/pak0.pak?rerelease', 79928896, 'a9e2a6e544da7f506f6b65b0661e89a9',
                                    '5963cc2ab77504de8f2d49df313791fc7155fb2c954589c271d4e172708c7363'),
        'quake-rerelease-dissolution': ('rogue/pak0.pak?rerelease', 44490612, 'd189c8aca9b15b1ebc31be4cf1809c14',
                                        '19309481ebc14cd027cd747798e7d80931d6e02a84370fd2b042b20f9ec67da7'),
        'quake-rerelease-dopa': ('dopa/pak0.pak?rerelease', 40600041, 'a3da46a3364bb02c5b6f3e1fca3d068a',
                                 '18a293f6e61ebb47d16acbba2d435141173b87e3985f137ddb846c4657c78610'),
        'quake-rerelease-dimension-of-the-machine': ('mg1/pak0.pak', 496862633, '0aff296d1ec3b9b70e67ce84c60a0d13',
                                                     '41436d7e10965276a28ff7b2649bfb9cadd491c9236c5cbd44a135d69023c0d0'),
    }
    MUSIC = {'quake-rerelease-armagon': 'hipnotic', 'quake-rerelease-dissolution': 'rogue'}

    def setUp(self):
        from game_data_packager.game import load_games
        self.g = load_games()['quake']
        self.g.load_file_data()

    def test_packages(self):
        for name, (pak, size, md5, sha256) in self.PAKS.items():
            p = self.g.packages[name]
            self.assertEqual(p.expansion_for, 'quake-rerelease', name)
            self.assertEqual(p.install_to, '$assets/quake-rerelease', name)
            self.assertEqual({w.name for w in p.install_files}, {pak}, name)
            f = self.g.files[pak]
            self.assertEqual((f.size, f.md5, f.sha256), (size, md5, sha256), name)
            self.assertEqual(f.install_as, pak.split('?')[0], name)
            d = self.MUSIC.get(name)
            want = {'%s/music/track%02d.ogg?rerelease' % (d, i) for i in range(2, 10)} if d else set()
            self.assertEqual({w.name for w in p.optional_files}, want, name)
            for w in want:
                self.assertEqual(self.g.files[w].install_as, w.split('?')[0])
                self.assertIsNotNone(self.g.files[w].sha256)
        # the classic packages keep their files
        self.assertEqual({w.name for w in self.g.packages['quake-armagon'].install_files}, {'hipnotic/pak0.pak'})
        self.assertEqual(self.g.packages['quake-dimension-of-the-machine'].install_to, '$assets/quake')

    def test_installer_provides_them(self):
        f = self.g.files[QuakeRerelease.NAME]
        for pak in ('hipnotic/pak0.pak?rerelease', 'rogue/pak0.pak?rerelease', 'dopa/pak0.pak?rerelease',
                    'MG1 content', 'hipnotic/music/track09.ogg?rerelease', 'rogue/music/track02.ogg?rerelease'):
            self.assertIn(pak, f.provides)

    def test_tryexec_only_for_the_classic_packs(self):
        from game_data_packager.games.quake import QuakeTask
        task = object.__new__(QuakeTask)	# fill_extra_files needs nothing of the task's own
        task._cleanup_dirs = set()		# what its __del__ empties
        for name, want in (('quake-armagon', ['usr/share/games/quake/hipnotic-tryexec.sh']),
                           ('quake-dissolution', ['usr/share/games/quake/rogue-tryexec.sh']),
                           ('quake-rerelease-armagon', []), ('quake-rerelease-dissolution', [])):
            with tempfile.TemporaryDirectory() as d:
                st = type('State', (), {'package': self.g.packages[name], 'destdir': d})()
                task.fill_extra_files(st)
                got = sorted(os.path.relpath(os.path.join(r, n), d) for r, _, ns in os.walk(d) for n in ns)
                self.assertEqual(got, want, name)


class QuakePackageOption(unittest.TestCase):
    """0013: -p before "quake" is kept; the quake shortcuts still select their package"""

    def parse(self, argv):
        import argparse
        from game_data_packager.game import load_games
        base = argparse.ArgumentParser(add_help=False, argument_default=argparse.SUPPRESS)
        base.add_argument('--package', '-p', action='append', dest='packages')
        top = argparse.ArgumentParser(parents=(base,))
        sub = top.add_subparsers(dest='shortname')
        load_games(game='quake')['quake'].add_parser(sub, base)
        return getattr(top.parse_args(argv), 'packages', None)

    def test_package_before_game(self):
        self.assertEqual(self.parse(['-p', 'quake-rerelease', 'quake', 'x']), ['quake-rerelease'])

    def test_package_after_game(self):
        self.assertEqual(self.parse(['quake', '-p', 'quake-rerelease', 'x']), ['quake-rerelease'])

    def test_shortcut(self):
        self.assertEqual(self.parse(['quake', '-m', 'x']), ['quake-registered'])
        self.assertIsNone(self.parse(['quake', 'x']))


class SpanishScumm(unittest.TestCase):
    """0016, 0017: the Spanish floppy releases of The Secret of Monkey Island and Loom"""

    def files(self, game, package):
        from game_data_packager.game import load_games
        g = load_games()[game]
        g.load_file_data()
        p = g.packages[package]
        return g, p, {w.name: w for w in p.install_files}

    def test_monkey(self):
        g, p, files = self.files('monkey', 'secret-of-monkey-island-floppy-es-data')
        self.assertEqual(p.lang, 'es')
        self.assertEqual(len(files), 13)
        f = files['000.lfl?es_floppy']
        self.assertEqual((f.size, f.md5, f.sha256),
                         (8357, '93f436541c10ce1e8a72cb3878ec0347',
                          '4b24ef06d45d31d34db293d807979b38a20a772982015d6d808ce3b558dfd9fc'))
        self.assertIn('901.lfl?de_floppy', files)	# the German floppy's, byte for byte
        self.assertEqual(sorted(n.split('?')[0] for n in files),
                         ['000.lfl', '901.lfl', '902.lfl', '903.lfl', '904.lfl'] +
                         ['disk%02d.lec' % i for i in range(1, 9)])
        for n, f in files.items():
            if n.endswith('?es_floppy'):
                self.assertTrue(f.size and f.md5 and f.sha1 and f.sha256, n)

    def test_loom(self):
        g, p, files = self.files('loom', 'loom-ega-es-data')
        self.assertEqual(p.lang, 'es')
        self.assertEqual(len(files), 74)
        f = files['00.lfl?es']
        self.assertEqual((f.size, f.md5), (5748, '2a208ffbcd0e83e86f4356e6f64aa6e1'))
        self.assertIn('05.lfl', files)		# unchanged from the English EGA release
        self.assertIn('99.lfl?fr', files)	# the French release's
        # the same rooms as the English EGA release
        _, _, en = self.files('loom', 'loom-ega-data')
        self.assertEqual(sorted(n.split('?')[0] for n in files), sorted(n.split('?')[0] for n in en))


class DiabloSecondBuild(unittest.TestCase):
    """0019: a second 1.09 DIABDAT.MPQ is accepted for diablo-data"""

    def test_diabdat(self):
        from game_data_packager.game import load_games
        g = load_games()['diablo']
        g.load_file_data()
        names = {w.name: w for w in g.packages['diablo-data'].install_files}
        self.assertEqual(list(names), ['DIABDAT.MPQ'])
        f = g.files['DIABDAT.MPQ']
        alts = list(f.alternatives)
        self.assertEqual(alts, ['DIABDAT.MPQ?gog', 'DIABDAT.MPQ?1.09'])
        n = g.files['DIABDAT.MPQ?1.09']
        self.assertEqual((n.size, n.md5), (517501242, '68f049866b44688a7af65ba766bef75a'))
        self.assertEqual(n.sha1, 'fe35cba676a0a194ccdf1d4357de49dff252fa40')
        self.assertEqual(n.sha256, '6bc120a603fdb383ff91f374e7db599765d6230d81e5a4c1b33f8254911774e7')
        gog = g.files['DIABDAT.MPQ?gog']
        self.assertEqual((gog.size, gog.md5), (517501282, '011bc6518e6166206231080a4440b373'))
        # GOG's installer still provides its own, under the new name
        self.assertIn('DIABDAT.MPQ?gog', {w.name for w in g.files['setup_diablo_1.09_hellfire_v4_(78466).exe'].provides_files})


class ScummvmNoLaunchers(unittest.TestCase):
    """0018: --no-launchers leaves a ScummVM game's menu entry and icon out"""
    DATA = b'walnut scumm room' * 64

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='gdp-test.')
        cls.datadir = os.path.join(cls.tmp, 'data')
        os.mkdir(cls.datadir)
        for f in os.listdir(PKGDATADIR):
            os.symlink(os.path.join(PKGDATADIR, f), os.path.join(cls.datadir, f))
        s = sums(cls.DATA)
        with open(os.path.join(cls.datadir, 'walnutscumm.yaml'), 'w', encoding='utf-8') as f:
            f.write(f"""---
longname: Walnut ScummVM test game
copyright: "\\u00a9 2026 nobody"
plugin: scummvm_common
wiki: Day_of_the_Tentacle

packages:
  walnut-scumm-data:
    install:
    - walnutscumm.000

groups:
  main: |
    {len(cls.DATA)} {s['md5']} walnutscumm.000

sha1sums: |
  {s['sha1']} walnutscumm.000

sha256sums: |
  {s['sha256']} walnutscumm.000
""")
        with open(os.path.join(cls.datadir, 'walnutscumm.json'), 'w', encoding='utf-8') as f:
            f.write('{}')
        cls.files = os.path.join(cls.tmp, 'files')
        os.mkdir(cls.files)
        with open(os.path.join(cls.files, 'WALNUTSCUMM.000'), 'wb') as f:
            f.write(cls.DATA)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def build(self, *args):
        out = tempfile.mkdtemp(dir=self.tmp, prefix='out.')
        env = dict(os.environ, LANGUAGE='en', GDP_PKGDATADIR=self.datadir)
        p = subprocess.run([GDP, '-n', '-d', out, '--no-search', '--no-compress', '--no-download',
                            *args, 'walnutscumm', self.files],
                           env=env, capture_output=True, text=True, timeout=600)
        self.assertEqual(p.returncode, 0, p.stderr)
        deb, = glob.glob(os.path.join(out, 'walnut-scumm-data_*_all.deb'))
        c = subprocess.run(['dpkg-deb', '-c', deb], check=True, capture_output=True,
                           text=True).stdout
        info = subprocess.run(['dpkg-deb', '-f', deb, 'Recommends'], capture_output=True,
                              text=True).stdout
        return c, info

    def test_with_launchers(self):
        c, rec = self.build()
        self.assertIn('./usr/share/games/walnut-scumm/walnutscumm.000', c)
        self.assertIn('./usr/share/applications/walnut-scumm.desktop', c)
        self.assertRegex(c, r'\./usr/share/(pixmaps|icons/hicolor/scalable/apps)/walnut-scumm\.')
        self.assertIn('game-data-packager-runtime', rec)

    def test_no_launchers(self):
        c, rec = self.build('--no-launchers')
        self.assertIn('./usr/share/games/walnut-scumm/walnutscumm.000', c)
        for gone in ('applications/walnut-scumm', 'pixmaps/walnut-scumm', 'apps/walnut-scumm'):
            self.assertNotIn(gone, c)
        self.assertNotIn('game-data-packager-runtime', rec)

if __name__ == '__main__':
    unittest.main()
