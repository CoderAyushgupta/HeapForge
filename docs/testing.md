# Testing

`make test` (or `scripts/run_tests.sh`) builds `bin/tests/release/*` and runs every program; the
exit status is non-zero if any check fails. `make asan` repeats the run with AddressSanitizer and
UndefinedBehaviorSanitizer. Each test starts from `hf_reset()` with default configuration.

## Methodology
* **White-box checks**: tests read block metadata through `hf_block_info`, `hf_blocks_snapshot`,
  `hf_freelist_snapshot` and the public header struct.
* **Independent oracles**: fragmentation metrics are recomputed in the test from the block
  snapshot and compared with `hf_stats_get`.
* **Fault injection**: tests overwrite canaries, corrupt headers, break counters and verify that
  `hf_check_heap` reports FAIL, then repair and verify PASS.
* **Randomised workloads** with data-pattern verification under all three strategies.
* **Concurrency**: 8 threads with a barrier + `hf_stress_run` per strategy; success criteria are
  zero integrity errors, heap check PASS, zero leaked blocks, zero corruption/invalid/double frees.

## Test cases (required list -> test)
| Required case | Test function (file) |
|---|---|
| malloc basic | `test_malloc_basic` (test_malloc.c) |
| malloc alignment | `test_malloc_alignment` |
| malloc multiple blocks | `test_malloc_multiple_blocks` |
| free | `test_malloc_basic`, `test_free_reuses_block` (test_free.c) |
| reuse freed block | `test_free_reuses_block` |
| split | `test_split_creates_remainder`, `test_split_threshold_boundary`, `test_no_split_below_threshold`, `test_first_alloc_splits_initial_chunk` (test_split.c) |
| coalesce previous / next / both | `test_coalesce_with_previous`, `test_coalesce_with_next`, `test_coalesce_both_sides` (test_coalesce.c); also `test_no_coalesce_between_live_neighbours`, `test_everything_merges_back_to_one_block` |
| calloc | `test_calloc_zeroes` |
| calloc overflow | `test_calloc_overflow` |
| realloc growth / shrink / NULL / zero | `test_realloc_grow_moves_and_preserves`, `test_realloc_grow_in_place`, `test_realloc_shrink_in_place_and_releases_tail`, `test_realloc_null_is_malloc`, `test_realloc_zero_frees` (test_realloc.c) + mmap and failure cases |
| free NULL | `test_free_null` |
| invalid pointer handling | `test_invalid_pointers_are_rejected`, `test_realloc_invalid_pointer` |
| double free detection | `test_double_free_detected`, `test_double_free_after_coalescing`, `test_realloc_double_free_style_misuse` |
| canary corruption detection | `test_canary_detected_on_free`, `test_checker_reports_canary_violation` (test_debug.c) |
| consistency checking | `test_checker_detects_damaged_headers`, `test_checker_detects_counter_mismatch`, `test_paranoid_mode_random_ops` |
| leak report | `test_leak_report_counts_live_blocks` |
| allocation failure | `test_allocation_failure_is_clean`, `test_malloc_zero_and_huge` |
| stress / multithread | `test_stress_run_all_strategies`, `test_threads_hold_disjoint_memory` (test_threads.c); `test_random_*` (test_stress.c) |
| fragmentation metric | `test_fragmentation_metrics_match_independent_computation` |

## What the tests do not prove
Absence of all data races (`make tsan` runs the suite under ThreadSanitizer, which reports races it observes at run time; a clean run is evidence, not proof), behaviour on non-x86-64
platforms, or performance. Tests that depend on the kernel layout (`test_trim_returns_memory_to_kernel`)
accept "nothing to trim" and print a note when libc has allocated above HeapForge's heap.

## Manual / demo checks
`./heapforge demo` exercises the same features end to end; `examples/` contains five small
programs (`make examples`, binaries in `bin/examples/release/`).
