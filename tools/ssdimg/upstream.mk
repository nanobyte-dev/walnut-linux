# Upstream pins of tools/ssdimg/ (included by the Makefile after config.mk; a change here is a change to the
# package, so its version goes up). Downloads are checked against these sha256s.
# --- The SSD root's initramfs (tools/ssdimg/build-initrd.sh, docs/GAMEOS-HDD.md "Phase B") -----------
# Debian's busybox-static, pinned to its own snapshot and sha256: ppc64 for the PS3 (debian-ports),
# amd64 for the KVM test (tools/ssdimg/vm-e2e.sh); the same version and configuration
BUSYBOX_SNAPSHOT    := 20261013T000000Z
BUSYBOX_PPC64_DEB   := busybox-static_1.38.0-3_ppc64.deb
BUSYBOX_PPC64_URL   := http://snapshot.debian.org/archive/debian-ports/$(BUSYBOX_SNAPSHOT)/pool-ppc64/main/b/busybox/$(BUSYBOX_PPC64_DEB)
BUSYBOX_PPC64_SHA256 := cd762eee99372ac2bfa0d0c32cc981a76d35cf3debb6b7c66a3b53b5f7d8cc30
BUSYBOX_AMD64_DEB   := busybox-static_1.38.0-3+b1_amd64.deb
BUSYBOX_AMD64_URL   := http://snapshot.debian.org/archive/debian/$(BUSYBOX_SNAPSHOT)/pool/main/b/busybox/$(BUSYBOX_AMD64_DEB)
BUSYBOX_AMD64_SHA256 := 26801f17e6c88e813be104effc0ea3b43d912bd59ca6295fcd1260528ebb4d41
# The generic initramfs's install tools (tools/ssdimg/build-initrd-tools.sh): static mke2fs and GNU tar
# from upstream, both checked against their maintainers' GPG signatures when pinned (2026-10-21)
E2FSPROGS_TARBALL   := e2fsprogs-1.47.4.tar.xz
E2FSPROGS_URL       := https://mirrors.edge.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/v1.47.4/$(E2FSPROGS_TARBALL)
E2FSPROGS_SHA256    := fd5bf388cbdbe006a3d3b318d983b2948382440acc85a87f1e7d108653e8db0b
GNUTAR_TARBALL      := tar-1.35.tar.xz
GNUTAR_URL          := https://ftp.gnu.org/gnu/tar/$(GNUTAR_TARBALL)
GNUTAR_SHA256       := 4d62ff37342ec7aed748535323930c7cf94acf71c3591882b26a7ea50f3edc16
