# Review-2 content pack (copy into the official PBL Review-2 template)

Use this as raw material. Items marked **[CAPTURE]** need evidence you must produce yourselves by
running the project; do not paste numbers or screenshots that you did not generate.

## 1. Problem Statement
Applications depend on `malloc/free`, but the allocator is opaque: students cannot see block
layout, fragmentation or the system calls behind it, and memory bugs (double free, overflow, leak)
are hard to diagnose. HeapForge is a Linux user-space allocator whose internals are observable and
which diagnoses its own misuse. (Approved use case #6, "A malloc/free implementation", Stretch;
Memory + Concurrency.)

## 2. Objectives
See README section 4 (eight objectives). Updated objectives versus Review-1: add diagnostics
(canary, double/invalid free, consistency checker), observatory, isolated benchmarking. **[Edit if your Review-1 differed.]**

## 3. Scope
README section 5 and 21.

## 4. System Architecture  **[CAPTURE: paste the diagram from README section 6 / docs/architecture.md]**

## 5. Architecture Explanation
docs/architecture.md: layers, global state with one mutex, flows of malloc and free, benchmark process model.

## 6. Technology Stack
C11 (gnu11), GCC, GNU Make, POSIX threads, Linux system calls (sbrk, mmap, mremap, munmap, fork,
execv, pipe, clock_gettime), Bash scripts, Ubuntu 22.04/24.04 (or WSL2). Debug aids: GDB, Valgrind,
AddressSanitizer/UBSan, strace. No external libraries, no network, no database.

## 7. Module Description
README section 7 (table).

## 8. Development Environment
**[FILL: each student's OS version, `gcc --version`, `uname -r`, editor.]** Build: `make`, tests: `make test`.

## 9. Core Module-I - Allocation + block management
Header layout, alignment, strategies, splitting threshold, coalescing, realloc/calloc: docs/design.md.
Evidence **[CAPTURE]**: `./heapforge demo` stages 1-4 (screenshots of heap map before/after free, split, coalesce).

## 10. Core Module-II - Diagnostics, concurrency, analytics
Canary/double-free/invalid-free/checker/leaks (docs/debugging.md); mutex and stress tester
(README section 15); statistics and fragmentation formulas (docs/design.md).
Evidence **[CAPTURE]**: demo stage 7 diagnostics, `stress --threads 8 --operations 100000`.

## 11. Module Integration
CLI -> Allocation Engine -> Block Manager -> Free List -> Heap Manager -> kernel; Diagnostics and
Analytics read the same block lists under the same lock; benchmark drives the public API.

## 12. OS Concepts Used
docs/os-concepts.md (table with code locations).

## 13. Testing Approach
docs/testing.md (methodology). ASan/UBSan run: `make asan`.

## 14. Test Cases
docs/testing.md (mapping table). **[CAPTURE]**: screenshot of `make test` ending with `ALL TEST PROGRAMS PASSED`.

## 15. Debugging Details
docs/debugging.md. Real bug found during development by the sanitizer build: pointer arithmetic
underflow for wild pointers in `hf_validate_ptr` (fixed by validating the address as an integer first).
A second real bug found by tests/demo: a split remainder merged back into the block being
allocated because the block was not yet marked allocated (fixed by ordering in `alloc_locked`).
**[CAPTURE]**: GDB session (`scripts/debug_gdb.sh`) and Valgrind run (`scripts/run_valgrind.sh`) if installed.

## 16. Performance and Optimization
docs/benchmarking.md. **[CAPTURE]**: `scripts/benchmark.sh` output table and `results/benchmark.csv`
from YOUR machine; discuss first/best/worst-fit and glibc using your numbers. Design optimisations
actually in the code: 128 KiB heap chunks (fewer syscalls), exact-fit early exit in best-fit,
in-place realloc, `mremap` for large blocks, address-ordered list. Not implemented (future work):
size-class bins, thread caches.

## 17. Final Results
**[CAPTURE]**: demo run, test run, stress run, benchmark table. Describe only what you observed.

## 18. Achievement of Objectives
Map objectives 1-8 to evidence: tests (1,2,3,6), `procmap`/stats (4), stress (5), diagnostics (6),
`stats`/`map` (7), `make test`/demo/benchmark (8).

## 19. Individual Contributions  **[FILL with the truth for the three students]**
| Student | Modules / files | Tests / docs | Demo part |
|---|---|---|---|
| 1 | | | |
| 2 | | | |
| 3 | | | |
Suggested split: docs/git-workflow.md. Everyone must be able to explain every module (see docs/viva.md).

## 20. Current Limitations
README section 21.

## 21. Future Enhancements
README section 22.

## 22. References
* R. Love, *Linux Kernel Development*; M. Kerrisk, *The Linux Programming Interface* (brk/sbrk, mmap, pthreads chapters)
* Silberschatz, Galvin, Gagne, *Operating System Concepts* (main memory, fragmentation)
* Linux man pages: `sbrk(2)`, `mmap(2)`, `mremap(2)`, `munmap(2)`, `pthread_mutex_lock(3)`, `malloc(3)`, `proc(5)`
* glibc malloc internals documentation (for comparison)
* C. Lever / D. Boreham, "malloc() performance in a multithreaded Linux environment"; Wilson et al., "Dynamic Storage Allocation: A Survey and Critical Review"
**[Verify every reference you cite is something you actually consulted.]**
