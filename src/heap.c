/*
 * heap.c - Heap Manager: sbrk()/mmap() growth, trimming, reset, locking.
 */
#include "heap.h"
#include "free_list.h"
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static struct hf_heap g_heap = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .next_seq = 1,
    .strategy = HF_STRATEGY_FIRST_FIT,
    .mmap_threshold = HF_MMAP_THRESHOLD_DEFAULT,
    .poison = 1,
    .verbose = 1,
};

struct hf_heap *hf_heap(void) { return &g_heap; }

void hf_heap_lock(struct hf_heap *h)
{
    if (pthread_mutex_trylock(&h->lock) != 0) {
        pthread_mutex_lock(&h->lock);       /* had to wait: real contention */
        h->c.lock_contended++;
    }
    h->c.lock_acquired++;
}

void hf_heap_unlock(struct hf_heap *h) { pthread_mutex_unlock(&h->lock); }

size_t hf_page_size(void)
{
    long v = sysconf(_SC_PAGESIZE);
    return v > 0 ? (size_t)v : 4096u;
}

/* ------------------------------------------------------------------ sbrk */

hf_block_t *hf_heap_grow(struct hf_heap *h, size_t need)
{
    size_t page = hf_page_size();
    size_t min_want = (need + HF_BLOCK_HDR + page - 1) & ~(page - 1);
    size_t want = min_want < HF_HEAP_CHUNK ? HF_HEAP_CHUNK : min_want;

    if (h->heap_limit != 0 && h->managed_bytes + want > h->heap_limit) {
        want = min_want;                    /* fall back to the bare minimum */
        if (h->managed_bytes + want > h->heap_limit) {
            errno = ENOMEM;
            return NULL;
        }
    }

    /* Make the new region start 16-byte aligned: probe the break first. */
    void *cur = sbrk(0);
    if (cur == (void *)-1) { errno = ENOMEM; return NULL; }
    size_t pad = (HF_ALIGNMENT - ((uintptr_t)cur & (HF_ALIGNMENT - 1))) & (HF_ALIGNMENT - 1);
    void *r = sbrk((intptr_t)(want + pad));
    if (r == (void *)-1) { errno = ENOMEM; return NULL; }
    h->c.sbrk_calls++;
    h->c.sbrk_bytes += want + pad;

    char *start = (char *)(((uintptr_t)r + HF_ALIGNMENT - 1) & ~(uintptr_t)(HF_ALIGNMENT - 1));
    size_t bytes = (size_t)(((char *)r + want + pad) - start) & ~(size_t)(HF_ALIGNMENT - 1);

    h->managed_bytes += bytes;
    if (h->managed_bytes > h->c.peak_managed) h->c.peak_managed = h->managed_bytes;
    if (!h->base) h->base = start;
    h->end = start + bytes;

    /* The break is shared with libc's malloc: if somebody else moved it,
     * the new memory is NOT adjacent to our tail. Adjacency is therefore
     * always decided by address comparison, never assumed. */
    if (h->tail && hf_block_end(h->tail) == start && hf_block_is_free(h->tail)) {
        h->tail->size += bytes;             /* extend the free tail in place */
        return h->tail;
    }
    hf_block_t *b = (hf_block_t *)start;
    memset(b, 0, sizeof *b);
    b->size = bytes - HF_BLOCK_HDR;
    b->magic = HF_MAGIC_FREE;
    b->flags = HF_FLAG_FREE;
    b->prev = h->tail;
    if (h->tail) h->tail->next = b; else h->head = b;
    h->tail = b;
    hf_fl_insert(h, b);
    return b;
}

