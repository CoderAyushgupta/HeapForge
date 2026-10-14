/*
 * stats.c - Statistics / Analytics Engine.
 *
 * All derived metrics are computed by walking the real block lists while
 * holding the allocator lock. Walks are bounded so a corrupted (cyclic)
 * list cannot hang the analytics.
 */
#include "stats.h"
#include "free_list.h"
#include <string.h>

static size_t walk_limit(const struct hf_heap *h)
{
    return h->managed_bytes / (HF_BLOCK_HDR + HF_MIN_PAYLOAD) + 4;
}

void hf_stats_compute_locked(struct hf_heap *h, hf_stats_t *s)
{
    memset(s, 0, sizeof *s);
    s->c = h->c;
    s->strategy = h->strategy;
    s->mmap_threshold = h->mmap_threshold;

    size_t limit = walk_limit(h), n = 0;
    for (hf_block_t *b = h->head; b && n++ < limit; b = b->next) {
        if (!hf_node_plausible(h, b)) break;
        s->heap_size += HF_BLOCK_HDR + b->size;
        s->header_bytes += HF_BLOCK_HDR;
        if (hf_block_is_free(b)) {
            s->free_blocks++;
            s->free_bytes += b->size;
            if (b->size > s->largest_free) s->largest_free = b->size;
        } else {
            s->alloc_blocks++;
            s->alloc_payload += b->size;
            s->alloc_requested += b->requested;
        }
    }
    for (hf_block_t *m = h->mmap_head; m; m = m->next) {
        s->mmap_blocks++;
        s->mmap_bytes += HF_BLOCK_HDR + m->size;
    }
    if (s->heap_size) s->utilization = (double)s->alloc_requested / (double)s->heap_size;
    if (s->alloc_payload)
        s->internal_frag = (double)(s->alloc_payload - s->alloc_requested) / (double)s->alloc_payload;
    if (s->free_bytes)
        s->external_frag = 1.0 - (double)s->largest_free / (double)s->free_bytes;
    if (s->free_blocks) s->avg_free_block = (double)s->free_bytes / (double)s->free_blocks;
}

void hf_stats_get(hf_stats_t *out)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h);
    hf_stats_compute_locked(h, out);
    hf_heap_unlock(h);
}

static void fill_info(const struct hf_heap *h, hf_block_t *b, hf_blockinfo_t *o)
{
    memset(o, 0, sizeof *o);
    o->header_addr = (uintptr_t)b;
    o->payload_addr = (uintptr_t)b + HF_BLOCK_HDR;
    o->size = b->size;
    if (hf_block_is_mmap(b)) o->state = HF_BLOCK_MMAP;
    else if (hf_block_is_free(b)) o->state = HF_BLOCK_FREE;
    else o->state = HF_BLOCK_ALLOC;
    if (o->state != HF_BLOCK_FREE) {
        o->requested = b->requested;
        o->id = b->seq;
        o->file = b->file;
        o->line = b->line;
    }
    o->is_last = (b == h->tail);
}

size_t hf_blocks_snapshot(hf_blockinfo_t *out, size_t cap, size_t *total)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h);
    size_t limit = walk_limit(h), n = 0, w = 0, t = 0;
    for (hf_block_t *b = h->head; b && n++ < limit; b = b->next) {
        if (!hf_node_plausible(h, b)) break;
        if (w < cap) fill_info(h, b, &out[w++]);
        t++;
    }
    for (hf_block_t *m = h->mmap_head; m; m = m->next) {
        if (w < cap) fill_info(h, m, &out[w++]);
        t++;
    }
    hf_heap_unlock(h);
    if (total) *total = t;
    return w;
}

size_t hf_freelist_snapshot(hf_blockinfo_t *out, size_t cap, size_t *total)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h);
    size_t limit = walk_limit(h), n = 0, w = 0, t = 0;
    for (hf_block_t *b = h->free_head; b && n++ < limit; b = b->free_next) {
        if (!hf_node_plausible(h, b)) break;
        if (w < cap) fill_info(h, b, &out[w++]);
        t++;
    }
    hf_heap_unlock(h);
    if (total) *total = t;
    return w;
}

void *hf_ptr_from_id(uint64_t id)
{
    struct hf_heap *h = hf_heap();
    void *found = NULL;
    if (id == 0) return NULL;
    hf_heap_lock(h);
    size_t limit = walk_limit(h), n = 0;
    for (hf_block_t *b = h->head; b && n++ < limit && !found; b = b->next) {
        if (!hf_node_plausible(h, b)) break;
        if (!hf_block_is_free(b) && b->seq == id) found = hf_block_payload(b);
    }
    for (hf_block_t *m = h->mmap_head; m && !found; m = m->next)
        if (m->seq == id) found = hf_block_payload(m);
    hf_heap_unlock(h);
    return found;
}

int hf_block_info(void *ptr, hf_blockinfo_t *out)
{
    struct hf_heap *h = hf_heap();
    hf_block_t *blk = NULL;
    hf_heap_lock(h);
    hf_ptr_status_t st = hf_validate_ptr(h, ptr, &blk, NULL);
    int rc = -1;
    if ((st == HF_PTR_OK || st == HF_PTR_DOUBLE_FREE) && blk) {
        fill_info(h, blk, out);
        rc = 0;
    }
    hf_heap_unlock(h);
    return rc;
}
