#!/bin/sh
# Starts ClassiCube from its own directory (it keeps options.txt, maps and sounds there)
cd "$(dirname "$0")" && exec ./ClassiCube "$@"
