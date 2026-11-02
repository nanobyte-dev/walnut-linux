"""walnut_lists.py: github-ci/packages.toml, the binary package names each package owns, and the
rules that tie a build's debs to its package (read by batch-publish.py, github-ci/build-package.sh's
drift check and gen-workflows.py). See the header of packages.toml for the format.

Names are literal, or have @RELEASE@ (a kernel release, 6.12.111+ps3.1076) and @STAMP@ (a 12-digit
debug stamp), which only an entry owning a reserved prefix may use. <name>-dbgsym is implied for every
binary. No regular expression is ever read from the file."""
import fnmatch, re, tomllib

TOKENS = {"@RELEASE@": r"\d+\.\d+\.\d+\+ps3\.\d+", "@STAMP@": r"\d{12}"}
NAME_RE = re.compile(r"[a-z0-9](?:[a-z0-9.+-]|@[A-Z]+@)*\Z")
TIERS = ("trusted", "external", "licensed")
ENTRY_KEYS = {"dir", "source", "tier", "binaries", "retired", "moved_from", "relations"}
RELATION_FIELDS = ("Replaces", "Breaks", "Conflicts", "Provides")
WEAK = ("Breaks",)	# a protected name may be named here when the entry lists it


class ListError(Exception):
	pass


def compile_name(n):
	"""A regex for name n (literal or with tokens), anchored."""
	out = re.escape(n)
	for t, rx in TOKENS.items():
		out = out.replace(re.escape(t), rx)
	return re.compile(out + r"\Z")


def prefix_of(n):
	return n.split("@", 1)[0]


class Entry:
	def __init__(self, name, d):
		self.name = name
		self.dir, self.source, self.tier = d["dir"], d["source"], d["tier"]
		self.binaries = list(d["binaries"])
		self.retired = list(d.get("retired", []))
		self.moved_from = list(d.get("moved_from", []))
		self.relations = list(d.get("relations", []))
		self.rx = [(n, compile_name(n)) for n in self.binaries]
		self.retired_rx = [(n, compile_name(n)) for n in self.retired]
		self.relations_rx = [compile_name(n) for n in self.relations]

	def owns(self, name):
		return any(r.match(name) for _, r in self.rx)

	def retires(self, name):
		return any(r.match(name) for _, r in self.retired_rx)

	def may_relate(self, name):
		return any(r.match(name) for r in self.relations_rx)


