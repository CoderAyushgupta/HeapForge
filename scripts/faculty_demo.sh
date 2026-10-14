#!/usr/bin/env bash
# faculty_demo.sh - one-command live demonstration (about 2-4 minutes with pauses).
# On a terminal it waits for Enter between stages so you can explain each one.
cd "$(dirname "$0")/.." || exit 1
make -s >/dev/null || { echo "build failed - run ./scripts/setup.sh"; exit 1; }
if [ -t 0 ] && [ -t 1 ]; then
    exec ./bin/heapforge demo --step
else
    exec ./bin/heapforge demo
fi
