#!/usr/bin/env bash
# debug_gdb.sh - start HeapForge's debug build under GDB with helper commands.
# Needs:  sudo apt install gdb
# Inside gdb try:  run   then  (Ctrl-C)  hfheap / hffree / hfblock <ptr>
# Or break on a function:  break hf_free   /  break hf_block_release
cd "$(dirname "$0")/.." || exit 1
command -v gdb >/dev/null || { echo "gdb not installed: sudo apt install gdb"; exit 1; }
make -s MODE=debug all || exit 1
exec gdb -q -x scripts/heapforge.gdb --args ./bin/heapforge-debug "$@"
