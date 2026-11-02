#!/usr/bin/env python3
"""walnut-repo.py: walnut.nanobyte.dev's apt suites without a database (docs/release/ALPHA.md M6).

CI runners keep no state, so every publish starts from what is published: the suite's InRelease,
checked against the suite's keyring, and the indices it signs. New packages are added, old
versions pruned, the indices and Release written and signed, and the result is an upload bundle
for servers/walnut.nanobyte.dev/walnut-upload.sh (kind apt): the new pool files and dists/<suite>/. The
signed Release names the InRelease it was built on (X-Walnut-Base); the server's walnut-watch
refuses a bundle whose base is no longer the published one, so two publishes at once can't lose
each other's packages: the loser builds again on the new state.

Debug symbols (-dbgsym) go to <suite>-debug, in the same bundle, so both switch together. Each
suite has its own pool (pool/<suite>/main/...): testing and stable are separate builds of the same
version, and their bytes may differ.

  add    --suite S --keyring K --sign-key FPR --out DIR [--keep N] [--debug-keep N] [--release-only]
         [--strict] FILE.deb|FILE.dsc...   (--release-only: without the debug kernel and the modules
         built for it, which are for testing; --strict: a version already published with other bytes
         is an error, not a note, for the licensed suite whose files never change)
  fetch  --suite S --keyring K --source NAME --to DIR  (the newest version's debs, checked)
  base   --suite S --keyring K  (prints the sha256 of the published InRelease, or none)

Signing uses gpg with the key in GNUPGHOME (its passphrase, if any, in the file
WALNUT_SIGN_PASSPHRASE_FILE names). Pure Python (reads .deb files itself); gpg/gpgv for
signatures. --url defaults to https://walnut.nanobyte.dev/apt; the password-protected licensed suite
is https://walnut.nanobyte.dev/licensed, its login (one line, user:password) in the file
WALNUT_APT_AUTH_FILE names (apt-repo/licensed-auth.sh writes one), never on a command line.
"""
import argparse, base64, gzip, hashlib, io, lzma, os, re, shutil, stat, subprocess, sys, tarfile, tempfile, threading, time, zlib
import urllib.error, urllib.request
from email.utils import formatdate

URL = "https://walnut.nanobyte.dev/apt"
ARCH = "ppc64"
COMP = "main"
PKG_INDEX = f"{COMP}/binary-{ARCH}/Packages"
SRC_INDEX = f"{COMP}/source/Sources"
# debug builds: the debug kernel and the modules built for it (testing only)
DEBUG_BUILD_RE = re.compile(r"^linux-(image|headers)-.*-debug|^ps3rsx-modules-.*-debug")


class Error(Exception):
	pass


# --- Debian versions -------------------------------------------------------------------------

def _order(c):
	if c == "~":
		return -1
	if c.isdigit():
		return 0
	if c.isalpha():
		return ord(c)
	return ord(c) + 256


def _cmp_part(a, b):
	while a or b:
		na = re.match(r"\D*", a).group(0)
		nb = re.match(r"\D*", b).group(0)
		for i in range(max(len(na), len(nb))):
			ca = _order(na[i]) if i < len(na) else 0
			cb = _order(nb[i]) if i < len(nb) else 0
			if ca != cb:
				return ca - cb
		a, b = a[len(na):], b[len(nb):]
		da = re.match(r"\d*", a).group(0)
		db = re.match(r"\d*", b).group(0)
		if int(da or 0) != int(db or 0):
			return int(da or 0) - int(db or 0)
		a, b = a[len(da):], b[len(db):]
	return 0


def vercmp(a, b):
	"""dpkg --compare-versions as a cmp: <0, 0, >0."""
	def split(v):
		e, _, rest = v.rpartition(":") if ":" in v else ("0", "", v)
		u, _, r = rest.rpartition("-") if "-" in rest else (rest, "", "0")
		return int(e or 0), u, r
	ea, ua, ra = split(a)
	eb, ub, rb = split(b)
	return (ea - eb) or _cmp_part(ua, ub) or _cmp_part(ra, rb)


