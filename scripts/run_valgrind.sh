#!/usr/bin/env bash
# run_valgrind.sh - check the project with Valgrind (an EXTERNAL tool; it is
# not part of HeapForge). Needs:  sudo apt install valgrind
# Valgrind watches glibc's malloc; HeapForge's own heap lives in sbrk/mmap
# memory, so here Valgrind verifies that the PROGRAM (CLI, tests) has no
# invalid reads/writes, uninitialised-value use or libc-heap leaks.
cd "$(dirname "$0")/.." || exit 1
command -v valgrind >/dev/null || { echo "valgrind not installed: sudo apt install valgrind"; exit 1; }
make -s MODE=debug all >/dev/null && make -s MODE=debug bin/tests/debug/test_free bin/tests/debug/test_realloc >/dev/null || exit 1
VG="valgrind --error-exitcode=9 --leak-check=full --show-leak-kinds=definite,indirect -q"
echo "== CLI session =="
$VG ./bin/heapforge-debug --no-color -c "malloc 256; malloc 300k; calloc 10 10; realloc #1 1k; free #1; free #2; free #3; check; leaks" || exit 1
echo "== test_free =="
$VG ./bin/tests/debug/test_free || exit 1
echo "== test_realloc =="
$VG ./bin/tests/debug/test_realloc || exit 1
echo "Valgrind: no errors reported"
