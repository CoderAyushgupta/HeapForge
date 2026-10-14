#!/usr/bin/env bash
# benchmark.sh - run the full benchmark and store a CSV for your report.
# Usage: scripts/benchmark.sh [extra benchmark options, e.g. --runs 5 --scale 2]
cd "$(dirname "$0")/.." || exit 1
make -s >/dev/null || exit 1
mkdir -p results
echo "Machine: $(uname -srm), $(nproc) CPU(s) visible"
./bin/heapforge benchmark --csv results/benchmark.csv "$@"
