#!/usr/bin/env python3
"""schema.py [--all | --coverage | --targets | FILE...]: GameDB's file schemas (gamedb/schemas/), stdlib only.

Every file under gamedb/ is of exactly one kind, a schema in gamedb/schemas/<kind>.toml, or on the explicit
list of files without one (gamedb/schemas/no-schema.toml: patches, debian/, source code, scripts, images,
raw/ evidence). A schema names the paths it governs (globs: `*` within a path component, `**` across
them), the file's format and every key: its type, whether it is required (always, or only for a draft or a
published game), the allowed values or a pattern, a one-line meaning. A Markdown kind also says its front
matter and the expected body: the title line and its `##` sections (required or allowed, in order).
gamedb/schemas/schema.toml describes the schema format itself.

The schemas are the one source of the key sets: tools/gamedb/check.py and bench/analysis/benchrecords.py
read their keys, types, required fields and allowed values from here (fields(), types(), required(),
enum()), gamedb/README.md's field tables are generated from them (tools/gamedb/blocks.py `schema-table`).

API
  load()                      {kind: schema dict} (cached); SchemaError on a broken schema file
  kind_of(path)               the kind governing a file (a path in the repository), None when no schema does
  validate(path)              [problems] of one file against its kind's schema
  coverage(files=None)        [problems]: a gamedb file of no kind, or of two
  targets_and_shared_inputs() {kind: {"targets": [globs], "shared": [paths/globs]}}: what a schema judges,
                              and what changes how it judges (the schema itself, this file, the files a
                              key refers to: tags.toml for tags, ...), for the commit-tests runner
CLI: FILE... validates those files; --all every gamedb file (and the coverage); --coverage only that;
--targets prints targets_and_shared_inputs() as JSON. Exit 1 on any problem.
"""
import argparse
import datetime
import json
import re
import subprocess
import sys
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
SCHEMAS = REPO / "gamedb" / "schemas"
NO_SCHEMA = "no-schema"
SELF = "tools/gamedb/schema.py"

TYPES = {"str": str, "int": int, "float": float, "number": (int, float), "bool": bool,
         "date": datetime.date, "datetime": datetime.datetime, "list": list, "table": dict, "tables": list,
         "any": object}
TYPE_WORDS = {"str": "a string", "int": "an integer", "float": "a float", "number": "a number", "bool": "true or false",
              "date": "a date", "datetime": "a date and time", "list": "a list", "table": "a table",
              "tables": "an array of tables", "any": "anything"}
FORMATS = ("toml", "markdown", "keylines", "desktop", "ini", "conf", "lines", "schema")
REQUIRED = (True, False, "draft", "published")
SCHEMA_TOP = {"kind", "title", "doc", "paths", "format", "state", "shared_inputs", "enums", "keys", "other",
              "other_keys", "other_ref", "front_matter", "body", "groups", "other_groups", "line", "comment"}
SPEC_KEYS = {"type", "items", "required", "required_when", "only_when", "enum", "pattern", "min", "nonempty",
             "keys", "other", "other_keys", "other_ref", "ref", "meaning", "by", "recommended", "repeat",
             "default"}
BODY_KEYS = {"title", "nonempty", "sections", "other_sections"}
SECTION_KEYS = {"name", "match", "required", "when", "meaning"}
GROUP_KEYS = {"required", "match", "keys", "meaning"}


class SchemaError(Exception):
    pass


# --- refs: values that must name something in another file (its shared input) ------------------------------
def _toml(path):
    try:
        return tomllib.loads(Path(path).read_text())
    except (OSError, tomllib.TOMLDecodeError, UnicodeDecodeError):
        return {}


def _ref_tags(root):
    out = set()
    for g in _toml(Path(root) / "gamedb" / "tags.toml").get("group", []):
        for t in g.get("tag", []) if isinstance(g, dict) else []:
            for x in [t] + list(t.get("sub", [])) if isinstance(t, dict) else []:
                if isinstance(x, dict) and x.get("automatic") is not True:
                    out.add(x.get("id"))
    return out


