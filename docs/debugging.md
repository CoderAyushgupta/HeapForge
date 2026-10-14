# Debugging

## Built into HeapForge (project features)
| Feature | Command / API | Inspects |
|---|---|---|
| Canary corruption report | automatic on `free`/`realloc`; `check` | 8 bytes after the requested size vs `HF_CANARY_BASE ^ block address` |
| Double free | `free` twice | header magic `FREE`, or `DEAD` when the header was merged away |
| Invalid free | `free 0x1234`, `free <interior ptr>` | alignment, heap bounds, magic, link cross-check |
| Heap consistency checker | `check`, `hf_check_heap()` | invariants I1-I8 in `docs/design.md` |
| Leak report | `leaks`, `hf_leak_report()` | live blocks (with `file:line` for `HF_MALLOC` macros) |
| Poison on free | default on | freed payload bytes are 0xDD |
| Paranoid mode | `paranoid on`, `hf_set_paranoid(1)` | full check after every operation; aborts on violation |
| Fault injection | `overflow #id N` | deliberately overwrites past a block's end (bounded) |

HeapForge does **not** claim to find the source line of an overflow: it only knows where the
block was allocated (when the macro is used) and which guard changed.

## External tools (not part of HeapForge)
**GDB** - `scripts/debug_gdb.sh` (needs `sudo apt install gdb`) loads `scripts/heapforge.gdb`:
`hfblock <ptr>` prints a block header, `hfheap` prints the allocator state, `hffree` walks the free
list, and a breakpoint on `hf_heap_grow` reports each `sbrk`. Typical session:
```
./scripts/debug_gdb.sh
(gdb) break hf_block_release
(gdb) run -c "malloc 100; malloc 100; free #1; free #2"
(gdb) bt
(gdb) print *blk
(gdb) hffree
```
**Valgrind** - `scripts/run_valgrind.sh` (needs `sudo apt install valgrind`). Valgrind tracks libc's
heap; HeapForge's blocks live in `sbrk`/`mmap` memory, so Valgrind validates the *program* around
the allocator (CLI, tests), not HeapForge's block bookkeeping.
**AddressSanitizer / UBSan** - `make asan` rebuilds everything with `-fsanitize=address,undefined`
and runs the tests; it already caught a pointer-arithmetic underflow in `hf_validate_ptr` during
development.
**strace** - `strace -f -e trace=brk,mmap,munmap,mremap ./heapforge -c "malloc 1k; malloc 300k; free #2"`
shows the real system calls behind the allocator.
