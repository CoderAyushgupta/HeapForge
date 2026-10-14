/*
 * allocator.c - Allocation Engine: hf_malloc / hf_free / hf_calloc /
 * hf_realloc plus configuration. Public entry points take the global
 * mutex; the *_locked helpers assume it is held.
 */
#include "allocator.h"
#include "debug.h"
#include "free_list.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ accounting */

static void account_alloc(struct hf_heap *h, const hf_block_t *b, size_t requested)
{
    hf_counters_t *c = &h->c;
    c->total_allocs++;
    c->active_allocs++;
    c->bytes_requested_total += requested;
    c->active_requested += requested;
    c->active_payload += b->size;
    if (c->active_requested > c->peak_active_requested)
        c->peak_active_requested = c->active_requested;
}

static void account_free(struct hf_heap *h, const hf_block_t *b)
{
    hf_counters_t *c = &h->c;
    c->total_frees++;
    if (c->active_allocs) c->active_allocs--;
    c->active_requested -= b->requested <= c->active_requested ? b->requested : c->active_requested;
    c->active_payload -= b->size <= c->active_payload ? b->size : c->active_payload;
}

static void paranoid_verify(struct hf_heap *h, const char *op)
{
    if (!h->paranoid) return;
    hf_check_report_t rep;
    h->c.paranoid_checks++;
    if (!hf_check_locked(h, &rep)) {
        fprintf(stderr, "HEAPFORGE PARANOID CHECK FAILED after %s:\n", op);
        hf_check_print(stderr, &rep);
        abort();
    }
}

/* -------------------------------------------------------------- allocate */

static void *alloc_locked(struct hf_heap *h, size_t size, const char *file, unsigned line)
{
    if (size == 0)
        return NULL;
    size_t need;
    if (!hf_payload_for_request(size, &need)) {
        h->c.alloc_failures++;
        errno = ENOMEM;
        return NULL;
    }

    hf_block_t *blk;
    if (size >= h->mmap_threshold) {                    /* large: own mapping */
        blk = hf_heap_map_block(h, need);
        if (!blk) { h->c.alloc_failures++; return NULL; }
    } else {                                            /* normal: sbrk heap  */
        blk = hf_fl_find(h, need, h->strategy);
        if (!blk) {
            if (!hf_heap_grow(h, need)) { h->c.alloc_failures++; return NULL; }
            blk = hf_fl_find(h, need, h->strategy);
            if (!blk) { h->c.alloc_failures++; errno = ENOMEM; return NULL; }
        }
        hf_fl_remove(h, blk);
        /* Mark it allocated BEFORE releasing the split remainder: otherwise
         * the remainder would see a "free" physical predecessor and merge
         * straight back into it. */
        blk->magic = HF_MAGIC_ALLOC;
        blk->flags = 0;
        hf_block_t *rem = hf_block_split(h, blk, need);
        if (rem) hf_block_release(h, rem);
    }

    blk->requested = size;
    blk->seq = h->next_seq++;
    blk->file = file;
    blk->line = line;
    hf_canary_write(blk);
    account_alloc(h, blk, size);
    return hf_block_payload(blk);
}

/* ------------------------------------------------------------------ free */

/* Release an already validated block (shared by free and realloc-move). */
static void release_validated(struct hf_heap *h, hf_block_t *blk)
{
    account_free(h, blk);
    if (hf_block_is_mmap(blk)) {
        hf_heap_unmap_block(h, blk);
        return;
    }
    if (h->poison) {
        size_t n = blk->requested < blk->size ? blk->requested : blk->size;
        memset(hf_block_payload(blk), HF_POISON_BYTE, n);
    }
    hf_block_release(h, blk);
}

