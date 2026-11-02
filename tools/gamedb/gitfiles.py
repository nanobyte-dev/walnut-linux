"""gitfiles.py: directory listings that skip git-ignored files (editor swap and backup files, see .gitignore)."""
import subprocess
from pathlib import Path


def not_ignored(paths):
    """the paths (Path objects) git does not ignore; all of them when git cannot tell (no repository)"""
    paths = list(paths)
    if not paths:
        return paths
    try:
        r = subprocess.run(["git", "-C", str(paths[0].parent), "check-ignore", "-z", "--stdin"],
                           input="\0".join(str(p.absolute()) for p in paths) + "\0", capture_output=True, text=True)
        if r.returncode not in (0, 1):
            return paths
    except OSError:
        return paths
    bad = set(x for x in r.stdout.split("\0") if x)
    return [p for p in paths if str(p.absolute()) not in bad]


def iterdir(d):
    """Path(d).iterdir() without the git-ignored entries"""
    return not_ignored(Path(d).iterdir())
