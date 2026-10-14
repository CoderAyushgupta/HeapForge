/*
 * block.c - Block Manager: sizes, canaries, split, coalesce, validation.
 */
#include "block.h"
#include "free_list.h"
#include <string.h>

size_t hf_align_up(size_t n)
{
    return (n + (HF_ALIGNMENT - 1)) & ~(size_t)(HF_ALIGNMENT - 1);
}

int hf_payload_for_request(size_t req, size_t *payload)
{
    if (req > HF_MAX_REQUEST)
        return 0;                       /* also rules out overflow below */
    size_t need = hf_align_up(req + HF_CANARY_SIZE);
    if (need < HF_MIN_PAYLOAD)
        need = HF_MIN_PAYLOAD;
    *payload = need;
    return 1;
}

/* ---------------------------------------------------------------- canary */

uint64_t hf_canary_expected(const hf_block_t *b)
{
    return HF_CANARY_BASE ^ (uint64_t)(uintptr_t)b;
}

void hf_canary_write(hf_block_t *b)
{
    uint64_t c = hf_canary_expected(b);
    memcpy((char *)b + HF_BLOCK_HDR + b->requested, &c, sizeof c);
}

int hf_canary_ok(const hf_block_t *b, uint64_t *observed)
{
    uint64_t c;
    memcpy(&c, (const char *)b + HF_BLOCK_HDR + b->requested, sizeof c);
    if (observed) *observed = c;
    return c == hf_canary_expected(b);
}

/* ----------------------------------------------------------------- split */

hf_block_t *hf_block_split(hf_heap_t *h, hf_block_t *blk, size_t need)
{
    if (blk->size < need + HF_BLOCK_HDR + HF_MIN_SPLIT_PAYLOAD)
        return NULL;                    /* remainder would be a useless sliver */

    hf_block_t *rem = (hf_block_t *)((char *)blk + HF_BLOCK_HDR + need);
    memset(rem, 0, sizeof *rem);
    rem->size = blk->size - need - HF_BLOCK_HDR;
    rem->magic = HF_MAGIC_FREE;
    rem->flags = HF_FLAG_FREE;
    rem->prev = blk;
    rem->next = blk->next;
    if (blk->next) blk->next->prev = rem; else h->tail = rem;
    blk->next = rem;
    blk->size = need;
    h->c.splits++;
    return rem;
}

/* -------------------------------------------------------------- coalesce */

void hf_block_absorb(hf_heap_t *h, hf_block_t *a, hf_block_t *b)
{
    a->size += HF_BLOCK_HDR + b->size;
    a->next = b->next;
    if (b->next) b->next->prev = a; else h->tail = a;
    b->magic = HF_MAGIC_DEAD;           /* any later free(b) is recognisable */
    b->flags = 0;
    h->c.coalesces++;
}

hf_block_t *hf_block_release(hf_heap_t *h, hf_block_t *blk)
{
    blk->magic = HF_MAGIC_FREE;
    blk->flags = HF_FLAG_FREE;
    blk->seq = 0;
    blk->file = NULL;
    blk->line = 0;
    blk->requested = 0;

    hf_block_t *n = blk->next;
    if (n && hf_block_is_free(n) && hf_blocks_adjacent(blk, n)) {   /* current + next */
        hf_fl_remove(h, n);
        hf_block_absorb(h, blk, n);
    }
    hf_block_t *p = blk->prev;
    if (p && hf_block_is_free(p) && hf_blocks_adjacent(p, blk)) {   /* previous + current */
        hf_block_absorb(h, p, blk);     /* p is already on the free list */
        return p;
    }
    hf_fl_insert(h, blk);
    return blk;
}

/* ------------------------------------------------------------ validation */

int hf_node_plausible(const hf_heap_t *h, const void *p)
{
    uintptr_t a = (uintptr_t)p;
    if (!h->base || (a & (HF_ALIGNMENT - 1)) != 0)
        return 0;
    return a >= (uintptr_t)h->base && a + HF_BLOCK_HDR + HF_MIN_PAYLOAD <= (uintptr_t)h->end;
}

static int links_consistent(hf_heap_t *h, hf_block_t *c)
{
    uintptr_t end = (uintptr_t)h->end;
    if ((c->size & (HF_ALIGNMENT - 1)) != 0 || c->size < HF_MIN_PAYLOAD)
        return 0;
    if ((uintptr_t)c + HF_BLOCK_HDR + c->size > end)
        return 0;
    if (c->prev) {
        if (!hf_node_plausible(h, c->prev) || c->prev->next != c) return 0;
    } else if (h->head != c) {
        return 0;
    }
    if (c->next) {
        if (!hf_node_plausible(h, c->next) || c->next->prev != c) return 0;
    } else if (h->tail != c) {
        return 0;
    }
    return 1;
}

hf_ptr_status_t hf_validate_ptr(hf_heap_t *h, void *ptr, hf_block_t **out, const char **why)
{
    const char *dummy;
    if (!why) why = &dummy;
    *why = NULL;
    if (out) *out = NULL;

    if (((uintptr_t)ptr & (HF_ALIGNMENT - 1)) != 0) {
        *why = "pointer is not 16-byte aligned, so it cannot be a HeapForge payload";
        return HF_PTR_INVALID;
    }
    /* Compare as integers: forming `ptr - header` for a wild pointer would
     * be undefined behaviour before we know the address is plausible. */
    uintptr_t cand_addr = (uintptr_t)ptr - HF_BLOCK_HDR;

    /* mmap blocks: exact match against the registered list (no dereference
     * of unknown memory before the match). */
    for (hf_block_t *m = h->mmap_head; m; m = m->next) {
        if ((uintptr_t)m == cand_addr) {
            if (m->magic != HF_MAGIC_ALLOC || !hf_block_is_mmap(m)) {
                *why = "mmap block header has been overwritten";
                return HF_PTR_INVALID;
            }
            if (out) *out = m;
            return HF_PTR_OK;
        }
    }

    if (!h->base || (uintptr_t)ptr < (uintptr_t)h->base + HF_BLOCK_HDR ||
        (uintptr_t)ptr + HF_MIN_PAYLOAD > (uintptr_t)h->end) {
        *why = "address is outside the HeapForge heap and not a registered mmap block";
        return HF_PTR_INVALID;
    }

    hf_block_t *cand = (hf_block_t *)cand_addr;
    if (cand->magic == HF_MAGIC_DEAD) {
        *why = "header was merged into a neighbouring free block";
        return HF_PTR_STALE;
    }
    if (cand->magic == HF_MAGIC_FREE && (cand->flags & HF_FLAG_FREE)) {
        if (!links_consistent(h, cand)) {
            *why = "looks free but its links are inconsistent";
            return HF_PTR_INVALID;
        }
        if (out) *out = cand;
        return HF_PTR_DOUBLE_FREE;
    }
    if (cand->magic != HF_MAGIC_ALLOC || cand->flags != 0) {
        *why = "no valid block header precedes this address "
               "(interior pointer, foreign pointer, or header overwritten)";
        return HF_PTR_INVALID;
    }
    if (!links_consistent(h, cand) || cand->requested + HF_CANARY_SIZE > cand->size) {
        *why = "block header fails its consistency cross-check";
        return HF_PTR_INVALID;
    }
    if (out) *out = cand;
    return HF_PTR_OK;
}
