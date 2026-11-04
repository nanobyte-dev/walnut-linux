"""walnut_checks.py: the rules walnut-watch applies to a bundle, as functions that read text and return or
raise (docs/WALNUT-SERVER.md; round 3 V1, V2, V4). walnut-watch imports this file from its own directory
(bin/ on the server), with walnut_lists.py beside it (deploy.sh copies github-ci/walnut_lists.py there).

  Refused         the one exception walnut-watch's outcomes use: .code is the public code (E-...), the message
                  stays in the journal and state/done/
  POLICY          which upload key may post which kind, suites and id shape
  policy_error    why a bundle at this key is not allowed, or None
  load_rules      the server's own rules (rules/: ownership.toml, protected-names.txt, pins.json), fail closed
  parse_stanzas   the stanzas of a Packages or Sources index
  check_index     ownership, relations, pool paths for one index (V2)
  signing_ok      a VALIDSIG status line against the suite's pinned fingerprints (V4)
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.realpath(__file__))
for cand in (HERE, os.path.join(HERE, "..", "..", "github-ci")):	# the server: bin/; a checkout: github-ci/
	if os.path.isfile(os.path.join(cand, "walnut_lists.py")):
		sys.path.insert(0, os.path.realpath(cand))
		break
import walnut_lists as L


class Refused(Exception):
	def __init__(self, msg, code="E-REFUSED"):
		super().__init__(msg)
		self.code = code


class Stale(Refused):
	"""Made from a published state that is no longer the published one: the publisher makes it again."""
	def __init__(self, msg):
		super().__init__(msg, "E-STALE")


class PolicyRefused(Refused):
	"""A bundle this key may not post: deleted and logged, with no done/ or status/ entry."""
	def __init__(self, msg):
		super().__init__(msg, "E-POLICY")


# --- V1: what each upload key may post --------------------------------------------------------

NIGHTLY_RE = re.compile(r"nightly-[0-9]{8}-[0-9]{4}-[0-9a-f]{7}\Z")
VERSION_RE = re.compile(r"([0-9]+)\.([0-9]+)\.([0-9]+)\Z")
SITE_ID = re.compile(r"[0-9]{8}-[0-9]{6}-[0-9a-f]{7}\Z")
TESTING_APT_ID = re.compile(r".+-testing-[0-9]+-u[0-9]+\Z")
STABLE_APT_ID = re.compile(r".+-stable-[0-9]+-u[0-9]+\Z")
LICENSED_ID = re.compile(r"[0-9]{8}-[0-9]{6}-licensed-[0-9]+\Z")

# keydir -> kind -> (allowed suites or None, id shape). The ids differ between keys by shape, so one key can
# never use (burn, or read the verdict of) an id another key's publisher will use. "" is the legacy root of
# today's two keys: it takes everything, as before, until the migration ends (LEGACY_ROOT).
POLICY = {
	"site-key": {"site": (None, SITE_ID)},
	"testing-key": {"apt": ({"testing", "testing-debug"}, TESTING_APT_ID), "download": (None, NIGHTLY_RE)},
	"stable-key": {"apt": ({"stable", "stable-debug"}, STABLE_APT_ID), "download": (None, VERSION_RE)},
	"pc-key": {"licensed": ({"licensed"}, LICENSED_ID), "site": (None, SITE_ID)},
}
KINDS = ("site", "download", "apt", "licensed")
LEGACY_ROOT = True
# the only key comments that get the legacy root (deploy.sh refuses every other comment it cannot map)
LEGACY_COMMENTS = ("walnut-deploy@pc", "walnut-deploy@github")


def keydirs():
	return list(POLICY) + ([""] if LEGACY_ROOT else [])


def policy_error(keydir, kind, bid, man):
	"""Why this key may not post this bundle, or None. man: the manifest's paths."""
	if keydir == "":
		return None if LEGACY_ROOT else "the legacy upload root is closed"
	allowed = POLICY.get(keydir, {}).get(kind)
	if allowed is None:
		return f"{keydir} may not post {kind}"
	suites, idrx = allowed
	if not idrx.match(bid):
		return f"{keydir}: id {bid!r} is not the shape of its {kind} ids"
	if suites is not None:
		got = {p.split("/")[1] for p in man if p.startswith("dists/") and p.count("/") >= 2}
		if not got or not got <= suites:
			return f"{keydir}: {kind} suites {sorted(got)} are not {sorted(suites)}"
	return None


# --- V4: the signing key, by the fingerprint that signed --------------------------------------

FPR_RE = re.compile(r"[0-9A-F]{40}\Z")


