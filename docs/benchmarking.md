# Benchmarking

Run: `./heapforge benchmark [--quick] [--runs N] [--scale X] [--threads N] [--no-libc] [--csv file]`
or `scripts/benchmark.sh` (writes `results/benchmark.csv`).

## What is measured
* Timer: `clock_gettime(CLOCK_MONOTONIC)` around the allocator calls of the main phase only.
  Array setup, snapshots and final clean-up are excluded.
* Isolation: every (workload, allocator, run) executes in a fresh child process
  (`fork` + `execv /proc/self/exe --bench-child`), so each run starts with an empty heap.
  The reported row is the **median** of N runs (default 3) by elapsed time.
* Heap / fragmentation columns are read with `hf_stats_get` right after the main phase
  (live state): `heap(KiB)` = bytes obtained through sbrk and managed; `extFrag%` =
  `1 - largest_free/free_bytes`; `freeBlks` = number of free blocks. For glibc they are `n/a`.
* `fail` = allocation calls that returned NULL; `integ` = data-integrity errors (multi-threaded
  workload only).
* HeapForge runs with poisoning off in benchmarks (we measure allocation, not debugging).

## Workloads (scale 1.0)
| Name | Description |
|---|---|
| small | 60,000 x 32 B allocations, then free all in order |
| mixed | 20,000 random sizes (16..4096, skewed small), free a random half, 5,000 more allocs, free rest |
| churn | 200,000 random alloc/free operations over 1,024 slots, sizes 16..2048 |
| frag | 4,000 pairs of 1024 B / 48 B, free the 1024 B ones, carve holes with 100..900 B then 32..128 B requests |
| threads | N threads (default 4), 100,000 total mixed alloc/free/realloc ops with data-integrity checking |

## Reading the results honestly
* glibc malloc is a heavily optimised production allocator (size-class bins, per-thread arenas,
  tcache). HeapForge uses one linear free list and one lock; expect glibc to be faster and to
  scale better. HeapForge's value is transparency, diagnostics and experimentation.
* first-fit vs best-fit vs worst-fit: compare `time`, `heap(KiB)`, `extFrag%`, `freeBlks`.
  Best-fit and worst-fit scan the whole free list; worst-fit tends to shatter the large block.
* Results depend on the machine, number of cores and run-to-run noise; do not quote numbers
  from another machine. Put your own `results/benchmark.csv` and terminal screenshot in the report.
