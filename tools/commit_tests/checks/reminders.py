"""reminders: docs/reminders.toml lists dated reminders ([[reminder]] with due = "YYYY-MM-DD" and what = "...").
A warning for each one past its due date, never an error for a late one; a malformed file is an error.
WALNUT_TODAY=YYYY-MM-DD replaces today's date (the test uses it)."""
import datetime
import os
import tomllib

NAME = "reminders"
PATH = "docs/reminders.toml"
TARGETS = (PATH,)
SHARED_INPUTS = ()


def today():
    v = os.environ.get("WALNUT_TODAY")
    return datetime.date.fromisoformat(v) if v else datetime.date.today()


def select(ctx):
    """runs on every commit: the warning is the point, not a change to the file"""
    return True, None


def parse(data):
    """([(due date, text)], [schema problems])"""
    try:
        doc = tomllib.loads(data.decode("utf-8"))
    except (tomllib.TOMLDecodeError, UnicodeDecodeError) as e:
        return [], ["not valid TOML: %s" % e]
    bad, out = [], []
    for k in doc:
        if k != "reminder":
            bad.append("unknown top-level key %r (entries are [[reminder]])" % k)
    items = doc.get("reminder", [])
    if not isinstance(items, list):
        return [], bad + ["reminder must be an array of tables ([[reminder]])"]
    for i, e in enumerate(items, 1):
        if not isinstance(e, dict) or set(e) != {"due", "what"}:
            bad.append("entry %d: exactly the keys due and what" % i)
            continue
        what = e["what"]
        if not isinstance(what, str) or not what.strip():
            bad.append("entry %d: what must be a non-empty string" % i)
            continue
        due = e["due"]
        if not isinstance(due, str):
            bad.append("entry %d: due must be a quoted \"YYYY-MM-DD\" string" % i)
            continue
        try:
            out.append((datetime.date.fromisoformat(due), what))
        except ValueError:
            bad.append("entry %d: due %r is not a YYYY-MM-DD date" % (i, due))
    return out, bad


def run(ctx, files):
    data = ctx.read_bytes(PATH)
    if data is None:
        return
    entries, bad = parse(data)
    for b in bad:
        ctx.error(PATH, b)
    now = today()
    for due, what in sorted(entries):
        if due <= now:
            ctx.warn(PATH, "reminder due %s (%d days ago): %s" % (due, (now - due).days, what))
