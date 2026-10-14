# OS concepts and where the code uses them

| Concept | Where it is demonstrated | How to see it |
|---|---|---|
| **Process address space** | `heap.c` obtains memory in two regions of the address space: the `[heap]` segment (brk) and separate anonymous VMAs | `procmap` command, demo stage 5: prints the `[heap]` and mmap lines from `/proc/self/maps` |
| **Virtual memory / demand paging** | `sbrk`/`mmap` only reserve virtual address space; physical pages are assigned when first touched (page fault). The first `memset` of a fresh block triggers it | benchmark times include this first-touch cost |
| **Heap** | `struct hf_heap`, physical block list `head..tail` in `heap.h` | `map`, `inspect` |
| **Dynamic memory allocation** | `alloc_locked` / `free_locked` in `allocator.c` | `malloc`, `free`, demo stages 1-4 |
| **System calls** | `sbrk` (`hf_heap_grow`, `hf_heap_trim`), `mmap`/`mremap`/`munmap` (`hf_heap_map_block`, `remap`, `unmap`), `fork`/`execv`/`pipe`/`waitpid`/`read`/`write` (`benchmark.c`), `clock_gettime` (timing) | `strace -f -e trace=brk,mmap,munmap,mremap ./heapforge -c "malloc 1k; malloc 300k"` (external tool) |
| **brk vs mmap** | `size >= mmap_threshold` selects the path; counters `sbrk_calls`, `mmap_calls`, `munmap_calls`, `mremap_calls` | `stats`, `threshold 1k`, `malloc 2k` |
| **Fragmentation** | internal: rounding + canary + unsplit remainders; external: holes between live blocks. Metrics in `stats.c` | `stats`, demo stage 2, `examples/fragmentation.c`, benchmark `frag` |
| **Resource management** | `hf_trim()` gives the top of the heap back (`sbrk(-n)`); `munmap` returns large blocks immediately; `reset` | `trim`, `procmap` before/after |
| **Synchronization / mutex** | `pthread_mutex_t lock` in `struct hf_heap`; `hf_heap_lock` uses `trylock` first to count contention | `stress`, `stats` (lock lines) |
| **Concurrency / race conditions** | 8-thread stress with per-block data patterns; barrier test in `tests/test_threads.c` | `stress --threads 8 --operations 100000` |
| **Memory safety** | canary, magic, pointer validation, poison, checker | `overflow`, `free` twice, demo stage 7 |
| **Processes** | benchmark isolates every measurement in a child process created by `fork` + `execv` | `benchmark` |
| **Pipes / IPC** | child sends its result struct to the parent via `pipe()` | `benchmark.c: run_isolated` |

## Notes for viva
* *Why can't `sbrk` memory be returned from the middle?* The break is a single pointer; only the top can move.
* *Why does HeapForge check adjacency by address?* libc's malloc also moves the break, so two of our regions may be separated by foreign memory.
* *Why is a freed `mmap` block unmapped at once?* It is its own VMA; `munmap` removes it, so the kernel reclaims the pages.
* *What does the mutex protect?* All allocator state: lists, counters, headers. Without it two threads could take the same free block.
* *Why not LD_PRELOAD by default?* The project must run side by side with libc malloc; `stdio` itself uses glibc's malloc inside HeapForge's diagnostics.