class Lists:
	def __init__(self, text):
		try:
			d = tomllib.loads(text)
		except tomllib.TOMLDecodeError as e:
			raise ListError(f"packages.toml: {e}") from None
		if d.get("version") != 1 or set(d) - {"version", "reserved", "package"}:
			raise ListError("packages.toml: version 1 with [reserved] and [package.*] only")
		self.reserved = dict(d.get("reserved", {}))
		self.entries = {}
		for name, e in (d.get("package") or {}).items():
			if not isinstance(e, dict) or set(e) - ENTRY_KEYS or {"dir", "source", "tier", "binaries"} - set(e):
				raise ListError(f"package.{name}: keys must be {sorted(ENTRY_KEYS)} (dir, source, tier, binaries required)")
			if e["tier"] not in TIERS:
				raise ListError(f"package.{name}: tier {e['tier']!r}")
			for k in ("binaries", "retired", "moved_from", "relations"):
				v = e.get(k, [])
				if not isinstance(v, list) or not all(isinstance(x, str) and NAME_RE.match(x) for x in v):
					raise ListError(f"package.{name}.{k}: names [a-z0-9][a-z0-9.+-]* with @TOKEN@s")
			if not re.fullmatch(r"[a-z0-9][a-z0-9+.-]*", e["source"]):
				raise ListError(f"package.{name}: source {e['source']!r}")
			self.entries[name] = Entry(name, e)
		self._validate()

	def _validate(self):
		sources = {}
		for e in self.entries.values():
			if e.source in sources:
				raise ListError(f"source {e.source} of both {sources[e.source]} and {e.name}")
			sources[e.source] = e.name
			for n in e.binaries + e.retired:
				if "@" in n:
					toks = re.findall(r"@[A-Z]+@", n)
					if any(t not in TOKENS for t in toks) or n.count("@") != 2 * len(toks):
						raise ListError(f"package.{e.name}: unknown token in {n}")
					if not any(n.startswith(p) and self.reserved[p] == e.name for p in self.reserved):
						raise ListError(f"package.{e.name}: pattern {n} outside a reserved prefix it owns")
				if n.endswith("-dbgsym"):
					raise ListError(f"package.{e.name}: {n}: -dbgsym is implied")
				for p, owner in self.reserved.items():
					if n.startswith(p) and owner != e.name:
						raise ListError(f"package.{e.name}: {n} is under {p}, reserved to {owner}")
		for p, owner in self.reserved.items():
			if owner not in self.entries:
				raise ListError(f"reserved {p}: no package {owner}")
		# one owner per name: every literal against every entry's patterns and literals, patterns by prefix
		lits = {}
		pats = []
		for e in self.entries.values():
			for n in e.binaries + e.retired:
				if "@" in n:
					pats.append((prefix_of(n), n, e.name))
				elif n in lits and lits[n] != e.name or n in lits and e.binaries.count(n) + e.retired.count(n) > 1:
					raise ListError(f"{n} is listed twice ({lits[n]}, {e.name})")
				else:
					lits[n] = e.name
		for n, o in lits.items():
			for e in self.entries.values():
				if e.name != o and (e.owns(n) or e.retires(n)):
					raise ListError(f"{n}: listed under {o} and matched by a pattern of {e.name}")
		for i, (p1, n1, o1) in enumerate(pats):
			for p2, n2, o2 in pats[i + 1:]:
				if o1 != o2 and (p1.startswith(p2) or p2.startswith(p1)):
					raise ListError(f"patterns {n1} ({o1}) and {n2} ({o2}) can overlap")

	def owner(self, name):
		"""(entry name, retired) of the entry that owns name (its -dbgsym too), or None."""
		base = name[:-7] if name.endswith("-dbgsym") else name
		for e in self.entries.values():
			if e.owns(base):
				return e.name, False
			if e.retires(base):
				return e.name, True
		for p, owner in self.reserved.items():
			if base.startswith(p):
				return owner, True	# reserved but not listed: nobody may publish it
		return None

	# -- relations -------------------------------------------------------------------------

	def reserved_owner(self, name):
		for p, owner in self.reserved.items():
			if name.startswith(p):
				return owner
		return None


def parse_relation(value):
	"""The package names in a Replaces/Breaks/Conflicts/Provides value (versions and arch qualifiers dropped)."""
	out = []
	for part in value.replace("\n", " ").split(","):
		for alt in part.split("|"):
			alt = alt.strip()
			if alt:
				out.append(re.split(r"[\s(]", alt, 1)[0].split(":", 1)[0])
	return out


def load_protected(text):
	return [l.split("#")[0].strip() for l in text.splitlines() if l.split("#")[0].strip()]


def is_protected(protected, name):
	return any(fnmatch.fnmatchcase(name, p) for p in protected)


def check_relations(lists, protected, entry, fields):
	"""(refusals, warnings) for one deb's control fields: relations to names the entry doesn't own."""
	R, W = [], []
	for k in fields:
		if k.lower() in ("essential", "protected") or (k.lower() == "important" and fields[k].strip() == "yes"):
			R.append(f"{k} field")
	for f in RELATION_FIELDS:
		for n in parse_relation(fields.get(f, "")):
			base = n
			if entry.owns(base) or entry.retires(base) or entry.owns(base.removesuffix("-dbgsym")):
				continue
			listed = entry.may_relate(n)
			own = lists.owner(n)
			if is_protected(protected, n) and (f not in WEAK or not listed):
				R.append(f"{f}: {n} is a protected name")
			elif own and own[0] != entry.name and not listed:
				R.append(f"{f}: {n} belongs to package {own[0]}")
			elif entry.tier == "external" and not listed:
				R.append(f"{f}: {n} (an external package names no foreign package)")
			elif not listed:
				W.append(f"{f}: {n} is not in the package's relations")
	return R, W