def newest_first(items, key):
	import functools
	return sorted(items, key=functools.cmp_to_key(lambda x, y: -vercmp(key(x), key(y))))


# --- deb822 ----------------------------------------------------------------------------------

FIELD_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9-]*\Z")
# fields whose spelling matters to apt, dpkg or the checks: written exactly so (apt and dpkg read field
# names case-insensitively, the checks here do not)
CANON = {n.lower(): n for n in (
	"Package", "Version", "Source", "Architecture", "Maintainer", "Description", "Depends", "Pre-Depends",
	"Recommends", "Suggests", "Enhances", "Provides", "Replaces", "Conflicts", "Breaks", "Essential",
	"Protected", "Priority", "Section", "Multi-Arch", "Installed-Size", "Homepage", "Built-Using",
	"Files", "Checksums-Sha256", "Checksums-Sha1", "Binary", "Format", "Directory", "Filename", "Size",
	"MD5sum", "SHA1", "SHA256", "SHA512")}
WRITTEN_BY_US = ("Filename", "Size", "MD5sum", "SHA1", "SHA256", "SHA512", "Directory")


def parse_stanzas(text):
	"""[fields dict] of a Packages/Sources/control text. A field name twice in one stanza (apt and dpkg
	compare names case-insensitively, and disagree about which one wins) or an odd name is an error."""
	out = []
	for para in re.split(r"\n\s*\n", text.strip()):
		if not para.strip():
			continue
		fields, key, seen = {}, None, set()
		for line in para.splitlines():
			if line[:1] in (" ", "\t") and key:
				fields[key] += "\n" + line
			elif ":" in line:
				key, _, val = line.partition(":")
				if not FIELD_RE.match(key):
					raise Error(f"bad field name {key!r}")
				if key.lower() in seen:
					raise Error(f"field {key!r} twice in one stanza")
				if key.lower() in CANON and key != CANON[key.lower()]:
					raise Error(f"field {key!r} must be written {CANON[key.lower()]!r}")
				seen.add(key.lower())
				fields[key] = val.strip()
			else:
				raise Error(f"bad deb822 line {line!r}")
		out.append(fields)
	return out


def format_stanza(fields):
	return "".join(f"{k}: {v}\n" if not v.startswith("\n") else f"{k}:{v}\n" for k, v in fields.items())


# --- .deb and .dsc ---------------------------------------------------------------------------

CONTROL_MEMBERS = {"control", "md5sums", "conffiles", "preinst", "postinst", "prerm", "postrm", "triggers",
                   "shlibs", "symbols", "templates", "config"}
MAX_CONTROL = 64 << 20	# a control.tar, packed and unpacked


def decompress(kind, data, limit=MAX_CONTROL):
	"""data unpacked, at most limit bytes (a bigger result is an error, never memory): kind xz, gz or zst."""
	try:
		if kind == "xz":
			d = lzma.LZMADecompressor()
			out = d.decompress(data, max_length=limit + 1)
			if not d.eof and len(out) <= limit:
				raise Error("truncated xz data")
		elif kind == "gz":
			d = zlib.decompressobj(31)
			out = d.decompress(data, limit + 1)
			if not d.eof and len(out) <= limit:
				raise Error("truncated gzip data")
		elif kind == "zst":
			out = _zstd(data, limit)
		else:
			raise Error(f"unknown compression {kind!r}")
	except (lzma.LZMAError, zlib.error, EOFError, ValueError) as e:
		raise Error(f"corrupt {kind} data ({e})") from None
	if len(out) > limit:
		raise Error(f"{kind} data unpacks to more than {limit} bytes")
	return out