def signing_fpr(status_lines):
	"""The fingerprint of the key that made the signature: field 1 after VALIDSIG (a subkey's own, never the
	primary key's in the last field), or None unless there is exactly one VALIDSIG."""
	v = [l.split() for l in status_lines if l.startswith("[GNUPG:] VALIDSIG ")]
	if len(v) != 1 or len(v[0]) < 3 or not FPR_RE.match(v[0][2]):
		return None
	return v[0][2]


def signing_ok(status_lines, pins):
	"""True when pins is None (no pinning) or the signing fingerprint is one of them, compared whole."""
	if pins is None:
		return True
	f = signing_fpr(status_lines)
	return f is not None and f in pins


# --- the server's rules (rules/, root-owned, changed at a sitting) ------------------------------

def index_owner(lists):
	"""Lists.owner with the literal names in a dict (the list is a few hundred entries, an index has as many
	stanzas: scanning every entry's regexes per stanza was quadratic). Same answers as Lists.owner."""
	lit, scan = {}, []
	for e in lists.entries.values():
		for n in e.binaries:
			if "@" in n:
				scan.append(e)
				break
		else:
			pass
		for n in e.binaries:
			if "@" not in n:
				lit.setdefault(n, (e.name, False))
		for n in e.retired:
			if "@" not in n:
				lit.setdefault(n, (e.name, True))
		if any("@" in n for n in e.retired) and e not in scan:
			scan.append(e)
	memo = {}

	def owner(name):
		base = name[:-7] if name.endswith("-dbgsym") else name
		if base in memo:
			return memo[base]
		r = lit.get(base)
		if r is None:
			for e in scan:
				if e.owns(base):
					r = (e.name, False)
					break
				if e.retires(base):
					r = (e.name, True)
					break
		if r is None:
			for p, own in lists.reserved.items():
				if base.startswith(p):
					r = (own, True)
					break
		memo[base] = r
		return r
	return owner


class Rules:
	def __init__(self, lists, protected, pins, anchor):
		self.lists, self.protected, self.pins, self.anchor = lists, protected, pins, anchor

	def pins_for(self, kind, suite_or_class):
		return self.pins.get(f"{kind}/{suite_or_class}")


def load_rules(rules_dir):
	"""Rules from rules_dir, or Refused E-RULE-MISSING: nothing is published on a server without them."""
	try:
		with open(f"{rules_dir}/ownership.toml", encoding="utf-8") as f:
			lists = L.Lists(f.read())
		with open(f"{rules_dir}/protected-names.txt", encoding="utf-8") as f:
			protected = L.load_protected(f.read())
		with open(f"{rules_dir}/pins.json", encoding="utf-8") as f:
			doc = json.load(f)
		lists.owner = index_owner(lists)
		pins = {k: [x.upper() for x in v] for k, v in doc["suites"].items()}
		anchor = [x.upper() for x in doc["anchor"]]
		for fprs in list(pins.values()) + [anchor]:
			if not fprs or not all(FPR_RE.match(x) for x in fprs):
				raise ValueError("a pinned fingerprint is not 40 hex characters")
	except (OSError, ValueError, KeyError, L.ListError, TypeError, AttributeError) as e:
		raise Refused(f"the server's rules are missing or unusable ({rules_dir}): {type(e).__name__}: {e}", "E-RULE-MISSING")
	return Rules(lists, protected, pins, anchor)


# --- V2: what the stanzas of a signed index may say ---------------------------------------------

SOURCE_FIELD_RE = re.compile(r"([a-z0-9][a-z0-9+.-]*)(?: \(([A-Za-z0-9.+:~-]+)\))?\Z")
SRC_FILE_RE = re.compile(r"(?:\.dsc|\.tar\.(?:gz|xz|bz2|zst)|\.diff\.gz|\.tar\.(?:gz|xz|bz2|zst)\.asc)\Z")
PKG_RE = re.compile(r"[a-z0-9][a-z0-9+.-]*\Z")
VER_RE = re.compile(r"[A-Za-z0-9.+~:-]+\Z")
ARCH_RE = re.compile(r"[a-z0-9-]+\Z")


def parse_stanzas(text):
	"""[{field: value}] of an index; continuation lines are joined with newlines."""
	out = []
	for para in text.split("\n\n"):
		d, last = {}, None
		for line in para.split("\n"):
			if not line.strip():
				continue
			if line[0] in " \t" and last:
				d[last] += "\n" + line.strip()
			elif ":" in line:
				last, _, v = line.partition(":")
				d[last] = v.strip()
		if d:
			out.append(d)
	return out


def pool_dir(suite, source):
	return f"pool/{suite}/main/{source[:4] if source.startswith('lib') else source[0]}/{source}"


def _entry_for_source(lists, src):
	return [e for e in lists.entries.values() if src == e.source or src in e.moved_from]


