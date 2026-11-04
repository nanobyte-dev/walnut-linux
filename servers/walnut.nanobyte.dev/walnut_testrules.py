#!/usr/bin/python3 -I
"""walnut_testrules.py ROOT [BUNDLE_DIR...]: for tests that use the real walnut-watch as a fake server (apt-repo/test_walnut_repo.py,
github-ci/test_batch_publish.py): writes ROOT/rules/ the way deploy.sh --rules would, from what the test has. Pins are every
fingerprint of ROOT/keys/{testing,archive}-keyring.gpg; the ownership copy lists every package the given bundles' Packages
indices name (each Source owns its binaries, relations included), added to what the previous call wrote. The test then
exercises walnut-watch's other checks (signatures, pins, indices, pool paths, ownership shape) unchanged; what these tests
do not do is decide which package may be published (server tests in test_walnut_watch.py do)."""
import json, os, re, subprocess, sys, tomllib

HERE = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, HERE)
from walnut_checks import parse_stanzas, SOURCE_FIELD_RE
import walnut_lists as L


def fprs(ring):
	if not os.path.isfile(ring):
		return []
	out = subprocess.run(["gpg", "--batch", "--show-keys", "--with-colons", "--with-subkey-fingerprints", ring],
	                     capture_output=True, text=True).stdout
	return [l.split(":")[9].upper() for l in out.splitlines() if l.startswith("fpr:")]


def stanzas(bundle):
	for d, _, fs in os.walk(os.path.join(bundle, "dists")):
		for f in fs:
			if f == "Packages":
				with open(os.path.join(d, f), encoding="utf-8", errors="replace") as fh:
					yield from parse_stanzas(fh.read())


def prepare(root, bundles=()):
	rules = os.path.join(root, "rules")
	os.makedirs(rules, exist_ok=True)
	own = {}
	path = os.path.join(rules, "ownership.toml")
	if os.path.exists(path):
		with open(path, "rb") as f:
			for name, e in tomllib.load(f).get("package", {}).items():
				own[name] = {"binaries": set(e["binaries"]), "relations": set(e.get("relations", []))}
	for b in bundles:
		for st in stanzas(b):
			pkg = st.get("Package", "")
			m = SOURCE_FIELD_RE.match(st.get("Source") or pkg)
			src = m.group(1) if m else pkg
			e = own.setdefault(src, {"binaries": set(), "relations": set()})
			e["binaries"].add(pkg.removesuffix("-dbgsym"))
			for f in L.RELATION_FIELDS:
				e["relations"].update(L.parse_relation(st.get(f, "")))
	text = "version = 1\n"
	for name, e in sorted(own.items()):
		rel = sorted(e["relations"] - e["binaries"])
		text += (f'\n[package.{json.dumps(name)[1:-1] if re.fullmatch(r"[A-Za-z0-9_-]+", name) else json.dumps(name)}]\n'
		         f'dir = "packages/x/{name}"\nsource = {json.dumps(name)}\ntier = "trusted"\n'
		         f'binaries = {json.dumps(sorted(e["binaries"]))}\n' + (f"relations = {json.dumps(rel)}\n" if rel else ""))
	with open(path, "w") as f:
		f.write(text)
	with open(os.path.join(rules, "protected-names.txt"), "w") as f:
		f.write("# none in tests\n")
	ci, arch = fprs(os.path.join(root, "keys", "testing-keyring.gpg")), fprs(os.path.join(root, "keys", "archive-keyring.gpg"))
	suites = {"apt/testing": ci, "apt/testing-debug": ci, "download/nightly": ci,
	          "apt/stable": arch, "apt/stable-debug": arch, "download/release": arch, "licensed/licensed": arch}
	suites = {k: v for k, v in suites.items() if v}
	with open(os.path.join(rules, "pins.json"), "w") as f:
		json.dump({"suites": suites, "anchor": ci + arch}, f)


if __name__ == "__main__":
	prepare(sys.argv[1], sys.argv[2:])
