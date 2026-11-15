#!/usr/bin/env python3
"""licensed-names.py: the PC's name check for the licensed suite (apt-repo/licensed-publish.sh; docs/PACKAGING.md
"The licensed suite"). walnut-watch lets the licensed suite take a name no ownership entry owns, and the server has no
Debian index to compare it with; so before an upload the PC refuses a new licensed package whose name, or a name in its
Provides, Replaces, Conflicts or Breaks, is

  - a package or a Provides of Debian's index at the pinned snapshot (config.mk DEBIAN_SNAPSHOT: debian-ports
    unstable main, binary-ppc64, which lists arch all too), or
  - a package or a Provides of our published stable or testing suites, or a name packages.toml gives another entry
    (binaries, retired, reserved prefixes, relations).

A name the package's own packages.toml entry owns or lists in its relations was reviewed there and passes; so does a
pair in the allow list (apt-repo/licensed-names-allow.txt: "<licensed package> <name>  # why", the reason required).
On the console the licensed suite is pinned below Debian and our suites (ps3-system's preferences.d/walnut, priority
100), so a collision that gets past this check, or a name Debian adds later, never wins there.

  debian --url URL --keyring K --cache DIR   prints the path of URL's main/binary-ppc64/Packages.xz (URL: a snapshot's
                                             dists/<suite>), kept in DIR and checked on every run against URL's
                                             InRelease, which gpgv checks with K (Debian's ports keyring)
  check --bundle DIR --debian FILE --lists packages.toml --allow FILE [--ours FILE]... [--ours-suite SUITE=KEYRING]...
                                             the stanzas of DIR's licensed Packages index whose files are in the bundle
                                             (what this publish adds); --ours: a Packages index of ours, --ours-suite: a
                                             suite of WALNUT_APT_URL (https://walnut.nanobyte.dev/apt), read and checked
                                             by walnut-repo.py
Exit 0: no collision; 1: collisions (listed) or an error. Never sends the licensed suite's login anywhere.
"""
import argparse, hashlib, importlib.machinery, importlib.util, lzma, os, re, sys

HERE = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "github-ci"))
import walnut_lists as L

_spec = importlib.util.spec_from_loader("walnut_repo", importlib.machinery.SourceFileLoader("walnut_repo", os.path.join(HERE, "walnut-repo.py")))
R = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(R)

INDEX = f"{R.COMP}/binary-{R.ARCH}/Packages.xz"
NAME_FIELDS = ("Provides", "Replaces", "Conflicts", "Breaks")
ALLOW_RE = re.compile(r"([a-z0-9][a-z0-9+.-]*)\s+([a-z0-9][a-z0-9+.-]*)\s+#\s*\S")


class Error(Exception):
	pass


def debian_index(url, keyring, cache):
	"""The checked Packages.xz of the snapshot suite at url, from the cache when it is the signed one."""
	d = os.path.join(cache, hashlib.sha256(url.encode()).hexdigest()[:16])
	os.makedirs(d, exist_ok=True)
	inrel = os.path.join(d, "InRelease")
	if not os.path.isfile(inrel):
		data = R.http_get(f"{url.rstrip('/')}/InRelease")
		if data is None:
			raise Error(f"{url}/InRelease: not found")
		R.gpgv_signed_text(data, keyring)	# a bad one is never cached
		with open(inrel + ".part", "wb") as f:
			f.write(data)
		os.replace(inrel + ".part", inrel)
	with open(inrel, "rb") as f:
		hashes = R.release_hashes(R.gpgv_signed_text(f.read(), keyring))
	if INDEX not in hashes:
		raise Error(f"{url}: the signed Release names no {INDEX}")
	want, size = hashes[INDEX]
	path = os.path.join(d, "Packages.xz")
	if os.path.isfile(path) and R.file_sums(path)[1]["SHA256"] == want:
		return path
	data = R.http_get(f"{url.rstrip('/')}/{INDEX}")
	if data is None or len(data) != size or hashlib.sha256(data).hexdigest() != want:
		raise Error(f"{url}/{INDEX}: missing or not the signed index")
	with open(path + ".part", "wb") as f:
		f.write(data)
	os.replace(path + ".part", path)
	return path


def index_names(text):
	"""The package and Provides names of a Packages text, read line by line (Debian's index is too big, and not ours to
	hold to walnut-repo.py's spellings)."""
	names = set()
	for m in re.finditer(r"^(Package|Provides):[ \t]*(.*)$", text, re.M):
		if m.group(1) == "Package":
			names.add(m.group(2).strip())
		else:
			names.update(L.parse_relation(m.group(2)))
	return names


def read_text(path):
	with open(path, "rb") as f:
		data = f.read()
	if path.endswith(".xz"):
		data = lzma.decompress(data)
	return data.decode("utf-8", "replace")