def check_index(rules, suite, sources, text):
	"""Checks one Packages (sources=False) or Sources (True) index of suite against the server's ownership
	copy. Raises Refused (E-OWNER, E-RELATION, E-POOLPATH, E-SOURCE) at the first violation."""
	lists = rules.lists
	for st in parse_stanzas(text):
		if sources:
			_check_source_stanza(rules, suite, st)
			continue
		name = st.get("Package", "")
		if not PKG_RE.match(name):
			raise Refused(f"{suite}: package name {name!r}", "E-OWNER")
		own = lists.owner(name)
		if own is None:
			raise Refused(f"{suite}: {name} is in no entry of the server's ownership rule", "E-OWNER")
		if own[1]:
			raise Refused(f"{suite}: {name} is retired or reserved: nobody may publish it", "E-OWNER")
		entry = lists.entries.get(own[0])
		if entry is None:
			raise Refused(f"{suite}: {name}: owner {own[0]} has no entry", "E-OWNER")
		m = SOURCE_FIELD_RE.match(st.get("Source") or name)
		if not m:
			raise Refused(f"{suite}: {name}: Source {st.get('Source')!r}", "E-OWNER")
		src = m.group(1)
		if src != entry.source and src not in entry.moved_from:
			raise Refused(f"{suite}: {name} belongs to source {entry.source}, not {src}", "E-OWNER")
		refusals, _ = L.check_relations(lists, rules.protected, entry, st)
		if refusals:
			raise Refused(f"{suite}: {name}: {refusals[0]}", "E-RELATION")
		ver, arch = st.get("Version", ""), st.get("Architecture", "")
		if not VER_RE.match(ver) or not ARCH_RE.match(arch):
			raise Refused(f"{suite}: {name}: version or architecture", "E-POOLPATH")
		want = f"{pool_dir(suite, src)}/{name}_{ver.split(':', 1)[-1]}_{arch}.deb"
		if st.get("Filename") != want:
			raise Refused(f"{suite}: {name}: Filename {st.get('Filename')!r} is not {want}", "E-POOLPATH")


def _check_source_stanza(rules, suite, st):
	lists = rules.lists
	name = st.get("Package", "")
	entries = _entry_for_source(lists, name)
	if not PKG_RE.match(name) or not entries:
		raise Refused(f"{suite}: source {name!r} is in no entry of the server's ownership rule", "E-SOURCE")
	ents = {e.name for e in entries}
	for b in re.split(r"[,\s]+", st.get("Binary", "").strip()):
		if not b:
			continue
		own = lists.owner(b)
		if own is None or own[1] or own[0] not in ents:
			raise Refused(f"{suite}: source {name}: binary {b} is not its own", "E-SOURCE")
	if st.get("Directory") != pool_dir(suite, name):
		raise Refused(f"{suite}: source {name}: Directory {st.get('Directory')!r}", "E-SOURCE")
	files = [l.split() for l in st.get("Files", "").split("\n") if l.strip()]
	if not files:
		raise Refused(f"{suite}: source {name}: no files", "E-SOURCE")
	for f in files:
		if len(f) != 3 or not f[2].startswith(f"{name}_") or not SRC_FILE_RE.search(f[2]) or "/" in f[2]:
			raise Refused(f"{suite}: source {name}: file name {f[-1]!r}", "E-SOURCE")


# --- the site's trust anchor (M6) ---------------------------------------------------------------

FPR_TEXT_RE = re.compile(r"fingerprint[^0-9A-Fa-f]{0,80}((?:[0-9A-Fa-f]{4}[ :]{0,2}){10})", re.I)


def anchor_errors(rel, data, anchor, home):
	"""Why a site file may not be published: a fingerprint the server doesn't pin printed next to the word
	'fingerprint', or a key file (.asc/.gpg) whose keys are not the pinned ones. anchor: the pinned fingerprints; home: a gpg home directory."""
	if rel.endswith(".asc") or rel.endswith(".gpg"):
		import subprocess, tempfile
		os.makedirs(home, 0o700, exist_ok=True)
		with tempfile.NamedTemporaryFile() as t:
			t.write(data)
			t.flush()
			r = subprocess.run(["gpg", "--homedir", home, "--batch", "--show-keys", "--with-colons", "--with-subkey-fingerprints", t.name],
			                   capture_output=True, text=True)
		got = {l.split(":")[9].upper() for l in r.stdout.splitlines() if l.startswith("fpr:")}
		if r.returncode or not got or not got <= set(anchor):
			return f"{rel}: not a key file of the pinned keys"
		return None
	if rel.rsplit(".", 1)[-1].lower() in ("html", "htm", "txt", "md", "xml", "json", "js"):
		text = data.decode("utf-8", "replace")
		for m in FPR_TEXT_RE.finditer(text):
			fpr = re.sub(r"[^0-9A-Fa-f]", "", m.group(1)).upper()
			if fpr not in anchor:
				return f"{rel}: prints a fingerprint the server does not pin"
	return None
