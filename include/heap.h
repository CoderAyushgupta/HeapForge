/*
 * heap.h - Heap Manager: the only module that talks to the kernel.
 *
 *   sbrk()   grows the contiguous "program break" heap in 128 KiB chunks.
 *   mmap()   gives every LARGE request its own anonymous mapping.
 *   mremap() grows/shrinks such mappings.
 *   munmap() returns them to the kernel immediately on free.
 *
 * The whole allocator state lives in ONE struct hf_heap protected by ONE
 * mutex (coarse-grained locking). All functions suffixed _locked / taking
 * a heap pointer expect the caller to hold h->lock.
 */
#ifndef HF_HEAP_H
#define HF_HEAP_H

#include <pthread.h>
#include <stdio.h>
#include "block.h"
#include "stats.h"

struct hf_heap {
    pthread_mutex_t lock;
    hf_block_t *head, *tail;   /* physical list of sbrk blocks (address order) */
    hf_block_t *free_head;     /* free list, address ordered                   */
    hf_block_t *mmap_head;     /* list of mmap blocks (unordered)              */
    void *base, *end;          /* [base,end) = span of the sbrk heap           */
    size_t managed_bytes;      /* bytes of sbrk memory that hold blocks        */
    uint64_t next_seq;

    /* configuration */
    hf_strategy_t strategy;
    size_t mmap_threshold;
    size_t heap_limit;         /* 0 = unlimited (testing aid)                  */
    int poison;                /* overwrite freed payloads with 0xDD           */
    int verbose;               /* print diagnostics to stderr                  */
    int paranoid;              /* run the full consistency check after each op */

    hf_counters_t c;
};

struct hf_heap *hf_heap(void);
void hf_heap_lock(struct hf_heap *h);
void hf_heap_unlock(struct hf_heap *h);
size_t hf_page_size(void);

/* sbrk path */
hf_block_t *hf_heap_grow(struct hf_heap *h, size_t need_payload);
size_t hf_heap_trim(struct hf_heap *h);
/* mmap path */
hf_block_t *hf_heap_map_block(struct hf_heap *h, size_t need_payload);
hf_block_t *hf_heap_remap_block(struct hf_heap *h, hf_block_t *blk, size_t need_payload);
void hf_heap_unmap_block(struct hf_heap *h, hf_block_t *blk);

/* Free every block, merge, clear statistics (memory stays with the process). */
void hf_heap_reset(struct hf_heap *h);

#endif /* HF_HEAP_H */