def load_allow(path):
	"""{(licensed package, name)} of the allow list; a line without a reason is an error."""
	out = set()
	with open(path, encoding="utf-8") as f:
		for n, line in enumerate(f, 1):
			if not line.strip() or line.lstrip().startswith("#"):
				continue
			m = ALLOW_RE.match(line.strip())
			if not m:
				raise Error(f"{path}:{n}: not '<licensed package> <name>  # why'")
			out.add((m.group(1), m.group(2)))
	return out


def new_stanzas(bundle):
	"""The stanzas of the bundle's licensed Packages index whose files the bundle carries (what it adds)."""
	p = os.path.join(bundle, "dists", "licensed", "main", f"binary-{R.ARCH}", "Packages")
	if not os.path.isfile(p):
		return []
	with open(p, encoding="utf-8") as f:
		stanzas = R.parse_stanzas(f.read())
	return [s for s in stanzas if os.path.isfile(os.path.join(bundle, s.get("Filename", "")))]


def collisions(stanzas, debian, ours, lists, allow):
	"""[(package, field, name, why)] for every name of stanzas that collides. debian: a set of names; ours: {name: where}."""
	out = []
	for st in stanzas:
		pkg = st["Package"]
		own = lists.owner(pkg)
		entry = lists.entries.get(own[0]) if own and not own[1] else None
		names = [("Package", pkg)] + [(f, n) for f in NAME_FIELDS for n in L.parse_relation(st.get(f, ""))]
		for field, n in names:
			if entry and (entry.owns(n) or entry.retires(n) or entry.may_relate(n)):
				continue	# reviewed in the package's own entry
			if (pkg, n) in allow:
				continue
			why = []
			if n in debian:
				why.append("a Debian package or Provides at the pinned snapshot")
			if n in ours:
				why.append(f"a package or Provides of ours in {', '.join(sorted(ours[n]))}")
			o = lists.owner(n)
			if o and (entry is None or o[0] != entry.name):
				why.append(f"a name of packages.toml's {o[0]}")
			rel = [e.name for e in lists.entries.values() if e is not entry and e.may_relate(n)]
			if rel:
				why.append(f"in the relations of {', '.join(sorted(rel)[:3])}")
			if why:
				out.append((pkg, field, n, "; ".join(why)))
	return out


def cmd_debian(a):
	print(debian_index(a.url, a.keyring, a.cache))


def cmd_check(a):
	lists = L.Lists(open(a.lists, encoding="utf-8").read())
	allow = load_allow(a.allow)
	stanzas = new_stanzas(a.bundle)
	debian = set()
	for p in a.debian:
		debian |= index_names(read_text(p))
	if len(debian) < 1000:
		raise Error(f"the Debian index ({', '.join(a.debian)}) has {len(debian)} names: not the archive's index")
	ours = {}
	for p in a.ours:
		for n in index_names(read_text(p)):
			ours.setdefault(n, set()).add(os.path.basename(p))
	for spec in a.ours_suite:
		suite, _, ring = spec.partition("=")
		for s in R.Suite(a.apt_url, suite, ring).packages:
			for n in [s["Package"]] + L.parse_relation(s.get("Provides", "")):
				ours.setdefault(n, set()).add(suite)
	bad = collisions(stanzas, debian, ours, lists, allow)
	for pkg, field, n, why in bad:
		print(f"!! {pkg}: {field if field != 'Package' else 'its name'} {n} is {why}", file=sys.stderr)
	if bad:
		print(f"!! {len(bad)} name collisions: rename the package, or (a deliberate reuse) list the pair in {a.allow}", file=sys.stderr)
		return 1
	print(f">> {len(stanzas)} new licensed packages: no name of Debian's ({len(debian)}) or ours ({len(ours)})")
	return 0


def main():
	os.environ.pop("WALNUT_APT_AUTH_FILE", None)	# the licensed suite's login: none of these reads needs it
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = p.add_subparsers(dest="cmd", required=True)
	s = sub.add_parser("debian")
	s.add_argument("--url", required=True)
	s.add_argument("--keyring", required=True)
	s.add_argument("--cache", required=True)
	s = sub.add_parser("check")
	s.add_argument("--bundle", required=True)
	s.add_argument("--debian", action="append", required=True)
	s.add_argument("--lists", required=True)
	s.add_argument("--allow", required=True)
	s.add_argument("--ours", action="append", default=[])
	s.add_argument("--ours-suite", action="append", default=[])
	s.add_argument("--apt-url", default=os.environ.get("WALNUT_APT_URL", R.URL), help="our public suites (--ours-suite)")
	a = p.parse_args()
	try:
		sys.exit({"debian": cmd_debian, "check": cmd_check}[a.cmd](a) or 0)
	except (Error, R.Error, L.ListError, OSError, lzma.LZMAError) as e:
		sys.exit(f"!! {e}")


if __name__ == "__main__":
	main()
