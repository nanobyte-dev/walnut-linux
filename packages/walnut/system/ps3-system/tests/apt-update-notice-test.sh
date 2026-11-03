#!/bin/bash
# ps3-system's apt update notice (files/libexec/apt-update-notice): a fixture lists dir, sources file, os-release and a
# fake dpkg (numeric compare of the first number): a newer release notifies; the same release with newer packages,
# a missing or garbage field, a missing os-release or list, a disabled suite are silent, exit 0; and that debian/rules installs the script and the drop-in.
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
cat > "$T/bin/dpkg" <<'X'
#!/bin/sh
# dpkg --compare-versions A gt B, by the dotted numbers: major then minor, then the first number after "ps3."
n() { echo "$1" | sed 's/[^0-9.].*//' | awk -F. '{printf "%d%06d", $1, $2}'; }
[ "$(n "$2")" -gt "$(n "$4")" ]
X
chmod +x "$T/bin/"*
L=$T/lists/walnut.nanobyte.dev_apt_dists_stable_main_binary-powerpc_Packages
printf 'NAME="Walnut"\nVERSION_ID="0.1"\n' > "$T/os-release"
fails=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }
run() { python3 -I "$S" "$T/lists" "$T/sources" "$T/bin" "$T/os-release"; }
silent() { out=$(run); rc=$?; [ -z "$out" ] && [ $rc = 0 ]; }
NOTE='Notice: A new version of walnut is available, upgrade using `apt full-upgrade`'
printf 'Package: ps3-system\nVersion: 0.1\nWalnut-Release: 0.1\n\nPackage: ps3-desktop\nVersion: 9.0\n\n' > "$L"
check "same release, newer packages: silent" 'silent'
printf 'Package: ps3-system\nVersion: 0.2\nWalnut-Release: 0.2\n\n' > "$L"
out=$(run); rc=$?
check "newer release: the notice, one line" '[ "$out" = "$NOTE" ] && [ $rc = 0 ]'
printf 'Package: ps3-system\nVersion: 0.2\nWalnut-Release: 0.1\n\nPackage: ps3-system\nVersion: 0.9\nWalnut-Release: 1.0\n\nPackage: ps3-system\nVersion: 0.5\nWalnut-Release: 0.1\n\n' > "$L"
out=$(run)
check "the highest ps3-system version decides" '[ "$out" = "$NOTE" ]'
printf 'Package: ps3-system\nVersion: 0.2\nWalnut-Release: 0.1\n\nPackage: ps3-system\nVersion: 0.1\nWalnut-Release: 9.9\n\n' > "$L"
check "an older ps3-system stanza does not count" 'silent'
printf 'Package: ps3-system\nVersion: 0.2\nWalnut-Release: 0.0.5\n\n' > "$L"
check "older release: silent" 'silent'
printf 'Package: ps3-system\nVersion: 0.2\n\n' > "$L"
check "missing field: silent" 'silent'
for g in 'x' '-1' '9;touch /tmp/x' '' '1.0 2' '$(id)'; do
	printf 'Package: ps3-system\nVersion: 0.2\nWalnut-Release: %s\n\n' "$g" > "$L"
	check "garbage field '$g': silent" 'silent'
done
printf 'Package: ps3-system\nVersion: -rf\nWalnut-Release: 5.0\n\n' > "$L"
check "garbage version: silent" 'silent'
printf 'Package: ps3-system\nVersion: 0.2\nWalnut-Release: 0.2\n\n' > "$L"
for v in 'VERSION_ID="x;y"' 'VERSION_ID=' 'NAME=x'; do
	printf '%s\n' "$v" > "$T/os-release"
	check "os-release '$v': silent" 'silent'
done
rm -f "$T/os-release"
check "missing os-release: silent" 'silent'
printf 'VERSION_ID="0.1"\n' > "$T/os-release"
printf 'Package: ps3-system\nVersion: 0.2\nWalnut-Release: 0.2\n\n' > "$T/lists/walnut.nanobyte.dev_apt_dists_testing_main_binary-powerpc_Packages"
rm -f "$L"
check "newer release only in a disabled suite: silent" 'silent'
printf '\0\0\xff\xfe garbage\n:\n\n\nPackage:\nVersion:\n\n' > "$L"
check "binary garbage: silent" 'silent'
printf 'Package: ps3-system\nVersion: 0.2\nWalnut-Release: 0.2\n\n' > "$L"
out=$(python3 -I "$S" "$T/nolists" "$T/nosources" "$T/bin" "$T/os-release"); rc=$?
check "missing sources: silent, exit 0" '[ -z "$out" ] && [ $rc = 0 ]'
head -c 3000000 /dev/zero | tr '\0' 'a' > "$T/lists/walnut.nanobyte.dev_apt_dists_stable_main_binary-ppc64_Packages"
check "a huge line without a newline: still ends, exit 0" 'run >/dev/null; [ $? = 0 ]'
check "debian/rules installs the script and the drop-in" 'grep -q "files/libexec/apt-update-notice .*usr/libexec/walnut/apt-update-notice" debian/rules && grep -q "files/apt/99walnut-update-notice .*etc/apt/apt.conf.d/99walnut-update-notice" debian/rules'
check "drop-in is Post-Invoke-Success, tolerates a removed script, || true" 'grep -q "Post-Invoke-Success" files/apt/99walnut-update-notice && grep -q "\[ ! -x /usr/libexec/walnut/apt-update-notice \] || " files/apt/99walnut-update-notice && grep -q "|| true" files/apt/99walnut-update-notice'
check "control carries Walnut-Release from VERSION" 'grep -q "^XB-Walnut-Release: \${Walnut:Release}" debian/control && grep -q "dh_gencontrol -- -VWalnut:Release=\$(VER)" debian/rules'
[ $fails -eq 0 ] && echo "apt-update-notice: all passed"
exit $((fails > 0))
