"""desktop: the menu entries of one game (gamedb/games/<id>/desktop/launchers/) or one package stay
distinct:
  - the launchers without NoDisplay=true (or Hidden=true) have different Names: two menu items of one
    name can't be told apart;
  - an entry that only opens files (its Exec is a program and nothing but a %f/%F/%u/%U field code)
    while another visible entry of the group starts the same program is NoDisplay=true: it's a handler
    for the file manager or the browser, not a second menu item.
A changed .desktop file checks its whole group."""
import shlex
from pathlib import PurePath

NAME = "desktop"
TARGETS = ("**/*.desktop",)
SHARED_INPUTS = ()
FIELD_CODES = {"%f", "%F", "%u", "%U"}


def entry(text):
    """the [Desktop Entry] group's keys (the localized ones left out)"""
    out, group = {}, None
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        if s.startswith("["):
            group = s
            continue
        if group == "[Desktop Entry]" and "=" in s:
            k, v = s.split("=", 1)
            if "[" not in k:
                out.setdefault(k.strip(), v.strip())
    return out


def program(exec_):
    """(the program as a tuple of words, the arguments after it)"""
    try:
        w = shlex.split(exec_)
    except ValueError:
        w = exec_.split()
    while w and (w[0] == "env" or ("=" in w[0] and not w[0].startswith(("-", "%")))):
        w = w[1:]
    n = 2 if w and w[0] == "ps3-game" else 1
    return tuple(w[:n]), w[n:]


def group_of(path, ctx):
    p = PurePath(path)
    if p.full_match("gamedb/games/*/**"):
        return "/".join(p.parts[:3])
    for up in range(1, len(p.parts)):
        d = PurePath(*p.parts[:-up])
        if ctx.exists(str(d / "package.conf")):
            return str(d)
    return str(p.parent)


def run(ctx, files):
    groups = {group_of(c.path, ctx) for c in files if c.status != "D" or c.old}
    tracked = [p for p in ctx.tracked() if p.endswith(".desktop")]
    for g in sorted(groups):
        members = []
        for p in tracked:
            if group_of(p, ctx) != g:
                continue
            text = ctx.read(p)
            if text is not None:
                members.append((p, entry(text)))
        visible = [(p, e) for p, e in members if e.get("NoDisplay", "false").lower() != "true" and e.get("Hidden", "false").lower() != "true"]
        names = {}
        for p, e in visible:
            names.setdefault(e.get("Name", ""), []).append(p)
        for name, ps in sorted(names.items()):
            if len(ps) > 1:
                ctx.error(g, "%d visible menu entries are named %r: %s (a distinct Name each, or NoDisplay=true)" % (
                    len(ps), name, ", ".join(PurePath(x).name for x in ps)))
        for p, e in visible:
            prog, args = program(e.get("Exec", ""))
            if prog and args and set(args) <= FIELD_CODES:
                others = [q for q, f in visible if q != p and program(f.get("Exec", ""))[0] == prog]
                if others:
                    ctx.error(p, "only opens files (Exec %s), next to %s for the same program: a file handler is "
                              "NoDisplay=true" % (e.get("Exec"), ", ".join(PurePath(x).name for x in others)))
