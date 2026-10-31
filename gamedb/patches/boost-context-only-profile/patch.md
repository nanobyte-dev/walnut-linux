+++
component = "boost-context"
patches = ["packaging/0002-contextonly-profile.patch"]
aliases = ["boost-context packaging/0002"]
category = "build"
switch = "pkg.boost.contextonly (build profile)"
default = "on in package.conf"
forwarded = "no"
tests = []
batches = []
+++
# Boost: the build profile pkg.boost.contextonly

Builds only Boost.Context, Boost.Coroutine and Boost.Fiber (the libraries whose code depends on the
Boost.Context implementation) and their -dev packages; everything else, and the build dependencies only the
other libraries and the documentation need, is excluded with Build-Profiles. The -dev packages depend on the
archive's headers at the version ours is based on, and the shlibs ask for our build, so a program linked
against the fcontext library never gets the archive's ucontext one.
