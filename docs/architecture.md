# HeapForge architecture

## Layers
```
CLI / Demo (cli.c demo.c main.c)
   |  calls only the public API and the read-only views below
   +--> Allocation Engine  allocator.c   hf_malloc/free/calloc/realloc, configuration, lock
   +--> Diagnostics        debug.c       hf_check_heap, hf_leak_report, hf_diag_*
   +--> Analytics          stats.c visualizer.c benchmark.c
          |
   Block Manager     block.c      header/canary, split, absorb/release (coalesce), hf_validate_ptr
          |
   Free List Manager free_list.c  hf_fl_insert / hf_fl_remove / hf_fl_find (strategy)
          |
   Heap Manager      heap.c       hf_heap_grow (sbrk), hf_heap_trim, map/remap/unmap (mmap), reset, lock
          |
   Linux kernel: brk/sbrk, mmap, mremap, munmap
```
Rules of the layering: a module only calls modules *below* it (except `debug.c`/`stats.c`, which
read the lists the lower layers maintain). Only `heap.c` makes memory-management system calls.

## Global state
`struct hf_heap` (heap.h) holds the physical block list (`head`/`tail`), the free list
(`free_head`), the mmap list (`mmap_head`), heap bounds, configuration and counters, plus the
single mutex. It is one object on purpose: a small allocator with a single owner of its state is
easier to reason about and to verify.

## Threads
Every public function locks the mutex, does its work through `*_locked` helpers and unlocks.
Read-only views (`hf_stats_get`, snapshots, the checker) lock too, so they observe a consistent
heap. Diagnostics print while locked; they use stdio (glibc's own malloc, not HeapForge), so
there is no re-entrancy.

## Data flow of `hf_malloc(n)`
`hf_malloc_dbg` -> lock -> `alloc_locked` -> `hf_payload_for_request` -> (mmap path: `hf_heap_map_block`)
or (`hf_fl_find` -> [`hf_heap_grow` -> `hf_fl_find`] -> `hf_fl_remove` -> `hf_block_split` ->
`hf_block_release(remainder)`) -> header + canary -> counters -> unlock.

## Data flow of `hf_free(p)`
lock -> `hf_validate_ptr` (OK / INVALID / DOUBLE_FREE / STALE) -> canary check -> counters ->
mmap block: `munmap`; heap block: poison, `hf_block_release` (merge next, merge previous, insert
into free list) -> unlock.

## Benchmark process model
`hf_benchmark_run` -> for each (workload, allocator, run): `fork` -> child `execv("/proc/self/exe",
"--bench-child ...")` -> child runs the workload and writes a result struct to a pipe -> parent
`read` + `waitpid`. Fresh address space per measurement.
