"""batches: a new or changed [[batch]] of gamedb/benchmarks/batches.toml has its hand-written tested (what
was tried and why) and conclusion (what it showed, what was decided; gamedb/benchmarks/README.md). A batch
is matched to the base commit's by name; one the base doesn't have is new."""
import tomllib

NAME = "batches"
TARGETS = ("gamedb/benchmarks/batches.toml",)
SHARED_INPUTS = ()
NEEDS = ("tested", "conclusion")


def batches(text):
    return {b.get("name"): b for b in tomllib.loads(text).get("batch", [])} if text else {}


def run(ctx, files):
    for c in files:
        if c.status == "D":
            continue
        text = ctx.read(c.path)
        if text is None:
            continue
        try:
            now = batches(text)
        except tomllib.TOMLDecodeError:
            continue    # the toml check reports it
        try:
            before = {} if c.status == "*" and ctx.mode == "all" else batches(ctx.base_text(c.path))
        except tomllib.TOMLDecodeError:
            before = {}
        for name, b in now.items():
            if c.status != "*" and before.get(name) == b:
                continue
            missing = [k for k in NEEDS if not (isinstance(b.get(k), str) and b[k].strip())]
            if missing:
                ctx.error(c.path, "batch %r (%s) has no %s" % (name, "new" if name not in before else "changed",
                                                               " and no ".join(missing)))
