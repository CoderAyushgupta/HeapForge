# Suggested commit sequence (create the history yourselves as you work)
1. `project bootstrap` - Makefile, directory layout, config.h, README skeleton, .gitignore, LICENSE
2. `allocator core` - heap.c (sbrk growth), block.h/block.c header and alignment, hf_malloc/hf_free
3. `free-list manager` - free_list.c, first-fit and best-fit (worst-fit)
4. `splitting and coalescing` - hf_block_split, hf_block_release, tests for all merge cases
5. `realloc and calloc` - in-place grow/shrink, overflow-checked calloc, mmap/mremap path
6. `diagnostics` - canary, double/invalid free, consistency checker, leak report
7. `concurrency` - global mutex, contention counters, multi-threaded stress tester
8. `analytics` - statistics, fragmentation metrics, heap observatory
9. `testing` - unit tests, ASan target, valgrind/gdb scripts
10. `benchmark and demo polish` - isolated benchmark processes, faculty demo, docs

Suggested split between three students (adjust to the real division and record it in
`docs/review2.md`): A - heap manager, block manager, free list (sections 2-4); B - allocation
engine, debug/safety engine, tests (5-6, 9); C - concurrency, analytics/visualizer, benchmark, CLI/demo (7-8, 10).