def _ref_named(rel, table):
    def f(root):
        return {x.get("name") for x in _toml(Path(root) / rel).get(table, []) if isinstance(x, dict)}
    return f


def _ref_games(root):
    d = Path(root) / "gamedb" / "games"
    return {x.name for x in d.iterdir() if (x / "index.md").is_file()} if d.is_dir() else set()


# name: (the shared input it reads, the set of names)
REFS = {
    "tags": ("gamedb/tags.toml", _ref_tags),
    "metrics": ("gamedb/benchmarks/metrics.toml", _ref_named("gamedb/benchmarks/metrics.toml", "metric")),
    "batches": ("gamedb/benchmarks/batches.toml", _ref_named("gamedb/benchmarks/batches.toml", "batch")),
    "games": ("gamedb/games/*/index.md", _ref_games),
}


# --- loading ------------------------------------------------------------------------------------------------
def glob_re(pat):
    """a path glob as a regex: `**/` any directories (none too), `**` anything, `*` and `?` within a component"""
    out, i = "", 0
    while i < len(pat):
        if pat.startswith("**/", i):
            out, i = out + "(?:.*/)?", i + 3
        elif pat.startswith("**", i):
            out, i = out + ".*", i + 2
        elif pat[i] == "*":
            out, i = out + "[^/]*", i + 1
        elif pat[i] == "?":
            out, i = out + "[^/]", i + 1
        else:
            out, i = out + re.escape(pat[i]), i + 1
    return re.compile(out + r"\Z")


def _spec_errors(spec, where, enums, named=True):
    """the problems of one key spec (recursively)"""
    errs = []
    if isinstance(spec, str):
        spec = {"type": spec}
    if not isinstance(spec, dict):
        return ["%s: a key spec is a table or a type name" % where]
    for k in sorted(set(spec) - SPEC_KEYS):
        errs.append("%s: unknown spec key %r" % (where, k))
    t = spec.get("type", "any")
    if t not in TYPES:
        errs.append("%s: type %r: one of %s" % (where, t, ", ".join(TYPES)))
    if named and not spec.get("meaning"):
        errs.append("%s: no meaning" % where)
    if spec.get("required", False) not in REQUIRED:
        errs.append("%s: required is true, false, \"draft\" or \"published\"" % where)
    e = spec.get("enum")
    if isinstance(e, str):
        if not e.startswith("@") or e[1:] not in enums:
            errs.append("%s: enum %r names no [enums] list" % (where, e))
    elif e is not None and not isinstance(e, list):
        errs.append("%s: enum is a list or \"@name\"" % where)
    for k in ("pattern", "other_keys"):
        if k in spec:
            try:
                re.compile(spec[k])
            except (re.error, TypeError) as x:
                errs.append("%s: %s: %s" % (where, k, x))
    for k in ("ref", "other_ref"):
        if k in spec and spec[k] not in REFS:
            errs.append("%s: %s %r: one of %s" % (where, k, spec[k], ", ".join(REFS)))
    for k in ("required_when", "only_when"):
        if k in spec and not isinstance(spec[k], dict):
            errs.append("%s: %s is a table {key = value}" % (where, k))
    if "items" in spec:
        errs += _spec_errors(spec["items"], where + "[]", enums, named=False)
    if "other" in spec:
        errs += _spec_errors(spec["other"], where + ".*", enums, named=False)
    for k, s in (spec.get("keys") or {}).items():
        errs += _spec_errors(s, "%s.%s" % (where, k), enums)
    return errs


