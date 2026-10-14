# HeapForge design notes

## Constants (include/config.h) and why
| Constant | Value | Reason |
|---|---|---|
| `HF_ALIGNMENT` | 16 | `alignof(max_align_t)` on x86-64; any C type fits, SSE-safe |
| `HF_CANARY_SIZE` | 8 | a 64-bit guard right after the requested bytes; a 1-byte overflow already hits it |
| `HF_MIN_PAYLOAD` | 32 | smallest useful block, keeps sizes multiples of 16 |
| `HF_MIN_SPLIT_PAYLOAD` | 64 | remainder must be able to serve a real request; avoids slivers |
| `HF_HEAP_CHUNK` | 128 KiB | amortise `sbrk` syscalls; multiple of the page size |
| `HF_MMAP_THRESHOLD_DEFAULT` | 128 KiB | large blocks get their own mapping and can be returned at once (same default as glibc) |
| `HF_MAX_REQUEST` | 2^40 | rejects absurd sizes early, makes `size + canary + alignment` overflow-free |

## Block header (80 bytes)
```
off  0  size        payload capacity (multiple of 16)
off  8  requested   bytes the caller asked for (0 when free)
off 16  prev        physical predecessor in the sbrk heap (mmap blocks: list prev)
off 24  next        physical successor
off 32  free_prev   free-list links (valid only while free)
off 40  free_next
off 48  seq         allocation id 1,2,3,... (0 when free)
off 56  file        call-site file (static string) or NULL
off 64  line        call-site line
off 68  magic       ALLOC 0xA110C8ED | FREE 0xF4EEB10C | DEAD 0xDEADB10C
off 72  flags       bit0 FREE, bit1 MMAP
off 76  reserved
```
Kept: only fields used by a feature: size/prev/next (walk, coalesce), free links (O(1) removal),
requested (canary position, utilisation, realloc copy length), seq/file/line (leak report),
magic/flags (state + corruption detection). Not stored: a footer, because `prev` pointers
already give O(1) access to the left neighbour.

## Invariants (checked by `hf_check_heap`)
I1 header addresses and payload sizes are multiples of 16. I2 `size >= 32`. I3 magic agrees with the
FREE flag. I4 `b->prev` is the physical predecessor; blocks do not overlap and lie inside the
heap. I5 no two address-adjacent free blocks exist. I6 the free list contains exactly the free
blocks, is address ordered and doubly consistent, and is acyclic. I7 allocated blocks satisfy
`requested + 8 <= size` and their canary equals `HF_CANARY_BASE ^ block_address`. I8 bookkeeping
matches reality: `managed_bytes`, `active_allocs`, `active_requested`, `active_payload`,
`mmap_active_bytes` equal what a walk of the lists finds. Walks are bounded, so a corrupted cyclic
list is reported instead of hanging.

## Pointer validation (`hf_validate_ptr`)
Rejects, without dereferencing unknown memory first: misaligned pointers; addresses outside
`[heap_base+80, heap_end)` unless they exactly match a registered mmap block. For an address inside the heap it checks
the magic (`DEAD` -> stale/double free, `FREE` -> double free, other than `ALLOC` -> invalid),
then cross-checks size, `prev->next == b`, `next->prev == b` and `head/tail`. An interior pointer
or a payload that merely contains the magic bytes fails the link cross-check.

## Fragmentation metrics
`utilization = requested bytes in live sbrk blocks / sbrk heap bytes`;
`internal = (payload - requested) / payload` over live blocks (header excluded);
`external = 1 - largest_free_payload / total_free_payload` (0 when nothing is free);
`avg_free = free_bytes / free_blocks`. Computed in `stats.c` from the lists at call time.

## realloc
NULL -> malloc; size 0 -> free and `NULL`; shrink -> in place, split surplus and release it
(merging with a free neighbour); grow -> absorb a free adjacent block if it is large enough,
otherwise allocate-copy-release; mmap blocks use `mremap(MREMAP_MAYMOVE)` while the new size stays
above the threshold, and move to the sbrk heap when shrunk below it. A failed realloc leaves the
original block untouched.

## Design decisions
* Single mutex: simplest design that is demonstrably correct; limits scalability (documented).
* Address-ordered free list: deterministic first-fit, easy invariants; costs O(f) inserts.
* Coalescing by address adjacency, not by assumption: the break may have been moved by libc.
* Header stores `prev/next` instead of boundary tags: simpler to verify, costs 16 bytes/block.
* Diagnostics never abort: they count, report and keep the heap valid (paranoid mode aborts).