def _zstd(data, limit):
	try:
		from compression import zstd	# Python 3.14
	except ImportError:
		zstd = None
	if zstd:
		try:
			d = zstd.ZstdDecompressor()
			out = d.decompress(data, limit + 1)
			if not d.eof and len(out) <= limit:
				raise Error("truncated zstd data")
			return out
		except zstd.ZstdError as e:
			raise Error(f"corrupt zst data ({e})") from None
	exe = shutil.which("zstd")
	if not exe:
		raise Error("a .zst member and neither python's zstd module nor the zstd program")
	p = subprocess.Popen([exe, "-dc", "-q"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
	try:
		threading.Thread(target=lambda: (p.stdin.write(data), p.stdin.close()), daemon=True).start()
	except OSError:
		pass
	out = p.stdout.read(limit + 1)
	if len(out) <= limit:
		if p.wait() != 0:
			raise Error("corrupt zst data")
	else:
		p.kill()
	p.stdout.close()
	return out


def deb_control(path):
	"""The control file of a .deb: an ar archive of exactly debian-binary, one control.tar.{xz,gz,zst}
	and one data.tar.*, in that order, and a control.tar of plain files with distinct, usual names (dpkg
	takes the last of two same-named members, this code used to take the first). Only the control
	member is read, and bounded."""
	control = None
	order = []
	with open(path, "rb") as f:
		f.seek(0, 2)
		total = f.tell()
		f.seek(0)
		if f.read(8) != b"!<arch>\n":
			raise Error(f"{path}: not a .deb")
		while hdr := f.read(60):
			if len(hdr) != 60 or hdr[58:60] != b"`\n":
				raise Error(f"{path}: bad ar header")
			try:
				name = hdr[:16].decode("ascii").strip().rstrip("/")
				size = int(hdr[48:58])
			except ValueError:
				raise Error(f"{path}: bad ar header") from None
			if f.tell() + size > total:
				raise Error(f"{path}: truncated")
			order.append(name)
			if name.startswith("control.tar"):
				if size > MAX_CONTROL:
					raise Error(f"{path}: control.tar is too big")
				control = (name, f.read(size))
			else:
				f.seek(size, 1)
			if size & 1:
				if f.read(1) != b"\n":
					raise Error(f"{path}: bad ar padding")
	if len(order) != 3 or order[0] != "debian-binary" or not order[1].startswith("control.tar.") \
			or not order[2].startswith("data.tar") or control is None:
		raise Error(f"{path}: ar members {order}: want debian-binary, control.tar.*, data.tar.*")
	name, data = control
	ext = name.rsplit(".", 1)[-1]
	if ext not in ("xz", "gz", "zst"):
		raise Error(f"{path}: {name}: not .xz, .gz or .zst")
	data = decompress(ext, data)
	seen = {}
	try:
		with tarfile.open(fileobj=io.BytesIO(data)) as t:
			for m in t.getmembers():
				n = m.name[2:] if m.name.startswith("./") else m.name
				if n in ("", "."):
					continue
				if not m.isfile() or n not in CONTROL_MEMBERS or n in seen:
					raise Error(f"{path}: control.tar member {m.name!r}: not a plain, single, usual control file")
				seen[n] = m
			if "control" not in seen:
				raise Error(f"{path}: no control file")
			return t.extractfile(seen["control"]).read().decode("utf-8")
	except (tarfile.TarError, UnicodeDecodeError, EOFError) as e:
		raise Error(f"{path}: control.tar unreadable ({e})") from None


def file_sums(path):
	h = {"MD5sum": hashlib.md5(), "SHA1": hashlib.sha1(), "SHA256": hashlib.sha256()}
	size = 0
	with open(path, "rb") as f:
		while chunk := f.read(1 << 20):
			size += len(chunk)
			for x in h.values():
				x.update(chunk)
	return size, {k: v.hexdigest() for k, v in h.items()}


SOURCE_RE = re.compile(r"[a-z0-9][a-z0-9+.-]*\Z")	# Debian's syntax for a package name
PLAIN_NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+~-]{0,200}\Z")	# a file next to the .dsc: ASCII, no path


def pool_dir(suite, source):
	if not SOURCE_RE.match(source):
		raise Error(f"{source!r}: not a source package name")
	return f"pool/{suite}/{COMP}/{source[:4] if source.startswith('lib') else source[0]}/{source}"


SOURCE_FIELD_RE = re.compile(r"([a-z0-9][a-z0-9+.-]*)(?: \(([A-Za-z0-9.+:~-]+)\))?\Z")	# name or name (version)


# the dependency fields, in the grammar apt parses (one bad value makes apt refuse the whole Packages file)
_NAME = r"[a-z0-9][a-z0-9+.-]*"
_VER = r"[0-9A-Za-z][0-9A-Za-z.+~:-]*"
_ITEM = rf"{_NAME}(?::[a-z0-9-]+)?(?: \((?:<<|<=|=|>=|>>|<|>) {_VER}\))?"
_PROVIDES_ITEM = rf"{_NAME}(?: \(= {_VER}\))?"
_BUILT_USING_ITEM = rf"{_NAME} \(= {_VER}\)"
RELATION_GRAMMAR = {
	**{f: re.compile(rf"{_ITEM}(?: \| {_ITEM})*\Z") for f in ("Depends", "Pre-Depends", "Recommends", "Suggests", "Enhances")},
	**{f: re.compile(rf"{_ITEM}\Z") for f in ("Breaks", "Conflicts", "Replaces")},
	"Provides": re.compile(rf"{_PROVIDES_ITEM}\Z"), "Built-Using": re.compile(rf"{_BUILT_USING_ITEM}\Z")}


def check_relation_fields(path, fields):
	for f, rx in RELATION_GRAMMAR.items():
		if f not in fields:
			continue
		items = [" ".join(x.split()) for x in fields[f].replace("\n", " ").split(",")]
		for it in items:
			if not rx.match(it):
				raise Error(f"{path}: {f}: {it[:80]!r} is not a dependency apt can parse")


def deb_fields(path):
	"""A .deb's control fields, strictly: one stanza, none of the fields the index writes itself, a
	Package, Version and Source that are what dpkg and apt will read."""
	stanzas = parse_stanzas(deb_control(path))
	if len(stanzas) != 1:
		raise Error(f"{path}: control holds {len(stanzas)} stanzas")
	fields = stanzas[0]
	for k in fields:
		if k.lower() in {w.lower() for w in WRITTEN_BY_US}:
			raise Error(f"{path}: control has a {k} field")
	if not SOURCE_RE.match(fields.get("Package", "")) or not re.match(r"[0-9][A-Za-z0-9.+:~-]*\Z", fields.get("Version", "")):
		raise Error(f"{path}: bad Package or Version")
	if "Source" in fields and not SOURCE_FIELD_RE.match(fields["Source"]):
		raise Error(f"{path}: bad Source {fields['Source']!r}")
	check_relation_fields(path, fields)
	return fields


def deb_stanza(path, suite):
	fields = deb_fields(path)
	source = fields.get("Source", fields["Package"]).split()[0]
	size, sums = file_sums(path)
	fields["Filename"] = f"{pool_dir(suite, source)}/{os.path.basename(path)}"
	fields["Size"] = str(size)
	fields.update(sums)
	return fields


def dsc_stanza(path, suite):
	"""(index stanza, the files it names). Every name in Files / Checksums-Sha256 must be a plain file name
	of a regular file next to the .dsc, with the size and sha256 the .dsc gives: a name is never a path
	(an absolute one would copy any file the process can read into the public pool)."""
	try:
		text = open(path, encoding="utf-8").read(1 << 20)
	except UnicodeDecodeError:
		raise Error(f"{path}: not text") from None
	m = re.search(r"-----BEGIN PGP SIGNED MESSAGE-----\n(?:.+\n)*\n(.*?)\n-----BEGIN PGP SIGNATURE", text, re.S)
	stanzas = parse_stanzas(m.group(1) if m else text)
	if len(stanzas) != 1 or "Source" not in stanzas[0]:
		raise Error(f"{path}: not a .dsc (want one stanza with a Source)")
	fields = stanzas[0]
	src = fields.pop("Source")
	if not SOURCE_RE.match(src) or not re.match(r"[0-9][A-Za-z0-9.+:~-]*\Z", fields.get("Version", "")):
		raise Error(f"{path}: bad Source or Version")
	if any(k in WRITTEN_BY_US for k in fields) or "Package" in fields:
		raise Error(f"{path}: the .dsc has a field the index writes itself (or a Package: that would replace its Source)")
	out = {"Package": src}
	out.update(fields)
	size, sums = file_sums(path)
	name = os.path.basename(path)
	listed = {}	# file name -> (size, sha256)
	for field in ("Checksums-Sha256", "Files"):
		names = {}
		for l in out.get(field, "").splitlines():
			if not l.strip():
				continue
			parts = l.split()
			if len(parts) != 3 or not parts[1].isdigit():
				raise Error(f"{path}: bad {field} line {l!r}")
			names[parts[2]] = (int(parts[1]), parts[0])
		if field == "Checksums-Sha256":
			listed = names
		elif {n: v[0] for n, v in names.items()} != {n: v[0] for n, v in listed.items()}:
			raise Error(f"{path}: Files and Checksums-Sha256 do not name the same files")
	if not listed:
		raise Error(f"{path}: no Checksums-Sha256")
	d = os.path.dirname(path)
	files = []
	for f, (fsize, fsha) in listed.items():
		if not PLAIN_NAME_RE.match(f):
			raise Error(f"{path}: {f!r} is not a plain file name")
		fp = os.path.join(d, f)
		if not os.path.lexists(fp) or not stat.S_ISREG(os.lstat(fp).st_mode):
			raise Error(f"{path}: {f} is not a regular file next to the .dsc")
		got, gsums = file_sums(fp)
		if got != fsize or gsums["SHA256"] != fsha:
			raise Error(f"{path}: {f} is not the size and sha256 the .dsc gives")
		files.append((fp, fsha))
	out["Files"] = out.get("Files", "") + f"\n {sums['MD5sum']} {size} {name}"
	out["Checksums-Sha256"] = out.get("Checksums-Sha256", "") + f"\n {sums['SHA256']} {size} {name}"
	out.pop("Checksums-Sha1", None)
	out["Directory"] = pool_dir(suite, src)
	return out, files + [(path, sums["SHA256"])]


def stanza_source(s):
	return s.get("Source", s["Package"]).split()[0]


def stanza_source_version(s):
	m = re.search(r"\((.+)\)", s.get("Source", ""))
	return m.group(1) if m else s["Version"]


# --- the published suite ---------------------------------------------------------------------

def auth_header():
	"""Basic auth from the file WALNUT_APT_AUTH_FILE names (user:password), or None."""
	f = os.environ.get("WALNUT_APT_AUTH_FILE")
	if not f:
		return None
	with open(f, encoding="utf-8") as fh:
		cred = fh.readline().strip()
	if ":" not in cred:
		raise Error(f"{f}: not user:password")
	return "Basic " + base64.b64encode(cred.encode()).decode()


def http_get(url, tries=6):
	"""The body, or None for a 404. Server errors and dropped connections are retried (the server's
	container restarts when the site is updated)."""
	auth = auth_header()
	for n in range(tries):
		try:
			req = urllib.request.Request(url, headers={"Authorization": auth} if auth else {})
			with urllib.request.urlopen(req, timeout=60) as r:
				return r.read()
		except urllib.error.HTTPError as e:
			if e.code == 404:
				return None
			if e.code < 500 or n == tries - 1:
				raise
		except (urllib.error.URLError, TimeoutError, ConnectionError):
			if n == tries - 1:
				raise
		time.sleep(5 * (n + 1))


def gpgv_signed_text(data, keyring):
	with tempfile.TemporaryDirectory() as d:
		open(f"{d}/InRelease", "wb").write(data)
		r = subprocess.run(["gpgv", "--homedir", d, "--status-fd", "1", "--keyring", os.path.abspath(keyring),
		                    "--output", f"{d}/Release", f"{d}/InRelease"], capture_output=True, text=True)
		status = [l.split()[1] for l in r.stdout.splitlines() if l.startswith("[GNUPG:] ")]
		if r.returncode or status.count("VALIDSIG") != 1:
			raise Error(f"the published InRelease is not signed by {keyring}:\n{r.stderr.strip()}")
		return open(f"{d}/Release", encoding="utf-8").read()


def release_hashes(release_text):
	m = re.search(r"^SHA256:\n((?: .*\n?)+)", release_text, re.M)
	out = {}
	for l in (m.group(1).splitlines() if m else []):
		h, size, name = l.split()
		out[name] = (h, int(size))
	return out


class Suite:
	"""The published state of one suite: base hash and its binary and source stanzas."""

	def __init__(self, url, suite, keyring):
		self.url, self.suite = url.rstrip("/"), suite
		data = http_get(f"{self.url}/dists/{suite}/InRelease")
		self.packages, self.sources = [], []
		if data is None:
			self.base = "none"
			return
		self.base = hashlib.sha256(data).hexdigest()
		hashes = release_hashes(gpgv_signed_text(data, keyring))
		for name, target in ((PKG_INDEX, self.packages), (SRC_INDEX, self.sources)):
			if name not in hashes:
				continue
			# by hash, as apt does: the plain name may already be the next publish's index (the
			# suite switched between the two reads); a by-hash file never changes and is kept
			h = hashes[name][0]
			raw = http_get(f"{self.url}/dists/{suite}/{os.path.dirname(name)}/by-hash/SHA256/{h}")
			if raw is None:
				raw = http_get(f"{self.url}/dists/{suite}/{name}")
			if raw is None or hashlib.sha256(raw).hexdigest() != h:
				raise Error(f"dists/{suite}/{name} does not match the signed Release")
			target += parse_stanzas(raw.decode("utf-8"))


# --- writing a suite -------------------------------------------------------------------------

def prune(stanzas, keep, source, version):
	"""The newest `keep` versions of each source package, binaries and sources alike. By source
	version, not package name: every kernel build has binaries of its own name."""
	if not keep:
		return stanzas
	vers = {}
	for s in stanzas:
		vers.setdefault(source(s), set()).add(version(s))
	kept = {src: set(newest_first(v, lambda x: x)[:keep]) for src, v in vers.items()}
	return [s for s in stanzas if version(s) in kept[source(s)]]


def write_index(dists, rel, stanzas, files):
	text = "".join(format_stanza(s) + "\n" for s in stanzas).encode()
	variants = {rel: text, rel + ".gz": gzip.compress(text, 9, mtime=0), rel + ".xz": lzma.compress(text)}
	for name, data in variants.items():
		p = f"{dists}/{name}"
		os.makedirs(os.path.dirname(p), exist_ok=True)
		open(p, "wb").write(data)
		h = hashlib.sha256(data).hexdigest()
		bh = f"{dists}/{os.path.dirname(name)}/by-hash/SHA256/{h}"
		os.makedirs(os.path.dirname(bh), exist_ok=True)
		open(bh, "wb").write(data)
		files[name] = (h, len(data))


def sort_key(s):
	return (s["Package"], s.get("Architecture", ""), s["Version"])


def write_suite(out, suite, base, packages, sources, sign_key):
	dists = f"{out}/dists/{suite}"
	shutil.rmtree(dists, ignore_errors=True)
	files = {}
	write_index(dists, PKG_INDEX, sorted(packages, key=sort_key), files)
	if sources:
		write_index(dists, SRC_INDEX, sorted(sources, key=sort_key), files)
	release = (f"Origin: Walnut\nLabel: Walnut\nSuite: {suite}\nCodename: {suite}\n"
	           f"Date: {formatdate(time.time(), usegmt=True)}\nArchitectures: {ARCH}\nComponents: {COMP}\n"
	           f"Description: Walnut Linux {suite}\nAcquire-By-Hash: yes\nX-Walnut-Base: {base}\nSHA256:\n"
	           + "".join(f" {h} {size} {name}\n" for name, (h, size) in sorted(files.items())))
	open(f"{dists}/Release", "w").write(release)
	gpg = ["gpg", "--batch", "--yes", "--pinentry-mode", "loopback", "--local-user", sign_key]
	if os.environ.get("WALNUT_SIGN_PASSPHRASE_FILE"):	# a key with a passphrase (the archive key)
		gpg += ["--passphrase-file", os.environ["WALNUT_SIGN_PASSPHRASE_FILE"]]
	subprocess.run(gpg + ["--clearsign", "-o", f"{dists}/InRelease", f"{dists}/Release"], check=True)
	subprocess.run(gpg + ["--armor", "--detach-sign", "-o", f"{dists}/Release.gpg", f"{dists}/Release"], check=True)


def merge(existing, new, key, label, log, strict=False):
	"""existing + new stanzas; a package version already published stays as it is (strict: one with
	other bytes is an error)."""
	have = {key(s): s for s in existing}
	added = []
	for s in new:
		k = key(s)
		if k in have:
			field = "SHA256" if "SHA256" in s else "Checksums-Sha256"	# a deb's, a dsc's
			same = have[k].get(field) == s.get(field)
			if strict and not same:
				raise Error(f"{label} {' '.join(k)} is already published with other bytes: a new file needs a new version")
			log(f"-- {label} {' '.join(k)}: already published{'' if same else ' (other bytes: the published one stays)'}")
			continue
		have[k] = s
		added.append(s)
	return list(have.values()), added


COPIED = {}	# (out, pool path) -> sha256 of what was copied there in this run


def copy_pool(out, rel, src, sha256):
	"""Copies src to out/rel from one open file, hashing what is copied: it must be the bytes the index names.
	src is opened without following a link and must be a regular file; a pool path written twice must get the
	same bytes (two source packages share an orig tarball, never a name with two contents)."""
	if COPIED.get((out, rel), sha256) != sha256:
		raise Error(f"pool/{rel}: two different files for one pool path")
	dst = f"{out}/{rel}"
	os.makedirs(os.path.dirname(dst), exist_ok=True)
	try:
		fd = os.open(src, os.O_RDONLY | os.O_NOFOLLOW)
	except OSError as e:
		raise Error(f"{src}: cannot be opened as a plain file ({e.strerror})") from None
	with os.fdopen(fd, "rb") as fi:
		if not stat.S_ISREG(os.fstat(fi.fileno()).st_mode):
			raise Error(f"{src}: not a regular file")
		h = hashlib.sha256()
		with open(dst, "wb") as fo:
			while chunk := fi.read(1 << 20):
				h.update(chunk)
				fo.write(chunk)
	if h.hexdigest() != sha256:
		os.unlink(dst)
		raise Error(f"{src}: changed after it was read")
	COPIED[(out, rel)] = sha256


# --- commands --------------------------------------------------------------------------------

def add_suite(a, out, suite, debs, dscs, keep):
	"""Adds debs and dscs to the published suite: its new pool files and dists into out."""
	cur = Suite(a.url, suite, a.keyring)
	new_bin = [(deb_stanza(f, suite), f) for f in debs]
	new_src = [dsc_stanza(f, suite) for f in dscs]
	bkey = lambda s: (s["Package"], s.get("Architecture", ""), s["Version"])
	skey = lambda s: (s["Package"], s["Version"])
	packages, added_bin = merge(cur.packages, [s for s, _ in new_bin], bkey, "deb", print, a.strict)
	sources, added_src = merge(cur.sources, [s for s, _ in new_src], skey, "dsc", print, a.strict)
	packages = prune(packages, keep, stanza_source, stanza_source_version)
	sources = prune(sources, keep, lambda s: s["Package"], lambda s: s["Version"])
	# files only for what the written indices hold: a stanza prune dropped gets none in the pool
	keep_bin = {id(s) for s in added_bin} & {id(s) for s in packages}
	for s, f in new_bin:
		if id(s) in keep_bin:
			copy_pool(out, s["Filename"], f, s["SHA256"])
	keep_src = {id(s) for s in added_src} & {id(s) for s in sources}
	for (s, files) in new_src:
		if id(s) in keep_src:
			for f, sha in files:
				copy_pool(out, f"{s['Directory']}/{os.path.basename(f)}", f, sha)
	write_suite(out, suite, cur.base, packages, sources, a.sign_key)
	print(f">> {suite}: +{len(added_bin)} debs, +{len(added_src)} sources on base {cur.base[:12]}; "
	      f"{len(packages)} debs, {len(sources)} sources in the suite")


def cmd_add(a):
	COPIED.clear()
	for f in a.files:	# the name goes into the pool path and the index: plain, whatever the caller passed
		if not PLAIN_NAME_RE.match(os.path.basename(f)):
			raise Error(f"{os.path.basename(f)!r}: not a plain file name")
		if not os.path.lexists(f) or not stat.S_ISREG(os.lstat(f).st_mode):
			raise Error(f"{os.path.basename(f)}: not a regular file (a link, or missing)")
	debs = [f for f in a.files if f.endswith(".deb")]
	dscs = [f for f in a.files if f.endswith(".dsc")]
	bad = [f for f in a.files if f not in debs and f not in dscs]
	if bad:
		raise Error(f"not a .deb or .dsc: {bad}")
	names = {f: deb_fields(f)["Package"] for f in debs}
	if a.release_only:
		left = [f for f in debs if DEBUG_BUILD_RE.search(names[f].removesuffix("-dbgsym"))]
		for f in left:
			print(f"-- {os.path.basename(f)}: a debug build, testing only")
		debs = [f for f in debs if f not in left]
	symbols = [f for f in debs if names[f].endswith("-dbgsym")]
	debs = [f for f in debs if f not in symbols]
	shutil.rmtree(a.out, ignore_errors=True)
	os.makedirs(a.out)
	add_suite(a, a.out, a.suite, debs, dscs, a.keep)
	if symbols:
		add_suite(a, a.out, f"{a.suite}-debug", symbols, [], a.debug_keep)


def cmd_fetch(a):
	cur = Suite(a.url, a.suite, a.keyring)
	bins = [s for s in cur.packages if stanza_source(s) == a.source]
	if not bins:
		raise Error(f"{a.source}: not in {a.suite}")
	ver = newest_first({stanza_source_version(s) for s in bins}, lambda v: v)[0]
	os.makedirs(a.to, exist_ok=True)
	for s in bins:
		if stanza_source_version(s) != ver or s["Package"].endswith("-dbgsym"):
			continue
		data = http_get(f"{cur.url}/{s['Filename']}")
		if data is None or hashlib.sha256(data).hexdigest() != s["SHA256"]:
			raise Error(f"{s['Filename']}: missing or not the published bytes")
		open(f"{a.to}/{os.path.basename(s['Filename'])}", "wb").write(data)
		print(f"-- {os.path.basename(s['Filename'])}")


def cmd_base(a):
	print(Suite(a.url, a.suite, a.keyring).base)


def main():
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("--url", default=os.environ.get("WALNUT_APT_URL", URL))
	sub = p.add_subparsers(dest="cmd", required=True)
	s = sub.add_parser("add")
	s.add_argument("--suite", required=True)
	s.add_argument("--keyring", required=True)
	s.add_argument("--sign-key", required=True)
	s.add_argument("--out", required=True)
	s.add_argument("--keep", type=int, default=0, help="source versions kept per package (0: all)")
	s.add_argument("--debug-keep", type=int, default=0, help="the same for <suite>-debug")
	s.add_argument("--release-only", action="store_true", help="leave out the debug kernel and its modules")
	s.add_argument("--strict", action="store_true", help="a published version with other bytes is an error")
	s.add_argument("files", nargs="+")
	s = sub.add_parser("fetch")
	s.add_argument("--suite", required=True)
	s.add_argument("--keyring", required=True)
	s.add_argument("--source", required=True)
	s.add_argument("--to", required=True)
	s = sub.add_parser("base")
	s.add_argument("--suite", required=True)
	s.add_argument("--keyring", required=True)
	a = p.parse_args()
	try:
		{"add": cmd_add, "fetch": cmd_fetch, "base": cmd_base}[a.cmd](a)
	except Error as e:
		sys.exit(f"!! {e}")


if __name__ == "__main__":
	main()
