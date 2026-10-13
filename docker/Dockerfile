# Build environment for kernel and rootfs. Base image is pinned by digest and apt is
# pointed at snapshot.debian.org, so the toolchain doesn't drift between builds.
ARG BASE
FROM ${BASE}
ARG SNAPSHOT

RUN rm -f /etc/apt/sources.list.d/debian.sources && \
    echo "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/${SNAPSHOT} stable main" > /etc/apt/sources.list && \
    echo "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/${SNAPSHOT} stable-security main" >> /etc/apt/sources.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        gcc-powerpc64-linux-gnu binutils-powerpc64-linux-gnu libc6-dev-ppc64-cross \
        gcc libc6-dev make bc bison flex libssl-dev libelf-dev kmod cpio \
        python3 perl xz-utils rsync patch file ca-certificates curl \
        mmdebstrap arch-test gpg debian-ports-archive-keyring && \
    rm -rf /var/lib/apt/lists/*
