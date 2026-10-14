/*
 * block.h - Block Manager.
 *
 * Block layout in memory (sbrk heap):
 *
 *   |<------------- hf_block_t header (80 B) ------------>|<-- payload (size B) -->|
 *   | size | requested | prev | next | free_prev | free_next | seq | file | line | magic | flags |  user bytes ... CANARY pad |
 *
 *  - payload address = header address + 80  (80 % 16 == 0, so the payload is
 *    16-byte aligned whenever the header is).
 *  - the CANARY (8 bytes) sits right after the `requested` user bytes.
 *  - `prev`/`next` link ALL blocks of the sbrk heap in address order
 *    (physical neighbours). For mmap blocks they form a separate list.
 *  - `free_prev`/`free_next` are only meaningful while the block is free.
 *
 * Invariants (verified by hf_check_heap, see debug.h):
 *  I1  header addresses and payload sizes are multiples of 16
 *  I2  size >= HF_MIN_PAYLOAD
 *  I3  magic matches the state flag (ALLOC <-> !FREE, FREE <-> FREE)
 *  I4  block->next->prev == block (physical list is doubly consistent)
 *  I5  address-adjacent free blocks never exist (always coalesced)
 *  I6  a block is on the free list  <=>  it is free
 *  I7  allocated blocks: requested + CANARY <= size and canary intact
 */
#ifndef HF_BLOCK_H
#define HF_BLOCK_H

#include "config.h"

#define HF_FLAG_FREE 0x1u
#define HF_FLAG_MMAP 0x2u

typedef struct hf_block {
    size_t size;               /* payload capacity in bytes */
    size_t requested;          /* bytes the caller asked for (allocated only) */
    struct hf_block *prev;     /* physical predecessor */
    struct hf_block *next;     /* physical successor */
    struct hf_block *free_prev;
    struct hf_block *free_next;
    uint64_t seq;              /* allocation id, 1,2,3,... (0 when free) */
    const char *file;          /* optional call site (static string) */
    uint32_t line;
    uint32_t magic;
    uint32_t flags;
    uint32_t reserved;
} hf_block_t;

#define HF_BLOCK_HDR (sizeof(hf_block_t))
_Static_assert(sizeof(hf_block_t) % HF_ALIGNMENT == 0,
               "header size must keep payloads 16-byte aligned");

typedef struct hf_heap hf_heap_t;

typedef enum {
    HF_PTR_OK = 0,
    HF_PTR_INVALID,      /* not a block payload (or header destroyed)   */
    HF_PTR_DOUBLE_FREE,  /* header says: already free                   */
    HF_PTR_STALE         /* header was merged into a neighbour (freed)  */
} hf_ptr_status_t;

/* ---- sizes -------------------------------------------------------- */
size_t hf_align_up(size_t n);
/* Payload size needed for a request (adds canary, rounds up, min size).
 * Returns 0 if the request is too large. */
int hf_payload_for_request(size_t req, size_t *payload);

/* ---- accessors ---------------------------------------------------- */
static inline void *hf_block_payload(hf_block_t *b) { return (char *)b + HF_BLOCK_HDR; }
static inline hf_block_t *hf_payload_block(void *p) { return (hf_block_t *)((char *)p - HF_BLOCK_HDR); }
static inline char *hf_block_end(hf_block_t *b) { return (char *)b + HF_BLOCK_HDR + b->size; }
static inline int hf_block_is_free(const hf_block_t *b) {
    return b->magic == HF_MAGIC_FREE && (b->flags & HF_FLAG_FREE) != 0;
}
static inline int hf_block_is_mmap(const hf_block_t *b) { return (b->flags & HF_FLAG_MMAP) != 0; }
static inline int hf_blocks_adjacent(hf_block_t *a, hf_block_t *b) { return hf_block_end(a) == (char *)b; }

/* ---- canary ------------------------------------------------------- */
uint64_t hf_canary_expected(const hf_block_t *b);
void hf_canary_write(hf_block_t *b);
/* Returns 1 if the canary is intact. *observed gets the bytes found. */
int hf_canary_ok(const hf_block_t *b, uint64_t *observed);

/* ---- structural operations --------------------------------------- */
/* Split `blk` (not on the free list) so that it keeps exactly `need`
 * payload bytes. Returns the new FREE remainder block (NOT yet inserted in
 * the free list) or NULL if the remainder would be below the split
 * threshold. */
hf_block_t *hf_block_split(hf_heap_t *h, hf_block_t *blk, size_t need);
/* Merge physical successor b into a (b must be address-adjacent to a). */
void hf_block_absorb(hf_heap_t *h, hf_block_t *a, hf_block_t *b);
/* Mark blk free, coalesce with free neighbours, make sure the result is on
 * the free list. Returns the surviving free block. */
hf_block_t *hf_block_release(hf_heap_t *h, hf_block_t *blk);

/* ---- validation --------------------------------------------------- */
/* Is p a plausible block pointer inside the sbrk heap (range+alignment)? */
int hf_node_plausible(const hf_heap_t *h, const void *p);
/* Classify a user pointer WITHOUT modifying anything. */
hf_ptr_status_t hf_validate_ptr(hf_heap_t *h, void *ptr, hf_block_t **blk, const char **why);

#endif /* HF_BLOCK_H */
