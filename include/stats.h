/*
 * stats.h - Statistics / Analytics Engine.
 *
 * Two kinds of numbers exist:
 *   1. COUNTERS (hf_counters_t): incremented by the allocator at the moment
 *      an event happens (malloc, split, coalesce, sbrk, ...).
 *   2. DERIVED METRICS (hf_stats_t): recomputed by WALKING the real block
 *      list each time a snapshot is requested. Nothing is cached or faked.
 */
#ifndef HF_STATS_H
#define HF_STATS_H

#include <stddef.h>
#include <stdint.h>
#include "config.h"

typedef struct hf_counters {
    uint64_t malloc_calls, calloc_calls, realloc_calls, free_calls;
    uint64_t total_allocs;          /* successful block allocations      */
    uint64_t total_frees;           /* successful block releases         */
    uint64_t alloc_failures;
    uint64_t active_allocs;         /* live blocks (sbrk heap + mmap)    */
    uint64_t active_requested;      /* sum of user-requested bytes, live */
    uint64_t active_payload;        /* sum of payload capacities, live   */
    uint64_t bytes_requested_total; /* cumulative requested bytes        */
    uint64_t peak_active_requested;
    uint64_t mmap_active_blocks, mmap_active_bytes;
    uint64_t splits, coalesces;
    uint64_t sbrk_calls, sbrk_bytes;
    uint64_t mmap_calls, munmap_calls, mremap_calls;
    uint64_t trim_calls, trim_bytes;
    uint64_t double_frees, invalid_frees, corruptions;
    uint64_t lock_acquired, lock_contended, paranoid_checks;
    uint64_t peak_managed;          /* peak bytes under sbrk management  */
} hf_counters_t;

typedef struct hf_stats {
    hf_counters_t c;
    hf_strategy_t strategy;
    size_t mmap_threshold;

    size_t heap_size;      /* sbrk-managed bytes: headers + payloads          */
    size_t header_bytes;   /* bytes spent on block headers                    */
    size_t free_bytes;     /* payload bytes in free blocks                    */
    size_t free_blocks;
    size_t largest_free;   /* payload of the largest free block               */
    size_t alloc_blocks;   /* allocated blocks inside the sbrk heap           */
    size_t alloc_payload;  /* payload capacity of those blocks                */
    size_t alloc_requested;/* bytes the user actually asked for in them       */
    size_t mmap_blocks;
    size_t mmap_bytes;     /* length of all live mappings                     */

    /* utilization   = alloc_requested / heap_size
     * internal_frag = (alloc_payload - alloc_requested) / alloc_payload
     * external_frag = 1 - largest_free / free_bytes   (0 when no free memory)
     * avg_free      = free_bytes / free_blocks                              */
    double utilization, internal_frag, external_frag, avg_free_block;
} hf_stats_t;

typedef enum { HF_BLOCK_FREE = 0, HF_BLOCK_ALLOC = 1, HF_BLOCK_MMAP = 2 } hf_block_state_t;

/* Read-only copy of one block's metadata (safe to use after unlock). */
typedef struct hf_blockinfo {
    uintptr_t header_addr;
    uintptr_t payload_addr;
    size_t size;           /* payload capacity */
    size_t requested;      /* 0 for free blocks */
    uint64_t id;           /* allocation sequence number (0 for free blocks) */
    const char *file;      /* call site, only when HF_MALLOC & co. were used */
    unsigned line;
    int state;             /* hf_block_state_t */
    int is_last;           /* last block of the sbrk heap */
} hf_blockinfo_t;

struct hf_heap;

void hf_stats_get(hf_stats_t *out);
void hf_stats_compute_locked(struct hf_heap *h, hf_stats_t *out);

/* Copy up to cap block descriptions (sbrk heap in address order, then mmap
 * blocks) into out. *total receives the real number of blocks. Returns the
 * number of entries written. */
size_t hf_blocks_snapshot(hf_blockinfo_t *out, size_t cap, size_t *total);
/* Same, but walks the FREE LIST in list order. */
size_t hf_freelist_snapshot(hf_blockinfo_t *out, size_t cap, size_t *total);

/* Look up a live allocation by its id; NULL if no such live block. */
void *hf_ptr_from_id(uint64_t id);
/* Describe the block that ptr points to. Returns 0 on success, -1 if ptr is
 * not the payload of a valid block. Has no side effects (no counters, no
 * diagnostics). */
int hf_block_info(void *ptr, hf_blockinfo_t *out);

#endif /* HF_STATS_H */
