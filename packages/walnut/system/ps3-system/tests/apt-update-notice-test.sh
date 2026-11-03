#!/bin/bash
# ps3-system's apt update notice (files/libexec/apt-update-notice): a fixture lists dir, a sources file and a
# fake dpkg-query/dpkg (numeric compare of the upstream part): updates, none, garbage stanzas, a missing
# list, a disabled suite, a failing dpkg-query, and that debian/rules installs the script and the drop-in.
set -u
cd "$(dirname "$0")/.." || exit 2
S=files/libexec/apt-update-notice
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/lists" "$T/bin"
cat > "$T/sources" <<'X'
Types: deb
URIs: https://walnut.nanobyte.dev/apt
Suites: stable
Components: main

Types: deb
URIs: https://walnut.nanobyte.dev/apt
Suites: testing
Enabled: no
X
cat > "$T/bin/dpkg-query" <<'X'
#!/bin/sh
[ -n "${FAIL:-}" ] && exit 1
printf 'ps3-system\t1.0+ps3.1\tii \nps3rsx-modules-ps3\t0.5\tii \nps3-desktop\t2.0\tii \nsame\t3.0\tii \nremoved\t1.0\trc \nnotwalnut\t1.0\tii \nNOT_VALID\t1.0\tii \n'
X
cat > "$T/bin/dpkg" <<'X'
#!/bin/sh
# dpkg --compare-versions A gt B, by the first number only
a=${2%%[!0-9]*}; b=${4%%[!0-9]*}; [ "${a:-0}" -gt "${b:-0}" ]
X
chmod +x "$T/bin/"*
L=$T/lists/walnut.nanobyte.dev_apt_dists_stable_main_binary-powerpc_Packages
cat > "$L" <<'X'
Package: ps3rsx-modules-ps3
Version: 6.0

Package: ps3-system
Version: 2.0+ps3.1

garbage line without colon
Package: ps3-desktop
Version: -rf $(touch /tmp/x)

Package: ps3-desktop
Version: 3.0
Description: x

Package: same
Version: 3.0

Package: removed
Version: 9.0

Package: Bad;Name
Version: 9.0

Package: NOT_VALID
Version: 9.0

Package: nover

Package: notinlist-new
Version: 9.0
X
printf 'Package: ps3-desktop\nVersion: 99\n\n' > "$T/lists/walnut.nanobyte.dev_apt_dists_testing_main_binary-powerpc_Packages"
fails=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }
run() { python3 -I "$S" "$T/lists" "$T/sources" "$T/bin"; }
out=$(run); rc=$?
check "updates named, one line" '[ "$out" = "Walnut: 3 updates (ps3rsx-modules-ps3, ps3-system, ps3-desktop). Install with: sudo apt full-upgrade (from a text console, Ctrl+Alt+F2, if the desktop is short on memory)." ]'
check "exit 0" '[ $rc = 0 ]'
check "disabled testing suite ignored (ps3-desktop 3.0 not 99)" '! grep -q testing "$T/sources" || [ "$(run | wc -l)" = 1 ]'
check "nothing written" '[ ! -e /tmp/x ] || ! [ /tmp/x -nt "$T/sources" ]'
printf 'Package: ps3-system\nVersion: 1.0\n\n' > "$L"
out=$(run); rc=$?
check "no updates: no output, exit 0" '[ -z "$out" ] && [ $rc = 0 ]'
printf 'Package: ps3-system\nVersion: 2.0\n\n' > "$L"
out=$(run)
check "singular" '[[ "$out" == "Walnut: 1 update (ps3-system). Install"* ]]'
printf '\0\0\xff\xfe garbage\n:\n\n\nPackage:\nVersion:\n\n' > "$L"
out=$(run); rc=$?
check "binary garbage: no output, exit 0" '[ -z "$out" ] && [ $rc = 0 ]'
rm -f "$L"
out=$(run); rc=$?
check "missing list: no output, exit 0" '[ -z "$out" ] && [ $rc = 0 ]'
printf 'Package: ps3-system\nVersion: 2.0\n\n' > "$L"
out=$(FAIL=1 run); rc=$?
check "failing dpkg-query: no output, exit 0" '[ -z "$out" ] && [ $rc = 0 ]'
out=$(python3 -I "$S" "$T/nolists" "$T/nosources" "$T/bin"); rc=$?
check "missing sources: no output, exit 0" '[ -z "$out" ] && [ $rc = 0 ]'
for i in 1 2 3 4 5 6 7 8 9 10 11; do printf 'Package: p%s\nVersion: 2\n\n' $i; done > "$L"
cat > "$T/bin/dpkg-query" <<'X'
#!/bin/sh
for i in 1 2 3 4 5 6 7 8 9 10 11; do printf 'p%s\t1\tii \n' $i; done
X
out=$(run)
check "more than 8: first 8 and a count" '[[ "$out" == *"11 updates (p1, p2, p3, p4, p5, p6, p7, p8, and 3 more)."* ]]'
check "debian/rules installs the script and the drop-in" 'grep -q "files/libexec/apt-update-notice .*usr/libexec/walnut/apt-update-notice" debian/rules && grep -q "files/apt/99walnut-update-notice .*etc/apt/apt.conf.d/99walnut-update-notice" debian/rules'
check "drop-in is Post-Invoke-Success with || true" 'grep -q "Post-Invoke-Success" files/apt/99walnut-update-notice && grep -q "|| true" files/apt/99walnut-update-notice'
[ $fails -eq 0 ] && echo "apt-update-notice: all passed"
exit $((fails > 0))
