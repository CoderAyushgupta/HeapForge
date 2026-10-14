# Viva question bank (answers point to the code)
1. **What does `hf_malloc(100)` do step by step?** `alloc_locked` in allocator.c: need = align16(100+8)=112; free-list search; split; header+canary.
2. **Why 16-byte alignment and an 80-byte header?** `alignof(max_align_t)`; 80 = 5x16 keeps payloads aligned (docs/design.md).
3. **When is a block split and why a threshold?** `hf_block_split`; remainder must have >= 64 payload bytes.
4. **Show all three coalescing cases.** `hf_block_release`; tests in test_coalesce.c; demo stage 4.
5. **Why is coalescing done by address comparison?** libc also moves the break; sbrk regions need not be contiguous.
6. **sbrk vs mmap - when and why?** README section 14; `size >= mmap_threshold`; `procmap`.
7. **How is a double free detected? When is it NOT?** magic FREE/DEAD; not when the header memory has been reused (reported as invalid pointer).
8. **How does the canary work? What does it miss?** 8 bytes after requested size, XOR block address; misses underflow and use-after-free.
9. **What is external fragmentation and how do you compute it?** `1 - largest_free/free_bytes` (stats.c).
10. **Where are the race conditions prevented?** every public function holds `h->lock`; list the shared state.
11. **How do you know it is thread-safe?** Not proven: per-thread data patterns + checker + leak count (README section 15). Mention TSan as next step.
12. **Why do first/best/worst-fit differ?** `hf_fl_find`; see your benchmark table.
13. **Why is glibc faster?** bins, tcache, arenas; HeapForge scans a single list under one lock.
14. **How does realloc grow in place?** absorbs a free adjacent block (`realloc_locked`).
15. **Why fork/exec in the benchmark?** a fresh address space per measurement removes carry-over heap state.
16. **What would you improve first?** size-class bins or per-thread arenas.
