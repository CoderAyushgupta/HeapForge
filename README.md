# HeapForge

**HeapForge: An Adaptive, Thread-Safe, Self-Diagnosing Linux Memory Allocator & Heap Observatory**

A user-space `malloc`/`free` implementation in C for Linux (OS & System Programming capstone,
approved use case **#6 - HeapForge, a memory allocator**; pillars **Memory + Concurrency**).
It manages its own heap with `sbrk()`/`mmap()`, never touches glibc's `malloc`, and can show
you - from real block metadata - what the heap looks like at any moment.

```
 $ make && ./heapforge demo        # the 10-stage live demonstration
```

## 1. Project overview
HeapForge allocates memory with `hf_malloc / hf_free / hf_calloc / hf_realloc`. Inside it keeps
a header in front of every block, a free list, splits big free blocks, merges adjacent free
blocks, grows the process heap with `sbrk()`, gives huge requests their own `mmap()` mapping,
protects all state with a `pthread` mutex, and detects double frees, invalid frees and buffer
overruns. A terminal "Heap Observatory" draws the heap, and a benchmark engine compares
allocation strategies (and glibc) with real timings.

## 2. Problem statement
`malloc` is a black box. Students use it daily but cannot see block layout, fragmentation,
or why a program corrupts its heap. Production allocators (glibc, jemalloc) are too complex to
study. We need a small, correct, *observable* allocator where every OS-level mechanism
(break pointer, anonymous mappings, locking, fragmentation) is visible and measurable.

## 3. Motivation
Memory management is the most failure-prone area of systems programming: leaks, use-after-free,
double free and overflows cause crashes and security bugs. Building an allocator *with built-in
diagnostics* teaches the OS side (virtual memory, system calls) and the software side
(data structures, invariants, concurrency) at once.

## 4. Objectives
1. Implement malloc/free/calloc/realloc with correct alignment and size handling.
2. Manage free memory with a free list, **splitting** and **coalescing**.
3. Support **first-fit, best-fit, worst-fit** and compare them by measurement.
4. Use **sbrk** for normal growth and **mmap/mremap/munmap** for large blocks.
5. Make the allocator **thread-safe** (mutex) and prove it under a multi-threaded stress test.
6. Detect **double free, invalid free, buffer overrun (canary)**, report **leaks**, and verify
   all **heap invariants** with a consistency checker.
7. Provide **fragmentation analytics** with explicit formulas and a **terminal heap map**.
8. Provide automated tests, a faculty demo and reproducible benchmarks.

## 5. Scope
In scope: user-space allocator, Linux x86-64 (Ubuntu 22.04/24.04, WSL2), single process, CLI.
Out of scope (see Limitations): replacing glibc malloc system-wide, per-thread arenas,
size-class bins, security hardening, other OSes.

## 6. Architecture
```
                    HEAPFORGE CLI  (cli.c, demo.c, main.c)
                           |
        +------------------+------------------+
        v                  v                  v
   Allocation          Diagnostics        Analytics / Observatory
     Engine              Engine           stats.c  visualizer.c  benchmark.c
   allocator.c           debug.c
        |                  |                  |
        +------------------+------------------+
                           |
                    Block Manager (block.c)      header, canary, split, coalesce, validate
                           |
                  Free List Manager (free_list.c) address-ordered list, first/best/worst fit
                           |
                    Heap Manager (heap.c)        the ONLY module that calls the kernel
                     /            \
                  sbrk()          mmap() / mremap() / munmap()
                  (heap)          (large blocks)

   Concurrency layer: ONE global pthread mutex taken by every public hf_* function.
```
Details: `docs/architecture.md`.

## 7. Module descriptions
| Module | File | What it really does |
|---|---|---|
| Allocation Engine | `allocator.c` | `hf_malloc/free/calloc/realloc`, locking, configuration, counters |
| Block Manager | `block.c` | 80-byte header, canary, `split`, `absorb`/`release` (coalescing), pointer validation |
| Free List Manager | `free_list.c` | Address-ordered doubly linked list; first/best/worst-fit search |
| Heap Manager | `heap.c` | `sbrk` growth/trim, `mmap`/`mremap`/`munmap`, reset, mutex helpers |
| Debug/Safety Engine | `debug.c` | Diagnostics, `hf_check_heap` (invariants), leak report |
| Statistics Engine | `stats.c` | Counters + metrics recomputed from the real block lists |
| Heap Observatory | `visualizer.c` | Heap map, block table, free list, stats panel, `/proc/self/maps` view |
| Benchmark / Stress | `benchmark.c` | Workloads in fresh child processes, multi-threaded stress tester |
| CLI + Demo | `cli.c demo.c main.c` | Interactive shell, one-shot commands, faculty demo |

## 8. OS concepts (summary - full mapping in `docs/os-concepts.md`)
Process address space and the `[heap]` segment (`procmap` command) - virtual memory and
demand paging behind `sbrk`/`mmap` - system calls (`sbrk`, `mmap`, `mremap`, `munmap`,
`fork/execv/pipe/waitpid` in the benchmark, `clock_gettime`) - dynamic memory allocation -
internal/external fragmentation - mutual exclusion with a mutex - memory safety.

## 9. Memory model
```
 | hf_block_t header : 80 bytes | user bytes (requested) | CANARY 8 B | padding | next header ...
 ^ 16-byte aligned              ^ payload, 16-byte aligned
```
* Payload size = `align16(requested + 8)`, at least 32 bytes.
* **Alignment:** 16 bytes (`alignof(max_align_t)` on x86-64). Header size 80 = 5 x 16, so
  payload alignment follows from header alignment; the region start is aligned when
  `sbrk` grows the heap.
* Header fields: `size, requested, prev, next` (physical neighbours), `free_prev, free_next`,
  `seq` (allocation id), `file, line` (optional call site), `magic`, `flags`.
  Rationale and invariants: `docs/design.md`.

## 10. Allocation algorithm
1. `size == 0` -> `NULL`; overflow / absurd size -> `ENOMEM`.
2. `size >= mmap_threshold` (128 KiB) -> own `mmap` mapping.
3. Otherwise search the free list with the selected strategy.
4. No fit -> `sbrk` another chunk (>= 128 KiB), extend or append a free block, search again.
5. Remove the block from the free list, **split** if the remainder is usable, write header
   and canary, update counters.

## 11. Free-list design
Doubly linked, **address ordered**. Insert O(f) (walk to position), remove O(1), search O(f).
First-fit = lowest address that fits; best-fit = smallest sufficient block (stops at an exact
fit); worst-fit = largest block. Address order keeps results deterministic and makes the
coalescing invariant easy to verify.

## 12. Splitting
A free block of payload `S` serving payload `N` is split only if `S - N - 80 >= 64`
(`HF_MIN_SPLIT_PAYLOAD`). Smaller remainders would be slivers that cost 80 header bytes and
list length but can serve almost no request; handing out the whole block (internal
fragmentation) is cheaper. Tested exactly at the boundary (`tests/test_split.c`).

## 13. Coalescing
On `free`, the block is marked free and merged with its **next** neighbour if that is free and
*address-adjacent*, then with its **previous** neighbour likewise (so previous+current,
current+next and previous+current+next are all handled). Adjacency is checked by address,
because `sbrk` memory is not guaranteed contiguous (libc may move the break). A merged-away
header is stamped `DEAD`, so a later double free of that pointer is recognised as stale.

## 14. sbrk vs mmap
| | `sbrk` heap | `mmap` block |
|---|---|---|
| Used for | requests < 128 KiB | requests >= 128 KiB |
| Memory | one contiguous growing region (`[heap]`) | separate anonymous mapping per block |
| Returned to OS | only by trimming the top (`trim`) | immediately (`munmap`) |
| Resize | split/merge, or move | `mremap` |
| Fragmentation | possible | none between blocks |
They are *not* interchangeable: `sbrk` moves a single break pointer shared with libc, `mmap`
creates independent virtual-memory areas. `stats` and `procmap` show both.

## 15. Concurrency
One global mutex serialises every public call. Lock acquisitions and *contended* acquisitions
(`trylock` failed) are counted. Correctness is **tested, not assumed**: the stress tester
gives every block a per-thread byte pattern and re-verifies it before free/realloc (overlapping
blocks would break the pattern), then runs the full heap check and compares live-block counts.
This is strong evidence but not a proof of absence of races (use ThreadSanitizer for more).
The coarse lock does not scale - see the `threads` benchmark.

## 16. Debugging features (project features)
Canary check at free, double-free and invalid-free detection (with the diagnostic format
required by the spec), `check` (heap invariants), `leaks` (live allocations with `file:line`
when `HF_MALLOC` macros are used), poison-on-free (0xDD), `paranoid on` (full check after every
call). External tools (GDB, Valgrind, ASan) are separate - see `docs/debugging.md`.

## 17. Testing
`make test` builds and runs 8 test programs (~300 checks): malloc, alignment, calloc/overflow,
free/reuse, split, all coalescing cases, realloc edge cases, invalid/double free, canary, checker,
leak report, poison, strategies, stress with 8 threads, random workloads. `make asan` runs them
under AddressSanitizer + UBSan. See `docs/testing.md`.

## 18. Benchmarking
`./heapforge benchmark` runs five workloads x {first-fit, best-fit, worst-fit, glibc}, each in
a fresh child process, median of N runs, CSV export. glibc is a production allocator; HeapForge
is not expected to beat it, and the numbers you get are what you report. See `docs/benchmarking.md`.

## 19. Build instructions
```
./scripts/setup.sh     # verifies gcc/make/pthread (installs nothing)
make                   # release build -> bin/heapforge, symlink ./heapforge
make test              # unit tests
make debug             # -O0 -g3 build -> bin/heapforge-debug
make asan              # ASan+UBSan build and test run
make tsan              # ThreadSanitizer build and test run
make examples          # example programs -> bin/examples/release/
make benchmark         # full benchmark
make clean
```
Requirements: Linux, gcc, make, glibc dev headers (`sudo apt install build-essential`).

## 20. Usage examples
```
./heapforge                                   # interactive shell
./heapforge demo --step                       # faculty demo, Enter between stages
./heapforge --strategy best-fit -c "malloc 256; malloc 512; free #1; map; check; leaks"
./heapforge stress --threads 8 --operations 100000
./heapforge benchmark --quick
echo -e "malloc 1k\nmap\nstats" | ./heapforge # scripted via stdin
```
Shell commands: `help malloc calloc realloc free map stats inspect freelist procmap check leaks
strategy threshold benchmark stress demo overflow paranoid trim reset quit`.
Pointers can be hex addresses or `#id` (allocation id shown by `malloc`).
Allocator state lives in one process, so one-shot `check`/`leaks` describe a fresh heap; use the
shell, `-c`, or stdin scripts for a heap you have built up.

## 21. Limitations
* One global lock: correct but not scalable; no per-thread caches/arenas.
* O(f) free-list search/insert: slower than size-class allocators; worst-fit scans everything.
* Canaries detect *overflows past the end*, not underflows or use-after-free (poison helps
  manually); a write far beyond the canary that skips it is not detected until the neighbour's
  header fails validation.
* Double-free detection of a block whose header was *reused* by a later allocation is reported
  as an invalid pointer, not as a double free.
* `sbrk` is shared with libc; HeapForge tolerates a moved break (gaps) but only returns memory
  at the top. Linux/x86-64 only. Not a drop-in `malloc` replacement.
* Call-site info exists only for the `HF_MALLOC/HF_CALLOC/HF_REALLOC` macros.

## 22. Future enhancements
Size-class segregated bins; per-thread arenas / thread caches; boundary tags for O(1) coalescing
without `prev` pointers; red-zones on both sides + quarantine for use-after-free; `LD_PRELOAD`
shim; backtrace capture for leak call stacks; lock-free fast path; ThreadSanitizer CI.

---
See `docs/` for architecture, design, OS concepts, testing, debugging, benchmarking, Review-2
template content and a viva question bank. Suggested commit sequence: `docs/git-workflow.md`.
Licence: MIT.
