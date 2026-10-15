#!/bin/bash
# Xorg (modesetting + glamor) on ps3rsx with the patched Mesa, running one client.
# Usage (as tibi, over SSH): rsx-x.sh [seconds] [client args...]   default: 30 glxgears -info
# Runs Xorg as root with the Mesa environment (the setuid wrapper would drop it).
# Log: /var/log/Xorg.1.log; client output on stdout. RSX_X_ENV="VAR=value ..." adds environment.
secs=${1:-30}; shift
[ $# -gt 0 ] || set -- glxgears -info
. /usr/lib/ps3-mesa/env.sh
lsmod | grep -q '^ps3rsx' || sudo modprobe ps3rsx
exec sudo -E env PATH="$PATH" LD_LIBRARY_PATH="$LD_LIBRARY_PATH" LIBGL_DRIVERS_PATH="$LIBGL_DRIVERS_PATH" \
	GBM_BACKENDS_PATH="$GBM_BACKENDS_PATH" __EGL_VENDOR_LIBRARY_FILENAMES="$__EGL_VENDOR_LIBRARY_FILENAMES" \
	__GLX_VENDOR_LIBRARY_NAME=mesa MESA_SHADER_CACHE_DISABLE=true ${RSX_X_ENV} \
	timeout "$secs" xinit /usr/bin/env "$@" -- /usr/lib/xorg/Xorg :1 vt7 -nolisten tcp </dev/null
