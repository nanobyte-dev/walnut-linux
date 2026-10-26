"""gamedata.py: the per-game data of the analysis tools (report.py, logscan.py, matrix.py,
interference-scan.py) and of the played test (playdef.py), read from gamedb (gamedb/README.md
"games/<id>/benchmark/" and "games/<id>/rating/"). The tools hold no game of their own: a rule that
belongs to one game, or to the games of one engine, is that game's data.

  games/<id>/benchmark/analysis.toml  [legacy] recipe_names, run_labels; [reload]; [[log_ignore]];
                                      [[log_strip]]; [interference]
  engines/<e>/analysis.toml           [[log_ignore]], [[log_strip]] every game on the engine shares (its
                                      build.toml engine = "<e>"); they come before the game's own
  games/<id>/rating/test.toml         the played test (process, home, play_mark, load_mark, command,
                                      env, sound_at, hud)

Where: bench/games.py's root (gamedb/games in the repository, games/ in the PS3's bench tree, or
BENCH_GAMES); the engines beside it (gamedb/engines; the PS3's tree has none). A run's game: bench.txt
recipe= (runs since the recipes moved into gamedb), else game= read through the old recipe names
(games.run_game), else the run directory's label (old names first, then [legacy] run_labels).
"""
import functools
import re
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import games  # noqa: E402

TEST = "rating/test.toml"


def _toml(path):
    try:
        return tomllib.loads(Path(path).read_text())
    except (OSError, tomllib.TOMLDecodeError):
        return {}


def _bundle(gid):
    """the game's folder (gamedb/games/<id>, or <id>/ of the PS3's tree)"""
    return games.root() / gid


def engine(gid):
    """the engine a game runs on (its build.toml engine), or None"""
    e = _toml(_bundle(gid) / "build.toml").get("engine")
    return e if isinstance(e, str) else None


@functools.lru_cache(maxsize=None)
def own_analysis(gid):
    """the game's own benchmark/analysis.toml, {} without one"""
    d = games.dirs().get(gid)
    return _toml(d / "analysis.toml") if d else {}


@functools.lru_cache(maxsize=None)
def engine_analysis(e):
    """engines/<e>/analysis.toml, {} without one"""
    return _toml(games.root().parent / "engines" / e / "analysis.toml") if e else {}


def analysis(gid):
    """the game's analysis data: its own, with its engine's [[log_ignore]] and [[log_strip]] first"""
    if not gid:
        return {}
    out, eng = dict(own_analysis(gid)), engine_analysis(engine(gid))
    for k in ("log_ignore", "log_strip"):
        rules = list(eng.get(k, [])) + list(out.get(k, []))
        if rules:
            out[k] = rules
    return out


def run_label(label):
    """(id, variant) from a run directory's label (the name without the stamp) by [legacy] run_labels,
    for runs without a bench.txt (the first day's harness), or None"""
    for gid in games.dirs():
        for rule in own_analysis(gid).get("legacy", {}).get("run_labels", []):
            if m := re.fullmatch(rule["re"], label):
                v = m.expand(rule.get("variant", ""))
                for s in rule.get("drop", []):
                    v = v.replace(s, "")
                return gid, v
    return None


def run_id(bench, label=None):
    """a run's game id: from its bench.txt (a dict), else from its label (old recipe names, then
    run_labels); None when nothing has it"""
    if bench.get("recipe") or bench.get("game"):
        return games.run_game(bench)
    if label:
        return games.resolve(label, old=True) or (run_label(label) or (None,))[0]
    return None


def reload(gid):
    """[reload]: where a looped demo reloads (report.py): re + after, or fixed; {} for none"""
    return dict(analysis(gid).get("reload", {}))


@functools.lru_cache(maxsize=None)
def log_rules(gid):
    """([(compiled, why)] of [[log_ignore]], [compiled] of [[log_strip]]) for logscan.py, case-insensitive
    as its own ignore list"""
    a = analysis(gid)
    return ([(re.compile(r["re"], re.I), r["why"]) for r in a.get("log_ignore", [])],
            [re.compile(r["re"]) for r in a.get("log_strip", [])])


def interference(gid):
    """[interference]: the game's own log of what the player did (interference-scan.py), or None"""
    return analysis(gid).get("interference")


def title(gid):
    """the game's title (index.md), else its id"""
    try:
        text = (_bundle(gid) / "index.md").read_text()
        m = re.match(r"\+\+\+\n(.*?)\n\+\+\+", text, re.S)
        return tomllib.loads(m[1]).get("title") or gid
    except (OSError, TypeError, tomllib.TOMLDecodeError):
        return gid


