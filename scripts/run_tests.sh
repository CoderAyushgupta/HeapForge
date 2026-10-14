#!/usr/bin/env bash
# run_tests.sh - build and run every unit test (release build).
# Usage: scripts/run_tests.sh [asan]
cd "$(dirname "$0")/.." || exit 1
if [ "${1:-}" = "asan" ]; then exec make asan; fi
exec make test
