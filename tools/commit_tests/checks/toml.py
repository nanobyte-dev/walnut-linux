"""toml: every TOML file parses (tomllib)."""
import tomllib

NAME = "toml"
TARGETS = ("**/*.toml",)
SHARED_INPUTS = ()


def run(ctx, files):
    for c in files:
        if c.status == "D":
            continue
        b = ctx.read_bytes(c.path)
        if b is None:
            continue
        try:
            tomllib.loads(b.decode("utf-8"))
        except UnicodeDecodeError as e:
            ctx.error(c.path, "not UTF-8: %s" % e)
        except tomllib.TOMLDecodeError as e:
            ctx.error(c.path, "doesn't parse: %s" % e)