def schema_errors(s, name="schema"):
    """the problems of a parsed schema file (the format of gamedb/schemas/schema.toml)"""
    if name == NO_SCHEMA:
        errs = ["%s: unknown key %r (title, [[entry]])" % (name, k) for k in s if k not in ("title", "entry")]
        for i, e in enumerate(s.get("entry", [])):
            if not isinstance(e, dict) or set(e) - {"paths", "why"} or not e.get("why") or \
                    not isinstance(e.get("paths"), list) or not e["paths"]:
                errs.append("%s: entry %d: paths (a list of globs) and why" % (name, i + 1))
        return errs
    errs = ["%s: unknown key %r" % (name, k) for k in sorted(set(s) - SCHEMA_TOP)]
    if s.get("kind") != name:
        errs.append("%s: kind %r is not the file's name" % (name, s.get("kind")))
    if s.get("format") not in FORMATS:
        errs.append("%s: format %r: one of %s" % (name, s.get("format"), ", ".join(FORMATS)))
    if not s.get("title"):
        errs.append("%s: no title" % name)
    if not isinstance(s.get("paths"), list) or not s["paths"] or not all(isinstance(p, str) for p in s["paths"]):
        errs.append("%s: paths is a list of globs" % name)
    enums = s.get("enums", {})
    if not isinstance(enums, dict) or not all(isinstance(v, list) for v in enums.values()):
        errs.append("%s: enums is a table of lists" % name)
        enums = {}
    for k, spec in (s.get("keys") or {}).items():
        errs += _spec_errors(spec, "%s: %s" % (name, k), enums)
    if "other" in s:
        errs += _spec_errors(s["other"], "%s: *" % name, enums, named=False)
    for k in ("other_keys", "line", "comment"):
        if k in s:
            try:
                re.compile(s[k])
            except (re.error, TypeError) as x:
                errs.append("%s: %s: %s" % (name, k, x))
    if s.get("format") == "markdown":
        if s.get("front_matter") not in ("toml", "none", "optional"):
            errs.append("%s: front_matter: toml, none or optional" % name)
        b = s.get("body", {})
        errs += ["%s: body: unknown key %r" % (name, k) for k in sorted(set(b) - BODY_KEYS)]
        for i, sec in enumerate(b.get("sections", [])):
            if set(sec) - SECTION_KEYS or ("name" in sec) == ("match" in sec):
                errs.append("%s: body.sections %d: name or match (and required, when, meaning)" % (name, i + 1))
            if sec.get("required", False) not in REQUIRED:
                errs.append("%s: body.sections %d: required is true, false, \"draft\" or \"published\"" % (name, i + 1))
    for g, spec in list((s.get("groups") or {}).items()) + ([("*", s["other_groups"])] if "other_groups" in s else []):
        errs += ["%s: group %s: unknown key %r" % (name, g, k) for k in sorted(set(spec) - GROUP_KEYS)]
        for k, ks in (spec.get("keys") or {}).items():
            errs += _spec_errors(ks, "%s: [%s] %s" % (name, g, k), enums)
    return errs


_CACHE = {}


def load(d=SCHEMAS):
    """{kind: schema} of every gamedb/schemas/*.toml but no-schema.toml (that one: load_no_schema())"""
    d = Path(d)
    if d not in _CACHE:
        out, errs = {}, []
        for f in sorted(d.glob("*.toml")):
            try:
                s = tomllib.loads(f.read_text())
            except tomllib.TOMLDecodeError as e:
                errs.append("%s: %s" % (f.name, e))
                continue
            errs += schema_errors(s, f.stem)
            if f.stem != NO_SCHEMA:
                s["_file"] = f
                out[f.stem] = s
        if errs:
            raise SchemaError("; ".join(errs))
        _CACHE[d] = out
    return _CACHE[d]


def load_no_schema(d=SCHEMAS):
    f = Path(d) / (NO_SCHEMA + ".toml")
    return tomllib.loads(f.read_text()).get("entry", []) if f.is_file() else []


# --- the accessors check.py and benchrecords.py use ------------------------------------------------------
def spec(kind, *path, d=SCHEMAS):
    """the spec at path in a kind's schema: key names, "[]" for a list's items, "*" for a table's other keys;
    no path: the schema itself (its keys are the file's top level)"""
    s = load(d)[kind]
    for p in path:
        if isinstance(s, str):
            s = {"type": s}
        if p == "[]":
            s = s["items"]
        elif p == "*":
            s = s["other"]
        else:
            s = s["keys"][p]
    return {"type": s} if isinstance(s, str) else s