/* Validate pointer + report problems. Returns the block or NULL. */
static hf_block_t *validate_and_report(struct hf_heap *h, void *ptr, const char *op)
{
    hf_block_t *blk = NULL;
    const char *why = NULL;
    hf_ptr_status_t st = hf_validate_ptr(h, ptr, &blk, &why);
    switch (st) {
    case HF_PTR_OK:
        break;
    case HF_PTR_DOUBLE_FREE:
        h->c.double_frees++;
        hf_diag_double_free(h, op, ptr, blk, 0);
        errno = EINVAL;
        return NULL;
    case HF_PTR_STALE:
        h->c.double_frees++;
        hf_diag_double_free(h, op, ptr, NULL, 1);
        errno = EINVAL;
        return NULL;
    default:
        h->c.invalid_frees++;
        hf_diag_invalid_free(h, op, ptr, why);
        errno = EINVAL;
        return NULL;
    }
    uint64_t observed;
    if (!hf_canary_ok(blk, &observed)) {
        h->c.corruptions++;
        hf_diag_corruption(h, blk, hf_canary_expected(blk), observed);
    }
    return blk;
}

static void free_locked(struct hf_heap *h, void *ptr)
{
    if (!ptr) return;
    hf_block_t *blk = validate_and_report(h, ptr, "free");
    if (blk) release_validated(h, blk);
}

/* --------------------------------------------------------------- realloc */

static void *realloc_locked(struct hf_heap *h, void *ptr, size_t size, const char *file, unsigned line)
{
    if (!ptr) return alloc_locked(h, size, file, line);
    if (size == 0) { free_locked(h, ptr); return NULL; }

    hf_block_t *blk = validate_and_report(h, ptr, "realloc");
    if (!blk) return NULL;

    size_t need;
    if (!hf_payload_for_request(size, &need)) {
        h->c.alloc_failures++;
        errno = ENOMEM;
        return NULL;
    }
    size_t old_req = blk->requested;
    size_t old_size = blk->size;
    int in_place = 0;

    if (hf_block_is_mmap(blk)) {
        if (size >= h->mmap_threshold) {
            if (need <= blk->size && blk->size - need < hf_page_size()) {
                in_place = 1;                       /* still fits the mapping */
            } else {
                hf_block_t *nb = hf_heap_remap_block(h, blk, need);
                if (!nb) { h->c.alloc_failures++; return NULL; }
                blk = nb;
                in_place = 1;
            }
        }
        /* else: shrinking below the threshold -> move into the sbrk heap */
    } else if (size < h->mmap_threshold) {
        if (need <= blk->size) {                    /* shrink (or same) */
            hf_block_t *rem = hf_block_split(h, blk, need);
            if (rem) hf_block_release(h, rem);
            in_place = 1;
        } else {
            hf_block_t *n = blk->next;
            if (n && hf_block_is_free(n) && hf_blocks_adjacent(blk, n) &&
                blk->size + HF_BLOCK_HDR + n->size >= need) {
                hf_fl_remove(h, n);                 /* grow into the free neighbour */
                hf_block_absorb(h, blk, n);
                hf_block_t *rem = hf_block_split(h, blk, need);
                if (rem) hf_block_release(h, rem);
                in_place = 1;
            }
        }
    }

    if (in_place) {
        blk->requested = size;
        hf_canary_write(blk);
        hf_counters_t *c = &h->c;
        c->active_requested = c->active_requested - old_req + size;
        c->active_payload = c->active_payload - old_size + blk->size;
        if (size > old_req) c->bytes_requested_total += size - old_req;
        if (c->active_requested > c->peak_active_requested)
            c->peak_active_requested = c->active_requested;
        return hf_block_payload(blk);
    }

    /* Move: allocate new, copy, release old. The old block survives a
     * failure, as required by the C standard for realloc. */
    void *np = alloc_locked(h, size, file, line);
    if (!np) return NULL;
    memcpy(np, hf_block_payload(blk), old_req < size ? old_req : size);
    release_validated(h, blk);
    return np;
}

/* ---------------------------------------------------------- public API   */

void *hf_malloc_dbg(size_t size, const char *file, int line)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h);
    h->c.malloc_calls++;
    void *p = alloc_locked(h, size, file, (unsigned)line);
    paranoid_verify(h, "malloc");
    hf_heap_unlock(h);
    return p;
}

