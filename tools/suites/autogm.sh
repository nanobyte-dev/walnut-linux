#!/bin/bash
# Suite autogm (slow): ps3-desktop's automatic game mode on a virtual X server.
set -u
cd "$(dirname "$0")/../.."
. tools/suites/lib.sh
if ! { command -v Xvfb >/dev/null && command -v openbox >/dev/null; } &&
   ! command -v docker >/dev/null && ! command -v podman >/dev/null; then
	echo "skip: needs Xvfb+openbox or docker/podman"; exit 77; fi
exec packages/walnut/system/ps3-desktop/tests/autogm-check.sh