def pytype(t):
    return TYPES[t if isinstance(t, str) else t.get("type", "any")]


def types(kind, *path, d=SCHEMAS):
    """{key: its Python type} of the table at path, in the schema's order"""
    return {k: pytype(v) for k, v in (spec(kind, *path, d=d).get("keys") or {}).items()}


def fields(kind, *path, d=SCHEMAS):
    """{key: (Python type, required for a published entry)} of the table at path"""
    return {k: (pytype(v), _req(v, "published")) for k, v in (spec(kind, *path, d=d).get("keys") or {}).items()}


def required(kind, *path, state="published", d=SCHEMAS):
    """the keys of the table at path that state (published or draft) requires"""
    return tuple(k for k, v in (spec(kind, *path, d=d).get("keys") or {}).items() if _req(v, state))


def enum(kind, *path, d=SCHEMAS):
    """the allowed values at path (its enum, an [enums] list resolved)"""
    e = spec(kind, *path, d=d).get("enum")
    if isinstance(e, str):
        e = load(d)[kind]["enums"][e[1:]]
    return tuple(e or ())


def keyed(kind, attr, *path, d=SCHEMAS):
    """the keys of the table at path with attr set (e.g. "recommended", or by = "hand": keyed(.., "by=hand"))"""
    a, _, want = attr.partition("=")
    return tuple(k for k, v in (spec(kind, *path, d=d).get("keys") or {}).items()
                 if isinstance(v, dict) and (str(v.get(a)) == want if want else v.get(a)))


def _req(s, state):
    r = s.get("required", False) if isinstance(s, dict) else False
    return r is True or r == state


# --- which kind ---------------------------------------------------------------------------------------------
def _rel(path, root=None):
    p = Path(path)
    root = Path(root) if root else REPO
    try:
        return p.resolve().relative_to(Path(root).resolve()).as_posix() if p.is_absolute() else p.as_posix()
    except ValueError:
        return p.as_posix()


def kinds_matching(rel, d=SCHEMAS):
    return [k for k, s in load(d).items() if any(glob_re(g).match(rel) for g in s["paths"])]


def no_schema_entry(rel, d=SCHEMAS):
    return next((e for e in load_no_schema(d) if any(glob_re(g).match(rel) for g in e["paths"])), None)


def kind_of(path, root=None, d=SCHEMAS):
    """the kind of a file (a path, relative to the repository or absolute), None when no schema matches"""
    ks = kinds_matching(_rel(path, root), d)
    return ks[0] if len(ks) == 1 else None


def gamedb_files(root=None):
    """the files under gamedb/: git's (tracked and untracked, not ignored), else every file"""
    root = Path(root or REPO)
    try:
        out = subprocess.run(["git", "-C", str(root), "ls-files", "-co", "--exclude-standard", "--", "gamedb"],
                             capture_output=True, text=True, check=True).stdout.split("\n")
        return sorted(x for x in out if x and (root / x).is_file())
    except (OSError, subprocess.CalledProcessError):
        return sorted(f.relative_to(root).as_posix() for f in (root / "gamedb").rglob("*") if f.is_file())


def coverage(files=None, root=None, d=SCHEMAS):
    """[problems]: every gamedb file is of exactly one kind, or on the no-schema list (a schema's match
    wins over that list's)"""
    errs = []
    for rel in files if files is not None else gamedb_files(root):
        ks = kinds_matching(rel, d)
        if len(ks) > 1:
            errs.append("%s: matches the schemas %s (exactly one may)" % (rel, ", ".join(ks)))
        elif not ks and no_schema_entry(rel, d) is None:
            errs.append("%s: no schema matches it and it is not in gamedb/schemas/no-schema.toml "
                        "(a new kind of file needs its schema)" % rel)
    return errs