void *hf_calloc_dbg(size_t n, size_t size, const char *file, int line)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h);
    h->c.calloc_calls++;
    void *p = NULL;
    if (n != 0 && size > SIZE_MAX / n) {            /* n*size would overflow */
        h->c.alloc_failures++;
        errno = ENOMEM;
    } else {
        size_t total = n * size;
        p = alloc_locked(h, total, file, (unsigned)line);
        if (p) memset(p, 0, total);
    }
    paranoid_verify(h, "calloc");
    hf_heap_unlock(h);
    return p;
}

void *hf_realloc_dbg(void *ptr, size_t size, const char *file, int line)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h);
    h->c.realloc_calls++;
    void *p = realloc_locked(h, ptr, size, file, (unsigned)line);
    paranoid_verify(h, "realloc");
    hf_heap_unlock(h);
    return p;
}

void hf_free(void *ptr)
{
    if (!ptr) return;
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h);
    h->c.free_calls++;
    free_locked(h, ptr);
    paranoid_verify(h, "free");
    hf_heap_unlock(h);
}

void *hf_malloc(size_t size) { return hf_malloc_dbg(size, NULL, 0); }
void *hf_calloc(size_t n, size_t size) { return hf_calloc_dbg(n, size, NULL, 0); }
void *hf_realloc(void *ptr, size_t size) { return hf_realloc_dbg(ptr, size, NULL, 0); }

/* ---------------------------------------------------------- configuration */

void hf_set_strategy(hf_strategy_t s)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h); h->strategy = s; hf_heap_unlock(h);
}

hf_strategy_t hf_get_strategy(void)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h); hf_strategy_t s = h->strategy; hf_heap_unlock(h);
    return s;
}

const char *hf_strategy_name(hf_strategy_t s)
{
    switch (s) {
    case HF_STRATEGY_FIRST_FIT: return "first-fit";
    case HF_STRATEGY_BEST_FIT:  return "best-fit";
    case HF_STRATEGY_WORST_FIT: return "worst-fit";
    }
    return "unknown";
}

int hf_strategy_parse(const char *name, hf_strategy_t *out)
{
    if (!name || !out) return -1;
    if (!strcmp(name, "first-fit") || !strcmp(name, "first") || !strcmp(name, "ff")) *out = HF_STRATEGY_FIRST_FIT;
    else if (!strcmp(name, "best-fit") || !strcmp(name, "best") || !strcmp(name, "bf")) *out = HF_STRATEGY_BEST_FIT;
    else if (!strcmp(name, "worst-fit") || !strcmp(name, "worst") || !strcmp(name, "wf")) *out = HF_STRATEGY_WORST_FIT;
    else return -1;
    return 0;
}

void hf_set_mmap_threshold(size_t bytes)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h); h->mmap_threshold = bytes ? bytes : 1; hf_heap_unlock(h);
}

size_t hf_get_mmap_threshold(void)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h); size_t t = h->mmap_threshold; hf_heap_unlock(h);
    return t;
}

void hf_set_poison(int on)   { struct hf_heap *h = hf_heap(); hf_heap_lock(h); h->poison = on != 0; hf_heap_unlock(h); }
void hf_set_verbose(int on)  { struct hf_heap *h = hf_heap(); hf_heap_lock(h); h->verbose = on != 0; hf_heap_unlock(h); }
void hf_set_paranoid(int on) { struct hf_heap *h = hf_heap(); hf_heap_lock(h); h->paranoid = on != 0; hf_heap_unlock(h); }
void hf_set_heap_limit(size_t bytes) { struct hf_heap *h = hf_heap(); hf_heap_lock(h); h->heap_limit = bytes; hf_heap_unlock(h); }

void hf_reset(void)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h);
    hf_heap_reset(h);
    hf_heap_unlock(h);
}

size_t hf_trim(void)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h);
    size_t r = hf_heap_trim(h);
    hf_heap_unlock(h);
    return r;
}
