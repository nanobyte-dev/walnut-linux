"""play-collect.py GAME ENTRY: the played sessions of GAME pulled into build/bench (play-test.sh collect): one line each, with how
it ended, play time, launcher, sound dropouts, the form's answers and whether it is imported. Run from the repository's top."""
import glob, os, sys, tomllib
sys.path.insert(0, "bench/harness")
import games    # bench/harness/games.py: a run's game (runs from before the move name the old recipe)
g, entry = sys.argv[1], sys.argv[2]
kv = lambda f: dict(l.split("=", 1) for l in open(f, errors="replace").read().splitlines() if "=" in l)
done = set()
for r in glob.glob("gamedb/games/%s/rating/sessions/*/result.toml" % entry):
    done.add(tomllib.loads(open(r).read()).get("run"))
def game_of(d):
    f = os.path.join(d, "bench.txt")
    if os.path.exists(f):
        return games.run_game(kv(f))
    return games.resolve(os.path.basename(d).split("-session-", 1)[1].rsplit("-", 1)[0], old=True)
runs = sorted(d for d in glob.glob("build/bench/ps3-bench-*-session-*") if game_of(d) == g)
if not runs:
    print("no sessions of %s pulled" % g)
for d in runs:
    b = kv(os.path.join(d, "bench.txt")) if os.path.exists(os.path.join(d, "bench.txt")) else {}
    f = os.path.join(d, "session.toml")
    form = tomllib.loads(open(f).read()) if os.path.exists(f) else {}
    play = float(b.get("play_seconds") or 0)
    print("%s  %s  %4.1f min  %-6s launcher=%s  dropouts=%s/%s  form=%s%s" % (
        os.path.basename(d)[10:26], "imported" if os.path.basename(d) in done else "new     ", play / 60,
        b.get("ended", "?"), b.get("launcher", "-"), b.get("xruns_audible", "-"), b.get("xruns_logged", b.get("underruns", "-")),
        ("sound=%s music=%s controls=%s" % (form.get("sound"), form.get("music"), form.get("controls")) +
         (" glitches=%s played_with=%s menus=%s saves=%s rules=2" % (form.get("glitches"), form.get("played_with"),
          form.get("menus_modes"), form.get("saves_load")) if form.get("form_version", 1) >= 2 else "")) if form else "none",
        ("  notes: " + " ".join(form.get("notes", "").split())[:100]) if form.get("notes") else ""))
