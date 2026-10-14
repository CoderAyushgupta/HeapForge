/*
 * debug.h - Debug / Safety Engine.
 *
 * Features: canary corruption reports, double-free / invalid-free
 * diagnostics, whole-heap consistency checker, leak (live allocation)
 * report. Everything is derived from the real block metadata.
 */
#ifndef HF_DEBUG_H
#define HF_DEBUG_H

#include <stdio.h>
#include "heap.h"

#define HF_CHECK_MAX_MSG 24
#define HF_CHECK_MSG_LEN 200

typedef struct hf_check_report {
    int ok;                        /* 1 = PASS */
    size_t violations;
    size_t blocks_checked;         /* sbrk-heap blocks */
    size_t alloc_blocks, free_blocks, mmap_blocks;
    size_t free_list_nodes;
    size_t messages_stored;
    char messages[HF_CHECK_MAX_MSG][HF_CHECK_MSG_LEN];
} hf_check_report_t;

/* Locks internally. Returns 1 on PASS, 0 on FAIL. rep may be NULL. */
int hf_check_heap(hf_check_report_t *rep);
void hf_check_print(FILE *out, const hf_check_report_t *rep);
/* Same check, caller holds the lock. */
int hf_check_locked(struct hf_heap *h, hf_check_report_t *rep);

typedef struct hf_leak_summary {
    uint64_t total_allocs, total_frees, active_allocs;
    uint64_t active_requested_bytes, active_payload_bytes;
    uint64_t mmap_blocks;
} hf_leak_summary_t;

/* Print every live allocation. Returns the number of live blocks. */
size_t hf_leak_report(FILE *out, size_t max_rows, hf_leak_summary_t *summary);

/* ---- internal diagnostics used by the allocator --------------------- */
void hf_diag_double_free(struct hf_heap *h, const char *op, void *ptr, const hf_block_t *blk, int stale);
void hf_diag_invalid_free(struct hf_heap *h, const char *op, void *ptr, const char *why);
void hf_diag_corruption(struct hf_heap *h, const hf_block_t *blk, uint64_t expected, uint64_t observed);

#endif