size_t hf_heap_trim(struct hf_heap *h)
{
    hf_block_t *t = h->tail;
    if (!t || !hf_block_is_free(t))
        return 0;
    void *brk_now = sbrk(0);
    if (brk_now == (void *)-1 || brk_now != (void *)hf_block_end(t))
        return 0;                           /* something else sits above us */
    size_t page = hf_page_size();
    size_t keep = HF_MIN_SPLIT_PAYLOAD;
    if (t->size <= keep + page)
        return 0;
    size_t release = (t->size - keep) & ~(page - 1);
    if (release == 0)
        return 0;
    if (sbrk(-(intptr_t)release) == (void *)-1)
        return 0;
    t->size -= release;
    h->end = (char *)h->end - release;
    h->managed_bytes -= release;
    h->c.trim_calls++;
    h->c.trim_bytes += release;
    return release;
}

/* ------------------------------------------------------------------ mmap */

static size_t map_len_for(size_t need)
{
    size_t page = hf_page_size();
    return (need + HF_BLOCK_HDR + page - 1) & ~(page - 1);
}

hf_block_t *hf_heap_map_block(struct hf_heap *h, size_t need)
{
    size_t len = map_len_for(need);
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { errno = ENOMEM; return NULL; }
    hf_block_t *b = (hf_block_t *)p;
    memset(b, 0, sizeof *b);
    b->size = len - HF_BLOCK_HDR;
    b->magic = HF_MAGIC_ALLOC;
    b->flags = HF_FLAG_MMAP;
    b->next = h->mmap_head;
    if (h->mmap_head) h->mmap_head->prev = b;
    h->mmap_head = b;
    h->c.mmap_calls++;
    h->c.mmap_active_blocks++;
    h->c.mmap_active_bytes += len;
    return b;
}

void hf_heap_unmap_block(struct hf_heap *h, hf_block_t *b)
{
    size_t len = b->size + HF_BLOCK_HDR;
    if (b->prev) b->prev->next = b->next; else h->mmap_head = b->next;
    if (b->next) b->next->prev = b->prev;
    b->magic = HF_MAGIC_DEAD;
    munmap(b, len);
    h->c.munmap_calls++;
    if (h->c.mmap_active_blocks) h->c.mmap_active_blocks--;
    h->c.mmap_active_bytes = h->c.mmap_active_bytes >= len ? h->c.mmap_active_bytes - len : 0;
}

hf_block_t *hf_heap_remap_block(struct hf_heap *h, hf_block_t *b, size_t need)
{
    size_t old_len = b->size + HF_BLOCK_HDR;
    size_t new_len = map_len_for(need);
    void *p = mremap(b, old_len, new_len, MREMAP_MAYMOVE);
    if (p == MAP_FAILED) { errno = ENOMEM; return NULL; }
    hf_block_t *nb = (hf_block_t *)p;
    nb->size = new_len - HF_BLOCK_HDR;
    if (nb->prev) nb->prev->next = nb; else h->mmap_head = nb;
    if (nb->next) nb->next->prev = nb;
    h->c.mremap_calls++;
    h->c.mmap_active_bytes = h->c.mmap_active_bytes - old_len + new_len;
    return nb;
}

/* ----------------------------------------------------------------- reset */

void hf_heap_reset(struct hf_heap *h)
{
    while (h->mmap_head)
        hf_heap_unmap_block(h, h->mmap_head);

    h->free_head = NULL;
    hf_block_t *last_free = NULL;
    hf_block_t *b = h->head;
    while (b) {
        b->requested = 0; b->seq = 0; b->file = NULL; b->line = 0;
        b->magic = HF_MAGIC_FREE; b->flags = HF_FLAG_FREE;
        hf_block_t *n = b->next;
        while (n && hf_blocks_adjacent(b, n)) {   /* merge the whole adjacent run */
            b->size += HF_BLOCK_HDR + n->size;
            n->magic = HF_MAGIC_DEAD; n->flags = 0;
            n = n->next;
        }
        b->next = n;
        if (n) n->prev = b; else h->tail = b;
        b->free_prev = last_free;
        b->free_next = NULL;
        if (last_free) last_free->free_next = b; else h->free_head = b;
        last_free = b;
        b = n;
    }
    memset(&h->c, 0, sizeof h->c);
    h->c.peak_managed = h->managed_bytes;
    h->next_seq = 1;
}
