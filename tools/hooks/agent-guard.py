#!/usr/bin/env python3
"""PreToolUse hook for Bash (see .claude/settings.json). Reads the hook JSON on stdin, splits
tool_input.command into commands (on ; && || | & newline and parentheses) and refuses, with exit 2
and a reason on stderr:
  - git commit --no-verify, or -n alone or bundled in short flags (-an, -nm)
  - git push --no-verify (push -n is --dry-run and stays allowed)
  - git -c core.hooksPath=..., git config core.hooksPath ...
  - rm, mv, unlink or chmod of .git/index.lock or .git/hooks/*
Only the parsed commands are looked at, never the text of a message. Run as python3 -I."""
import fnmatch
import json
import posixpath
import shlex
import sys

SEPARATORS = {";", "&&", "||", "|", "&", "|&", "(", ")", "\n", ";;", "`"}
WRAPPERS = {"env", "sudo", "command", "nice", "ionice", "time", "exec", "nohup", "builtin", "stdbuf", "timeout"}
SHELLS = {"bash", "sh", "zsh", "dash"}
# git commit / push options that take their value as the next word
COMMIT_VALUE_LONG = {"--message", "--file", "--author", "--date", "--cleanup", "--template", "--reuse-message",
                     "--reedit-message", "--fixup", "--squash", "--gpg-sign-key", "--pathspec-from-file"}
COMMIT_VALUE_SHORT = set("mFCct")      # a letter that takes a value ends a bundle: -nm msg, -mfoo
PUSH_VALUE_LONG = {"--repo", "--push-option", "--receive-pack", "--exec", "--force-with-lease-x"}
GIT_VALUE_OPTS = {"-C", "--git-dir", "--work-tree", "--namespace", "--exec-path", "--super-prefix"}


def refuse(why):
    sys.stderr.write("agent-guard: refused: %s (see CLAUDE.md, Commits and agents)\n" % why)
    sys.exit(2)


def split_commands(cmd):
    lex = shlex.shlex(cmd, posix=True, punctuation_chars=";&|()`\n")
    lex.whitespace = " \t\r"
    lex.whitespace_split = True
    lex.commenters = ""
    out, cur = [], []
    for tok in lex:
        if tok and set(tok) <= set(";&|()`\n"):
            if cur:
                out.append(cur)
            cur = []
        else:
            cur.append(tok)
    if cur:
        out.append(cur)
    return out


def strip_wrappers(argv):
    while argv:
        w = posixpath.basename(argv[0])
        if "=" in argv[0] and not argv[0].startswith("-") and argv[0].split("=", 1)[0].replace("_", "a").isalnum():
            argv = argv[1:]
        elif w in WRAPPERS:
            argv = argv[1:]
            while argv and (argv[0].startswith("-") or argv[0].lstrip("-").isdigit() or
                            ("=" in argv[0] and not argv[0].startswith("-"))):
                argv = argv[1:]
        else:
            break
    return argv


def is_prefix_of_no_verify(a):
    return a.startswith("--no-v") and "--no-verify".startswith(a)


def check_commit(args):
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--":
            return
        if a.startswith("--"):
            if is_prefix_of_no_verify(a):
                refuse("git commit --no-verify")
            if a in COMMIT_VALUE_LONG:
                i += 1
        elif a.startswith("-") and len(a) > 1:
            for k, c in enumerate(a[1:]):
                if c == "n":
                    refuse("git commit -n (that is --no-verify)")
                if c in COMMIT_VALUE_SHORT:
                    if k == len(a) - 2:
                        i += 1       # the value is the next word
                    break
        i += 1


def check_push(args):
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--":
            return
        if a.startswith("--"):
            if is_prefix_of_no_verify(a):
                refuse("git push --no-verify")
            if a in PUSH_VALUE_LONG:
                i += 1
        i += 1


def hooks_key(s):
    return s.split("=", 1)[0].strip().lower() == "core.hookspath"


def check_git(args):
    i = 0
    while i < len(args):
        a = args[i]
        if a == "-c":
            if i + 1 < len(args) and hooks_key(args[i + 1]):
                refuse("git -c core.hooksPath")
            i += 2
        elif a.startswith("-c") and len(a) > 2 and hooks_key(a[2:]):
            refuse("git -c core.hooksPath")
        elif a.startswith("--config-env"):
            val = a.split("=", 1)[1] if "=" in a else (args[i + 1] if i + 1 < len(args) else "")
            if hooks_key(val):
                refuse("git --config-env core.hooksPath")
            i += 1 if "=" in a else 2
        elif a in GIT_VALUE_OPTS:
            i += 2
        elif a.startswith("-"):
            i += 1
        else:
            break
    if i >= len(args):
        return
    sub, rest = args[i], args[i + 1:]
    if sub == "commit":
        check_commit(rest)
    elif sub == "push":
        check_push(rest)
    elif sub == "config":
        reading = any(x in ("--get", "--get-all", "--get-regexp", "--list", "-l") for x in rest)
        if not reading and any(hooks_key(x) for x in rest if not x.startswith("-")):
            refuse("git config core.hooksPath")


def protected(path):
    p = posixpath.normpath(path)
    parts = p.split("/")
    if ".git" not in parts:
        return False
    after = parts[parts.index(".git") + 1:]
    if not after:
        return False
    if after[0] == "hooks":
        return True
    last = after[-1]
    if last == "index.lock":
        return True
    return any(c in last for c in "*?[") and fnmatch.fnmatch("index.lock", last)


def check_command(argv, depth=0):
    argv = strip_wrappers(argv)
    if not argv:
        return
    name = posixpath.basename(argv[0])
    if name == "git":
        check_git(argv[1:])
    elif name in ("rm", "chmod", "mv", "unlink"):
        for a in argv[1:]:
            if not a.startswith("-") and protected(a):
                refuse("%s of %s" % (name, a))
    elif name in SHELLS and depth < 4:
        for j, a in enumerate(argv[1:-1], 1):
            if a.startswith("-") and not a.startswith("--") and "c" in a:
                check_string(argv[j + 1], depth + 1)
                break


def check_string(cmd, depth=0):
    try:
        cmds = split_commands(cmd)
    except ValueError:
        return          # unbalanced quotes: the shell will not run it either
    for argv in cmds:
        check_command(argv, depth)


def main():
    try:
        data = json.load(sys.stdin)
        cmd = data["tool_input"]["command"]
    except (ValueError, KeyError, TypeError):
        return 0
    if isinstance(cmd, str):
        check_string(cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
