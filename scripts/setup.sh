#!/usr/bin/env bash
# setup.sh - verify prerequisites. Read-only: installs nothing, changes nothing.
set -u
cd "$(dirname "$0")/.." || exit 1

ok=1
say() { printf '%s\n' "$*"; }

say "HeapForge setup check"
say "---------------------"
if [ "$(uname -s)" != "Linux" ]; then
    say "ERROR: HeapForge needs Linux (sbrk/mmap/mremap, /proc). Found: $(uname -s)"; exit 1
fi
if [ -r /etc/os-release ]; then . /etc/os-release; say "OS        : ${PRETTY_NAME:-unknown}"; fi
if grep -qi microsoft /proc/version 2>/dev/null; then say "Environment: WSL detected (supported)"; fi
say "Kernel    : $(uname -r)  arch: $(uname -m)"

missing=()
need() { # command, apt-package
    if command -v "$1" >/dev/null 2>&1; then say "[ok] $1 ($($1 --version 2>&1 | head -1))"
    else say "[MISSING] $1"; missing+=("$2"); ok=0; fi
}
need gcc build-essential
need make build-essential
need ar build-essential

if echo '#include <pthread.h>' | gcc -E -x c - >/dev/null 2>&1; then say "[ok] pthread.h"
else say "[MISSING] pthread.h (libc dev headers)"; missing+=(libc6-dev); ok=0; fi

say ""
say "Optional (debugging aids, not needed to build or run):"
for t in gdb valgrind zip; do
    if command -v $t >/dev/null 2>&1; then say "[ok] $t"; else say "[not installed] $t"; opt+=("$t"); fi
done

chmod +x scripts/*.sh 2>/dev/null

say ""
if [ $ok -eq 1 ]; then
    say "All required tools are present. Next:  make && ./heapforge --help"
    [ ${#opt[@]} -gt 0 ] 2>/dev/null && say "Optional install:  sudo apt update && sudo apt install -y ${opt[*]}"
    exit 0
fi
say "Required tools are missing. Run this yourself (this script never uses sudo):"
say "    sudo apt update && sudo apt install -y $(printf '%s\n' "${missing[@]}" | sort -u | tr '\n' ' ')"
exit 1