def targets_and_shared_inputs(d=SCHEMAS):
    """{kind: {"targets": [globs], "shared": [paths and globs]}}: a change to a target re-checks that file, a
    change to a shared input every target (the schema, this validator, the files the keys refer to)"""
    out = {}
    for k, s in load(d).items():
        shared = [_rel(s["_file"]), SELF]
        for r in sorted(_refs(s)):
            if REFS[r][0] not in shared:
                shared.append(REFS[r][0])
        shared += [x for x in s.get("shared_inputs", []) if x not in shared]
        out[k] = {"targets": list(s["paths"]), "shared": shared}
    return out


def _refs(s):
    out = set()
    if isinstance(s, dict):
        for k in ("ref", "other_ref"):
            if k in s:
                out.add(s[k])
        for k in ("items", "other"):
            out |= _refs(s.get(k))
        for v in list((s.get("keys") or {}).values()) + list((s.get("groups") or {}).values()) + \
                ([s["other_groups"]] if "other_groups" in s else []):
            out |= _refs(v)
    return out


# --- validation ---------------------------------------------------------------------------------------------
class _Ctx:
    def __init__(self, schema, root, state):
        self.schema, self.root, self.state, self._refs = schema, Path(root), state, {}

    def ref(self, name):
        if name not in self._refs:
            self._refs[name] = REFS[name][1](self.root)
        return self._refs[name]

    def enum(self, e):
        return self.schema.get("enums", {}).get(e[1:], []) if isinstance(e, str) else e


def _type_ok(v, t):
    if t == "any":
        return True
    if t in ("int", "float", "number") and isinstance(v, bool):
        return False
    if t == "date":
        return isinstance(v, datetime.date) and not isinstance(v, datetime.datetime)
    if t == "tables":
        return isinstance(v, list) and all(isinstance(x, dict) for x in v)
    return isinstance(v, TYPES[t])


def _join(where, k):
    return "%s.%s" % (where, k) if where else str(k)


def _cond(cond, table):
    return all(table.get(k) == v for k, v in (cond or {}).items())


def _value(v, s, where, ctx, errs):
    if isinstance(s, str):
        s = {"type": s}
    t = s.get("type", "any")
    if not _type_ok(v, t):
        errs.append("%s: should be %s" % (where, TYPE_WORDS[t]))
        return
    e = ctx.enum(s.get("enum"))
    if e and v not in e:
        errs.append("%s: %r is not one of %s" % (where, v, ", ".join(map(str, e))))
    if "pattern" in s and isinstance(v, str) and not re.fullmatch(s["pattern"], v, re.S):
        errs.append("%s: %r does not match %s" % (where, v if len(v) < 60 else v[:57] + "...", s["pattern"]))
    if "min" in s and isinstance(v, (int, float)) and v < s["min"]:
        errs.append("%s: %r is less than %s" % (where, v, s["min"]))
    if s.get("nonempty") and (not v or (isinstance(v, str) and not v.strip())):
        errs.append("%s: empty" % where)
    if "ref" in s and isinstance(v, str) and v not in ctx.ref(s["ref"]):
        errs.append("%s: %r is no name of %s" % (where, v, REFS[s["ref"]][0]))
    if t == "list" and "items" in s:
        for i, x in enumerate(v):
            _value(x, s["items"], "%s[%d]" % (where, i), ctx, errs)
    elif t == "table":
        _table(v, s, where, ctx, errs)
    elif t == "tables":
        for i, x in enumerate(v):
            _table(x, s, "%s[%d]" % (where, i), ctx, errs)


def _table(d, s, where, ctx, errs):
    keys = s.get("keys") or {}
    other = s.get("other")
    for k, v in d.items():
        w = _join(where, k)
        if k in keys:
            ks = keys[k]
            if isinstance(ks, dict) and "only_when" in ks and not _cond(ks["only_when"], d):
                errs.append("%s: only where %s" % (w, ", ".join("%s = %r" % x for x in ks["only_when"].items())))
            _value(v, ks, w, ctx, errs)
        elif other is not None:
            if "other_keys" in s and not re.fullmatch(s["other_keys"], str(k)):
                errs.append("%s: the key does not match %s" % (w, s["other_keys"]))
            if "other_ref" in s and k not in ctx.ref(s["other_ref"]):
                errs.append("%s: unknown key (no name of %s)" % (w, REFS[s["other_ref"]][0]))
            _value(v, other, w, ctx, errs)
        else:
            errs.append("%s: unknown key (%s)" % (w, ", ".join(keys) or "no keys"))
    for k, ks in keys.items():
        if k in d or not isinstance(ks, dict):
            continue
        if _req(ks, ctx.state) or ("required_when" in ks and _cond(ks["required_when"], d)):
            errs.append("%s: missing" % _join(where, k))


