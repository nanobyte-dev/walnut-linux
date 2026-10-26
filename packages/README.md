# packages/: every Debian source package we build, one directory each, named like the package (`make <name>-deb`).
`walnut/` is our own code, `patched/` is Debian's or upstream's source with our patches, `repackaged/` is upstream releases we package unchanged, `licensed/` is game data we may not redistribute (built here only, never public).
Touch it to change what goes on the PS3; how a package is built is in docs/PACKAGING.md, the layout in docs/LAYOUT.md.
