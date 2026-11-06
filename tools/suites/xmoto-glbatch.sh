#!/bin/bash
# Suite xmoto-glbatch: X-Moto's GLBatch against the patched source tree (make xmoto-work).
set -u
cd "$(dirname "$0")/../.."
. tools/suites/lib.sh
[ -e build/work/xmoto/src/drawlib/GLBatch.cpp ] || { echo "skip: no build/work/xmoto (make xmoto-work)"; exit 77; }
need g++
exec "$(tools/pkg-dir.sh xmoto)/test/glbatch-test.sh" 100