def state_of(path, schema):
    """draft or published: the state a file is judged in (schema state: "self", the front matter's draft; a
    path relative to the file's folder: that file's front matter)"""
    st = schema.get("state")
    if not st:
        return "published"
    f = Path(path) if st == "self" else Path(path).parent / st
    try:
        fm = _front_matter(f.read_text())[0]
    except (OSError, UnicodeDecodeError, ValueError, tomllib.TOMLDecodeError):
        return "published"
    return "draft" if (fm or {}).get("draft") is True else "published"


def _front_matter(text):
    """(front matter dict or None, the body, the body's first line number)"""
    m = re.match(r"\+\+\+\n(.*?)\n\+\+\+\n?", text, re.S)
    if not m:
        return None, text, 1
    return tomllib.loads(m[1]), text[m.end():], text.count("\n", 0, m.end()) + 1


def _markdown(text, s, ctx):
    errs = []
    fmode = s.get("front_matter", "none")
    try:
        fm, body, _ = _front_matter(text)
    except tomllib.TOMLDecodeError as e:
        return ["front matter: %s" % e]
    if fm is None and text.startswith("---"):
        errs.append("front matter: +++ TOML, not --- YAML")
    if fmode == "toml" and fm is None:
        return errs + ["no +++ TOML front matter"]
    if fmode == "none" and fm is not None:
        return ["front matter: this kind has none"]
    if fm is not None:
        if s.get("state") == "self":
            ctx.state = "draft" if fm.get("draft") is True else "published"
        _table(fm, s, "", ctx, errs)
    b = s.get("body") or {}
    lines = body.split("\n")
    first = next((x for x in lines if x.strip()), "")
    if "title" in b and not re.fullmatch(b["title"], first):
        errs.append("body: the first line %r is not the title line (%s)" % (first[:60], b["title"]))
    if b.get("nonempty") and len([x for x in lines if x.strip()]) < 2:
        errs.append("body: a title and nothing else")
    heads, fence = [], False
    for x in lines:
        if x.lstrip().startswith("```"):
            fence = not fence
        elif not fence and x.startswith("## "):
            heads.append(x[3:].strip())
    secs = b.get("sections", [])
    if secs or b.get("other_sections") is False:
        def which(h):
            return next((i for i, sec in enumerate(secs)
                         if (sec.get("name") == h if "name" in sec else re.fullmatch(sec["match"], h))), None)
        last, seen = -1, set()
        for h in heads:
            i = which(h)
            if i is None:
                if b.get("other_sections", True) is False:
                    errs.append("body: section %r is not one of %s" % (h, ", ".join(x.get("name", x.get("match")) for x in secs)))
                continue
            if i < last:
                errs.append("body: section %r comes after %r (the order: %s)" % (
                    h, secs[last].get("name", secs[last].get("match")), ", ".join(x.get("name", x.get("match")) for x in secs)))
            last = max(last, i)
            seen.add(i)
        for i, sec in enumerate(secs):
            if i not in seen and (_req(sec, ctx.state) or ("when" in sec and _cond(sec["when"], fm or {}))):
                errs.append("body: no section %r" % sec.get("name", sec.get("match")))
    return errs


def _text_value(v, s, where, ctx, errs):
    """a value of a text format (keylines, desktop, ini, conf): typed from its text"""
    t = s.get("type", "str") if isinstance(s, dict) else s
    if t == "bool":
        if v not in ("true", "false"):
            errs.append("%s: %r is not true or false" % (where, v))
        return
    if t == "int":
        if not re.fullmatch(r"-?\d+", v):
            errs.append("%s: %r is not an integer" % (where, v))
        return
    _value(v, s if isinstance(s, dict) else {"type": "str"}, where, ctx, errs)