def test_file(gid):
    return _bundle(gid) / TEST


def test(gid):
    """the game's played test (rating/test.toml), {} without one"""
    return _toml(test_file(gid)) if gid else {}


# --- the schema (tools/gamedb/check.py validates every analysis.toml with it) ---------------------------
# key: what it holds; a closed set, so a typo fails the check instead of vanishing
ANALYSIS_KEYS = {"legacy": dict, "reload": dict, "log_ignore": list, "log_strip": list, "interference": dict}
ENGINE_KEYS = ("log_ignore", "log_strip")
INTERFERENCE_KEYS = {"file": True, "action_re": True, "time_format": True, "anchor_re": False, "evidence": True}


def _rx(v, what, errs, groups=0):
    if not isinstance(v, str) or not v:
        errs.append("%s: a regex" % what)
        return
    try:
        if re.compile(v).groups < groups:
            errs.append("%s: needs %d capture group(s)" % (what, groups))
    except re.error as e:
        errs.append("%s %r: %s" % (what, v, e))


def validate_analysis(a, engine=False):
    """the problems of a parsed analysis.toml (an engine's: engine=True), [] when it is fine"""
    errs = []
    allowed = ENGINE_KEYS if engine else ANALYSIS_KEYS
    for k, v in a.items():
        if k not in allowed:
            errs.append("unknown key %r (%s)" % (k, ", ".join(allowed)))
        elif not isinstance(v, ANALYSIS_KEYS[k]):
            errs.append("%s should be a %s" % (k, "table" if ANALYSIS_KEYS[k] is dict else "list of tables"))
    if errs:
        return errs
    leg = a.get("legacy", {})
    for k in leg:
        if k not in ("recipe_names", "run_labels"):
            errs.append("legacy: unknown key %r (recipe_names, run_labels)" % k)
    names = leg.get("recipe_names", [])
    if not isinstance(names, list) or not all(isinstance(n, str) and n for n in names):
        errs.append("legacy: recipe_names is a list of names")
    labels = leg.get("run_labels", [])
    for i, r in enumerate(labels if isinstance(labels, list) else [None]):
        if not isinstance(r, dict) or set(r) - {"re", "variant", "drop"} or "re" not in r:
            errs.append("legacy: run_labels[%d]: a table of re, variant, drop" % i)
            continue
        _rx(r["re"], "legacy: run_labels[%d] re" % i, errs)
        if not isinstance(r.get("variant", ""), str) or not all(isinstance(x, str) for x in r.get("drop", [])):
            errs.append("legacy: run_labels[%d]: variant a string, drop a list of strings" % i)
    rl = a.get("reload", {})
    if rl:
        if set(rl) - {"re", "after", "fixed"} or ("re" in rl) == ("fixed" in rl) or ("after" in rl and "re" not in rl):
            errs.append("reload: re (and after) or fixed")
        if "re" in rl:
            _rx(rl["re"], "reload: re", errs)
        for k in ("after", "fixed"):
            if k in rl and (not isinstance(rl[k], (int, float)) or isinstance(rl[k], bool) or rl[k] <= 0):
                errs.append("reload: %s: seconds, more than 0" % k)
    for k in ("log_ignore", "log_strip"):
        for i, r in enumerate(a.get(k, [])):
            if not isinstance(r, dict) or set(r) != {"re", "why"} or not isinstance(r["why"], str) or not r["why"]:
                errs.append("%s[%d]: a table of re and why" % (k, i))
                continue
            _rx(r["re"], "%s[%d] re" % (k, i), errs)
    itf = a.get("interference")
    if itf is not None:
        for k, req in INTERFERENCE_KEYS.items():
            if req and k not in itf:
                errs.append("interference: %s is missing" % k)
        for k in itf:
            if k not in INTERFERENCE_KEYS:
                errs.append("interference: unknown key %r (%s)" % (k, ", ".join(INTERFERENCE_KEYS)))
            elif not isinstance(itf[k], str) or not itf[k]:
                errs.append("interference: %s is a string" % k)
        if isinstance(itf.get("file"), str) and not re.fullmatch(r"[\w.-]+", itf["file"]):
            errs.append("interference: file %r: a file of the run directory" % itf["file"])
        if "action_re" in itf:
            _rx(itf["action_re"], "interference: action_re", errs, groups=3)
        if "anchor_re" in itf:
            _rx(itf["anchor_re"], "interference: anchor_re", errs, groups=1)
    return errs
