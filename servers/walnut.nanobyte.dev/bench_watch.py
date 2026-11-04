#!/usr/bin/env python3
"""bench_watch.py [--old PATH_TO_OLD_WALNUT_WATCH] [--stanzas 40,200,800] [--runs 5]: wall time, CPU time and peak
memory of one walnut-watch run publishing one signed testing bundle, for the before/after reading of the V1/V2
change (docs/WALNUT-SERVER.md; the deploy runbook). It builds the bundle with test_walnut_watch.py's helpers, with
small stand-in .deb files (64 KiB; the new checks scale with stanzas, not bytes) plus one 200 MiB file in the largest
run, in build/tmp/bench-watch (never /tmp or a nearly full /home: it is deleted afterwards).
The old script is run on the same bundle without the rules directory it does not know. Output: one line per
size and script, medians of the runs."""
import argparse, os, shutil, statistics, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_walnut_watch as T


def build(root, n):
	shutil.rmtree(root, ignore_errors=True)	# the bundle is made on an empty published state
	w = T.Watch("test_download")
	w.root = root
	# one entry per binary: the real packages.toml has about 170 entries and publishes up to a few hundred stanzas
	w.OWNERSHIP = "version = 1\n" + "".join(
		f'[package.p{i}]\ndir = "packages/x/p{i}"\nsource = "p{i}"\ntier = "trusted"\nbinaries = ["p{i}"]\n' for i in range(n))
	debs = {f"p{i}_1_ppc64.deb": os.urandom(64 << 10) for i in range(n)}
	return w, w.apt_bundle(debs)


def once(script, root, files, w, rules):
	shutil.rmtree(root, ignore_errors=True)
	for k in ("site", "download", "apt", "licensed"):
		os.makedirs(f"{root}/incoming/{k}")
	os.makedirs(f"{root}/www")
	os.makedirs(f"{root}/keys")
	shutil.copy(T.GPG["keyring"], f"{root}/keys/archive-keyring.gpg")
	shutil.copy(T.GPG["testing_keyring"], f"{root}/keys/testing-keyring.gpg")
	if rules:
		w.install_rules(ownership=w.OWNERSHIP)
	w.drop("apt", "b1", files)
	env = dict(os.environ, WALNUT_ROOT=root, WALNUT_NO_DOCKER="1")
	t0 = time.monotonic()
	p = subprocess.Popen([script], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	_, status, ru = os.wait4(p.pid, 0)
	wall = time.monotonic() - t0
	ok = os.path.exists(f"{root}/www/apt/dists/testing")
	return wall, ru.ru_utime + ru.ru_stime, ru.ru_maxrss / 1024, ok


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--old")
	ap.add_argument("--stanzas", default="40,170,300")
	ap.add_argument("--runs", type=int, default=5)
	a = ap.parse_args()
	base = os.path.join(HERE, "..", "..", "build", "tmp", "bench-watch")
	os.makedirs(base, exist_ok=True)
	T.setUpModule()
	try:
		for n in map(int, a.stanzas.split(",")):
			root = os.path.join(base, "root")
			w, files = build(root, n)
			scripts = [("new", os.path.join(HERE, "walnut-watch"), True)] + ([("old", a.old, False)] if a.old else [])
			for name, script, rules in scripts:
				res = [once(script, root, files, w, rules) for _ in range(a.runs)]
				if not all(r[3] for r in res):
					print(f"{n:5d} stanzas {name}: a run did not publish")
					continue
				print(f"{n:5d} stanzas {name}: wall {statistics.median(r[0] for r in res):6.2f} s  cpu {statistics.median(r[1] for r in res):6.2f} s  "
				      f"peak {statistics.median(r[2] for r in res):6.1f} MiB  ({a.runs} runs)")
	finally:
		T.tearDownModule()
		shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
	main()
