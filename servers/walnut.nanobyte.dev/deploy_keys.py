#!/usr/bin/python3 -I
"""deploy_keys.py: deploy-keys.pub -> the upload container's authorized_keys (deploy.sh runs it on the PC).

  deploy_keys.py authorized FILE   the authorized_keys text on stdout
  deploy_keys.py dirs FILE         the incoming/ directories the keys need, one per line (relative to incoming/)

A key's comment names what it is for. walnut-deploy-<name> (site, testing, stable, pc) confines it to
/incoming/<name>-key (walnut-watch accepts there only what walnut_checks.POLICY lets that key post). Exactly the
two comments of today's keys, walnut-deploy@pc and walnut-deploy@github, keep the whole /incoming (the legacy
root, closed when the migration ends). Any other comment is an error and nothing is written: a key whose purpose
cannot be read never falls back to the all-kinds root."""
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from walnut_checks import POLICY, LEGACY_COMMENTS

NAME_RE = re.compile(r"walnut-deploy-([a-z]+)\Z")
KEY_RE = re.compile(r"ssh-ed25519 [A-Za-z0-9+/=]+ (\S+)\Z")


def parse(text):
	"""[(root dir relative to incoming/, key line)]; ValueError for a line that is not one of ours"""
	out = []
	for n, line in enumerate(text.splitlines(), 1):
		line = line.strip()
		if not line or line.startswith("#"):
			continue
		m = KEY_RE.match(line)
		if not m:
			raise ValueError(f"line {n}: not an ssh-ed25519 key with a comment")
		comment = m.group(1)
		if comment in LEGACY_COMMENTS:
			out.append(("", line))
			continue
		m = NAME_RE.match(comment)
		if not m or f"{m.group(1)}-key" not in POLICY:
			raise ValueError(f"line {n}: the comment {comment!r} is not walnut-deploy-<{'|'.join(k[:-4] for k in POLICY)}> or a legacy comment")
		out.append((f"{m.group(1)}-key", line))
	if not out:
		raise ValueError("no keys")
	return out


def authorized(text):
	return "".join('restrict,command="rrsync -wo -no-del -no-overwrite -no-lock /incoming%s" %s\n' % (f"/{d}" if d else "", line)
	               for d, line in parse(text))


def dirs(text):
	res = set()
	for d, _ in parse(text):
		if d:
			res |= {f"{d}/{k}" for k in POLICY[d]}
	return sorted(res)


if __name__ == "__main__":
	if len(sys.argv) != 3 or sys.argv[1] not in ("authorized", "dirs"):
		sys.exit(__doc__)
	try:
		with open(sys.argv[2]) as f:
			text = f.read()
		sys.stdout.write(authorized(text) if sys.argv[1] == "authorized" else "".join(d + "\n" for d in dirs(text)))
	except (OSError, ValueError) as e:
		sys.exit(f"!! {sys.argv[2]}: {e}")