def _keylines(text, s, ctx):
    errs, seen = [], {}
    keys = s.get("keys") or {}
    for n, line in enumerate(text.split("\n"), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        m = re.match(r"\s*[-*]?\s*([A-Za-z_]+):\s*(.*?)\s*\Z", line)
        if not m:
            errs.append("line %d: not `key: value` or a # comment" % n)
            continue
        k, v = m[1].lower(), m[2]
        if k not in keys:
            errs.append("line %d: unknown key %r (%s)" % (n, k, ", ".join(keys)))
            continue
        if k in seen and not keys[k].get("repeat"):
            errs.append("line %d: %s twice (first on line %d)" % (n, k, seen[k]))
        seen.setdefault(k, n)
        _text_value(v, keys[k], "line %d: %s" % (n, k), ctx, errs)
    errs += ["%s: missing" % k for k, ks in keys.items() if k not in seen and _req(ks, ctx.state)]
    return errs


def _groups(text, s, ctx, sep="="):
    """desktop and ini files: [group] headers, key=value lines, # comments"""
    errs, groups, cur, seen = [], s.get("groups") or {}, None, {}
    other = s.get("other_groups")
    gspec = None
    for n, line in enumerate(text.split("\n"), 1):
        x = line.strip()
        if not x or x.startswith(("#", ";")):
            continue
        m = re.fullmatch(r"\[([^\]]+)\]", x)
        if m:
            cur = m[1]
            if cur in seen:
                errs.append("line %d: [%s] twice" % (n, cur))
            seen[cur] = set()
            gspec = groups.get(cur)
            if gspec is None and other is not None and re.fullmatch(other.get("match", ".*"), cur):
                gspec = other
            if gspec is None:
                errs.append("line %d: unknown group [%s] (%s)" % (n, cur, ", ".join(groups)))
            continue
        k, eq, v = line.partition(sep)
        k = k.strip()
        if not eq:
            errs.append("line %d: not key%svalue" % (n, sep))
            continue
        if cur is None:
            errs.append("line %d: %s before any [group]" % (n, k))
            continue
        if gspec is None:
            continue
        keys = gspec.get("keys") or {}
        if k not in keys:
            errs.append("line %d: [%s] unknown key %r (%s)" % (n, cur, k, ", ".join(keys)))
            continue
        if k in seen[cur]:
            errs.append("line %d: [%s] %s twice" % (n, cur, k))
        seen[cur].add(k)
        _text_value(v.strip(), keys[k], "[%s] %s" % (cur, k), ctx, errs)
    for g, gs in groups.items():
        if g not in seen:
            if gs.get("required"):
                errs.append("no [%s] group" % g)
            continue
        errs += ["[%s] %s: missing" % (g, k) for k, ks in (gs.get("keys") or {}).items()
                 if k not in seen[g] and _req(ks, ctx.state)]
    if other is not None:
        for g in seen:
            if g not in groups and re.fullmatch(other.get("match", ".*"), g):
                errs += ["[%s] %s: missing" % (g, k) for k, ks in (other.get("keys") or {}).items()
                         if k not in seen[g] and _req(ks, ctx.state)]
    return errs


def _conf(text, s, ctx):
    """shell-style KEY=value assignments (package.conf), # comments"""
    errs, seen, d = [], {}, {}
    keys = s.get("keys") or {}
    for n, line in enumerate(text.split("\n"), 1):
        x = line.strip()
        if not x or x.startswith("#"):
            continue
        m = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)=(.*)", x)
        if not m:
            errs.append("line %d: not KEY=value or a # comment" % n)
            continue
        k, v = m[1], m[2]
        if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
            v = v[1:-1]
        if k in seen:
            errs.append("line %d: %s twice (first on line %d)" % (n, k, seen[k]))
        seen.setdefault(k, n)
        d[k] = v
    for k, v in d.items():
        if k in keys:
            _text_value(v, keys[k], k, ctx, errs)
        elif "other_keys" in s and re.fullmatch(s["other_keys"], k):
            _text_value(v, s.get("other", {"type": "str"}), k, ctx, errs)
        else:
            errs.append("%s: unknown key (%s)" % (k, ", ".join(keys)))
    errs += ["%s: missing" % k for k, ks in keys.items() if k not in d and _req(ks, ctx.state)]
    return errs


