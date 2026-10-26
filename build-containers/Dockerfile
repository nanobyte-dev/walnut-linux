# Build environment for kernel and rootfs: Debian sid, pinned by digest and upgraded to the
# snapshot the PS3's root comes from, so the cross toolchain is the snapshot's (GCC 16.2,
# binutils 2.47) and doesn't drift between builds.
ARG BASE
FROM ${BASE}
ARG SNAPSHOT
# config.mk SNAPSHOT_URL: walnut.nanobyte.dev's snapshot cache (snapshot.debian.org throttles)
ARG SNAPSHOT_URL=http://snapshot.debian.org

RUN rm -f /etc/apt/sources.list.d/debian.sources && \
    printf 'Acquire::Retries "8";\n' > /etc/apt/apt.conf.d/80retries && \
    echo "deb [check-valid-until=no] ${SNAPSHOT_URL}/archive/debian/${SNAPSHOT} unstable main" > /etc/apt/sources.list && \
    apt-get update && \
    apt-get full-upgrade -y --no-install-recommends && \
    apt-get install -y --no-install-recommends \
        gcc-16-powerpc64-linux-gnu gcc-powerpc64-linux-gnu binutils-powerpc64-linux-gnu \
        libc6-dev-ppc64-cross \
        gcc libc6-dev make bc bison flex libssl-dev libelf-dev kmod cpio \
        python3 perl xz-utils rsync patch file ca-certificates curl \
        mmdebstrap arch-test gpg debian-ports-archive-keyring && \
    rm -rf /var/lib/apt/lists/*
