#!/bin/bash
# install.sh [--check]: make hooks. Writes .git/hooks/pre-commit and .git/hooks/commit-msg, small stubs that run
# tools/hooks/<name> of the checkout committing (and refuse the commit when that file is missing). No
# core.hooksPath; Git LFS's hooks (post-checkout, post-commit, post-merge, pre-push) are left alone. A hook
# that is there and isn't our stub is not overwritten. --check (make test's commit-hooks suite): exit 1
# unless both stubs are installed as this script writes them.
set -euo pipefail
cd "$(dirname "$0")/../.."
dir=$(git rev-parse --git-path hooks)
[ "$(git config --get core.hooksPath || true)" = "" ] ||
	{ echo "!! core.hooksPath is set ($(git config --get core.hooksPath)): git would not run $dir; unset it" >&2; exit 1; }
stub() {	# the stub for hook $1
	cat <<STUB
#!/bin/sh
# Installed by make hooks (tools/hooks/install.sh): runs tools/hooks/$1 of the checkout. Don't edit.
h="\$(git rev-parse --show-toplevel)/tools/hooks/$1"
[ -x "\$h" ] || { echo "commit-tests: \$h is missing: the commit tests can't run, so nothing is committed (make hooks)" >&2; exit 1; }
exec "\$h" "\$@"
STUB
}
bad=0
for h in pre-commit commit-msg; do
	f=$dir/$h
	if [ "${1:-}" = --check ]; then
		if [ ! -x "$f" ] || [ "$(cat "$f")" != "$(stub "$h")" ]; then
			echo "!! $f is not the commit-tests stub: make hooks"; bad=1
		fi
		continue
	fi
	if [ -e "$f" ] && ! grep -q 'Installed by make hooks' "$f"; then
		echo "!! $f exists and isn't ours: left alone (move it away, then make hooks)" >&2; bad=1; continue
	fi
	stub "$h" > "$f.tmp" && chmod 755 "$f.tmp" && mv "$f.tmp" "$f"
	echo ">> $f -> tools/hooks/$h"
done
[ "${1:-}" != --check ] || [ $bad = 1 ] || echo "commit hooks installed: pre-commit, commit-msg"
exit $bad