def _lines(text, s, ctx):
    errs = []
    for n, line in enumerate(text.split("\n"), 1):
        if not line.strip() or ("comment" in s and re.fullmatch(s["comment"], line.strip())):
            continue
        if not re.fullmatch(s["line"], line):
            errs.append("line %d: %r does not match %s" % (n, line[:60], s["line"]))
    return errs


def validate_text(text, kind, path="", root=None, d=SCHEMAS):
    """[problems] of a file's text as kind (path, when given, decides a draft's state)"""
    s = load(d)[kind]
    ctx = _Ctx(s, root or REPO, state_of(path, s) if path and s.get("state") not in (None, "self") else "published")
    fmt = s["format"]
    if fmt == "toml":
        try:
            data = tomllib.loads(text)
        except tomllib.TOMLDecodeError as e:
            return ["%s" % e]
        errs = []
        _table(data, s, "", ctx, errs)
        return errs
    if fmt == "markdown":
        return _markdown(text, s, ctx)
    if fmt == "keylines":
        return _keylines(text, s, ctx)
    if fmt in ("desktop", "ini"):
        return _groups(text, s, ctx)
    if fmt == "conf":
        return _conf(text, s, ctx)
    if fmt == "lines":
        return _lines(text, s, ctx)
    if fmt == "schema":
        try:
            data = tomllib.loads(text)
        except tomllib.TOMLDecodeError as e:
            return ["%s" % e]
        return schema_errors(data, Path(path).stem if path else "schema")
    return ["format %r has no validator" % fmt]


def validate(path, root=None, d=SCHEMAS):
    """[problems] of one file against its kind's schema (a file of no kind, or of two, is a problem)"""
    root = Path(root or REPO)
    p = Path(path) if Path(path).is_absolute() else root / path
    rel = _rel(p, root)
    ks = kinds_matching(rel, d)
    if not ks:
        return [] if no_schema_entry(rel, d) else ["no schema matches it (gamedb/schemas/)"]
    if len(ks) > 1:
        return ["matches the schemas %s (exactly one may)" % ", ".join(ks)]
    try:
        text = p.read_text()
    except (OSError, UnicodeDecodeError) as e:
        return ["unreadable: %s" % e]
    return validate_text(text, ks[0], p, root, d)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--all", action="store_true", help="every gamedb file, and the coverage")
    ap.add_argument("--coverage", action="store_true", help="only: every gamedb file has one kind or none on purpose")
    ap.add_argument("--targets", action="store_true", help="print targets_and_shared_inputs() as JSON")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args(argv)
    try:
        load()
    except SchemaError as e:
        print("schema.py: broken schema: %s" % e, file=sys.stderr)
        return 1
    if a.targets:
        print(json.dumps(targets_and_shared_inputs(), indent=1))
        return 0
    probs = []
    if a.all or a.coverage:
        files = gamedb_files()
        probs += coverage(files)
        if a.all:
            for f in files:
                probs += ["%s: %s" % (f, e) for e in validate(f) if not e.startswith("no schema matches")]
    else:
        if not a.files:
            ap.error("FILE..., --all or --coverage")
        for f in a.files:
            probs += ["%s: %s" % (f, e) for e in validate(Path(f).resolve() if Path(f).exists() else f)]
    for p in probs:
        print("!! " + p, file=sys.stderr)
    print("schema.py: %d schemas, %d problems" % (len(load()), len(probs)))
    return 1 if probs else 0


if __name__ == "__main__":
    sys.exit(main())
