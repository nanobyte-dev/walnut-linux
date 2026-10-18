#!/bin/sh
# Nexuiz Classic (Debian's nexuiz-data in /usr/share/games/nexuiz) on the GL 1.x/2.0 darkplaces.
# Lean settings for the PS3 (about 217 MB RAM) come from nexuiz-gl2.cfg through +exec; the
# player's own arguments follow and win.
exec /usr/games/darkplaces-gl2 -nexuiz -basedir /usr/share/games/nexuiz +exec /usr/share/darkplaces-gl2/nexuiz-lean.cfg "$@"
