#!/usr/bin/env python3
"""tools/hooks/agent-guard.py on allowed and refused commands (python3 -I tools/hooks/test_agent_guard.py)."""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GUARD = os.path.join(HERE, "agent-guard.py")

ALLOWED = [
    'git commit -m "mentions --no-verify"',
    "git commit -m 'use -n sparingly' -- tools/x.py",
    "git commit -am msg",
    "git commit -F msg.txt tools/x",
    "git commit -mnote",
    "git commit -m -n",
    "git push -n",
    "git push --dry-run origin main",
    "git push origin main",
    "echo git commit --no-verify",
    "git log -n 5",
    "git config user.name x",
    "git config --get core.hooksPath",
    "git -c user.name=x commit -m y",
    "ls .git/hooks",
    "rm -f build/tmp/x.lock",
    "chmod +x tools/hooks/agent-guard.py",
    "rm -rf build/tmp/a/.git/../x",
    "make test && git status",
    "nice -n 10 make -C x test",
    "",
]
REFUSED = [
    "git commit --no-verify -m x",
    "git commit -m x --no-verify",
    "git commit -n -m x",
    "git commit -an",
    "git commit -nm msg",
    "git commit --no-verif -m x",
    "git push --no-verify",
    "git push origin main --no-verify",
    "git -c core.hooksPath=/dev/null commit -m x",
    "git -c CORE.HOOKSPATH=/tmp/h commit -m x",
    "git -C sub -c core.hooksPath=x status",
    "git config core.hooksPath /tmp/h",
    "git config --global core.hookspath /tmp/h",
    "rm .git/index.lock",
    "rm -f /home/x/repo/.git/index.lock",
    "rm -f .git/index.*",
    "chmod -x .git/hooks/pre-commit",
    "rm -rf .git/hooks/*",
    "cd x && git commit --no-verify",
    "true; git commit -an",
    "false || git push --no-verify",
    "echo hi | git commit -n",
    "sudo git commit -n",
    "FOO=1 git commit -n",
    "bash -c 'git commit --no-verify'",
    "(git commit -n)",
    "git commit -m 'a\nb'\ngit commit -n",
]


def run(cmd):
    p = subprocess.run([sys.executable, "-I", GUARD], input=json.dumps({"tool_name": "Bash", "tool_input": {"command": cmd}}),
                       capture_output=True, text=True)
    return p.returncode, p.stderr


def main():
    bad = 0
    for c in ALLOWED:
        rc, err = run(c)
        if rc != 0:
            bad += 1
            print("FAIL allowed but refused: %r: %s" % (c, err.strip()))
    for c in REFUSED:
        rc, err = run(c)
        if rc != 2 or not err.strip():
            bad += 1
            print("FAIL refused expected: %r: rc=%d" % (c, rc))
    for raw in ("not json", "{}", '{"tool_input": {"command": 5}}', '{"tool_input": {"command": "echo \\"x"}}'):
        p = subprocess.run([sys.executable, "-I", GUARD], input=raw, capture_output=True, text=True)
        if p.returncode != 0:
            bad += 1
            print("FAIL malformed input %r must pass: rc=%d" % (raw, p.returncode))
    print("%s: %d allowed, %d refused cases, %d failed" % ("ok" if not bad else "FAILED", len(ALLOWED), len(REFUSED), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
