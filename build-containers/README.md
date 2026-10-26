# build-containers/: the Dockerfiles of the builders (toolchain and rootfs, the deb builder, the SPU toolchain, games data, the PC references).
The Makefile tags each image by a hash of its Dockerfile, so editing one rebuilds that image; Dockerfile.deb also counts in every package's version.
Touch it to change a build environment (docs/PACKAGING.md "Base and toolchain").
