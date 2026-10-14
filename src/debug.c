/*
 * debug.c - Debug / Safety Engine: diagnostics, consistency checker and
 * leak report. Every message is produced from inspected block metadata.
 */
#include "debug.h"
#include "free_list.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------------------------------------- diagnostics */

static int use_color(void)
{
    return isatty(STDERR_FILENO) && getenv("NO_COLOR") == NULL;
}

static void banner_open(FILE *f, const char *error)
{
    const char *r = use_color() ? "\033[1;31m" : "";
    const char *z = use_color() ? "\033[0m" : "";
    fprintf(f, "================================================\n");
    fprintf(f, "HEAPFORGE DIAGNOSTIC\n");
    fprintf(f, "================================================\n");
    fprintf(f, "%sERROR: %s%s\n\n", r, error, z);
}

static void banner_close(FILE *f)
{
    fprintf(f, "================================================\n");
    fflush(f);
}

void hf_diag_double_free(struct hf_heap *h, const char *op, void *ptr, const hf_block_t *blk, int stale)
{
    if (!h->verbose) return;
    FILE *f = stderr;
    banner_open(f, "DOUBLE FREE DETECTED");
    fprintf(f, "Operation:\n%s\n\nPointer:\n%p\n\n", op, ptr);
    if (stale) {
        fprintf(f, "State:\nSTALE - the block's header was merged into a neighbouring\n"
                   "free block when it was freed (coalescing)\n\n");
    } else {
        fprintf(f, "Block size:\n%zu bytes\n\nState:\nALREADY FREE\n\n", blk->size);
    }
    fprintf(f, "Recommendation:\nverify ownership/lifetime of the allocation\n");
    banner_close(f);
}

void hf_diag_invalid_free(struct hf_heap *h, const char *op, void *ptr, const char *why)
{
    if (!h->verbose) return;
    FILE *f = stderr;
    banner_open(f, "INVALID POINTER");
    fprintf(f, "Operation:\n%s\n\nPointer:\n%p\n\nReason:\n%s\n\n", op, ptr, why ? why : "unknown");
    fprintf(f, "Recommendation:\nonly pass pointers returned by hf_malloc/hf_calloc/hf_realloc\n");
    banner_close(f);
}

void hf_diag_corruption(struct hf_heap *h, const hf_block_t *blk, uint64_t expected, uint64_t observed)
{
    if (!h->verbose) return;
    FILE *f = stderr;
    banner_open(f, "HEAP CORRUPTION DETECTED");
    fprintf(f, "Block:\n%p (allocation id #%llu", (const void *)((const char *)blk + HF_BLOCK_HDR),
            (unsigned long long)blk->seq);
    if (blk->file) fprintf(f, ", allocated at %s:%u", blk->file, blk->line);
    fprintf(f, ")\n\nRequested size:\n%zu bytes\n\n", blk->requested);
    fprintf(f, "Expected guard:\n0x%016llx\n\nObserved guard:\n0x%016llx\n\n",
            (unsigned long long)expected, (unsigned long long)observed);
    fprintf(f, "Possible cause:\nbuffer overrun / overwrite just past the end of the block\n");
    banner_close(f);
}

/* ---------------------------------------------------- consistency check */


static void violate(hf_check_report_t *r, const char *fmt, ...)
{
    r->violations++;
    r->ok = 0;
    if (r->messages_stored < HF_CHECK_MAX_MSG) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(r->messages[r->messages_stored], HF_CHECK_MSG_LEN, fmt, ap);
        va_end(ap);
        r->messages_stored++;
    }
}

int hf_check_locked(struct hf_heap *h, hf_check_report_t *out)
{
    hf_check_report_t local;
    hf_check_report_t *r = out ? out : &local;
    memset(r, 0, sizeof *r);
    r->ok = 1;

    size_t limit = h->managed_bytes / (HF_BLOCK_HDR + HF_MIN_PAYLOAD) + 4;
    size_t sum_managed = 0, sum_requested = 0, sum_payload = 0;
    hf_block_t *prev = NULL;
    int broken = 0;

    /* --- physical list of the sbrk heap ---------------------------------- */
    for (hf_block_t *b = h->head; b; b = b->next) {
        if (r->blocks_checked >= limit) {
            violate(r, "block list longer than the heap can hold: cycle suspected");
            broken = 1;
            break;
        }
        if (!hf_node_plausible(h, b)) {
            violate(r, "block pointer %p is outside the heap or misaligned", (void *)b);
            broken = 1;
            break;
        }
        r->blocks_checked++;
        if (b->magic != HF_MAGIC_ALLOC && b->magic != HF_MAGIC_FREE) {
            violate(r, "block %p: bad header magic 0x%08x (header overwritten?)", (void *)b, b->magic);
            broken = 1;
            break;
        }
        int is_free = b->magic == HF_MAGIC_FREE;
        if (is_free != ((b->flags & HF_FLAG_FREE) != 0))
            violate(r, "block %p: magic and FREE flag disagree", (void *)b);
        if ((b->size & (HF_ALIGNMENT - 1)) != 0 || b->size < HF_MIN_PAYLOAD)
            violate(r, "block %p: invalid size %zu", (void *)b, b->size);
        if ((uintptr_t)b + HF_BLOCK_HDR + b->size > (uintptr_t)h->end) {
            violate(r, "block %p: extends past the end of the heap", (void *)b);
            broken = 1;
            break;
        }
        if (b->prev != prev)
            violate(r, "block %p: prev link %p != physical predecessor %p", (void *)b, (void *)b->prev, (void *)prev);
        if (prev && (uintptr_t)b < (uintptr_t)hf_block_end(prev))
            violate(r, "block %p overlaps its predecessor %p", (void *)b, (void *)prev);
        sum_managed += HF_BLOCK_HDR + b->size;

        if (is_free) {
            r->free_blocks++;
            if (prev && hf_block_is_free(prev) && hf_blocks_adjacent(prev, b))
                violate(r, "adjacent free blocks %p and %p were not coalesced", (void *)prev, (void *)b);
        } else {
            r->alloc_blocks++;
            sum_payload += b->size;
            sum_requested += b->requested;
            if (b->requested + HF_CANARY_SIZE > b->size) {
                violate(r, "block %p: requested %zu + canary exceeds payload %zu", (void *)b, b->requested, b->size);
            } else {
                uint64_t obs;
                if (!hf_canary_ok(b, &obs))
                    violate(r, "block %p (id #%llu): CANARY CORRUPT expected 0x%016llx observed 0x%016llx",
                            (void *)hf_block_payload(b), (unsigned long long)b->seq,
                            (unsigned long long)hf_canary_expected(b), (unsigned long long)obs);
            }
        }
        prev = b;
    }
    if (!broken) {
        if (prev != h->tail) violate(r, "tail pointer %p != last block %p", (void *)h->tail, (void *)prev);
        if (sum_managed != h->managed_bytes)
            violate(r, "managed_bytes %zu != sum of block sizes %zu", h->managed_bytes, sum_managed);
    }

    /* --- free list -------------------------------------------------------- */
    hf_block_t *lastf = NULL;
    size_t nodes = 0;
    for (hf_block_t *f = h->free_head; f; f = f->free_next) {
        if (nodes >= limit) { violate(r, "free list too long: cycle suspected"); break; }
        if (!hf_node_plausible(h, f)) { violate(r, "free-list node %p outside heap", (void *)f); break; }
        nodes++;
        if (!hf_block_is_free(f)) violate(r, "free-list node %p is not marked free", (void *)f);
        if (f->free_prev != lastf) violate(r, "free-list node %p: bad free_prev", (void *)f);
        if (lastf && (uintptr_t)lastf >= (uintptr_t)f) violate(r, "free list not in address order at %p", (void *)f);
        lastf = f;
    }
    r->free_list_nodes = nodes;
    if (!broken && nodes != r->free_blocks)
        violate(r, "free list has %zu nodes but the heap holds %zu free blocks", nodes, r->free_blocks);

    /* --- mmap list --------------------------------------------------------- */
    size_t mmap_bytes = 0, mcount = 0;
    hf_block_t *mprev = NULL;
    for (hf_block_t *m = h->mmap_head; m; m = m->next) {
        if (mcount++ > (size_t)h->c.mmap_calls + 4) { violate(r, "mmap list cycle suspected"); break; }
        if (m->magic != HF_MAGIC_ALLOC || !hf_block_is_mmap(m)) {
            violate(r, "mmap block %p: bad magic/flags", (void *)m);
            break;
        }
        if (m->prev != mprev) violate(r, "mmap block %p: bad prev link", (void *)m);
        if (m->requested + HF_CANARY_SIZE > m->size) {
            violate(r, "mmap block %p: requested exceeds payload", (void *)m);
        } else {
            uint64_t obs;
            if (!hf_canary_ok(m, &obs))
                violate(r, "mmap block %p (id #%llu): CANARY CORRUPT expected 0x%016llx observed 0x%016llx",
                        (void *)hf_block_payload(m), (unsigned long long)m->seq,
                        (unsigned long long)hf_canary_expected(m), (unsigned long long)obs);
        }
        r->mmap_blocks++;
        sum_payload += m->size;
        sum_requested += m->requested;
        mmap_bytes += HF_BLOCK_HDR + m->size;
        mprev = m;
    }

    /* --- bookkeeping vs reality -------------------------------------------- */
    if (!broken) {
        if (h->c.active_allocs != r->alloc_blocks + r->mmap_blocks)
            violate(r, "active_allocs counter %llu != live blocks found %zu",
                    (unsigned long long)h->c.active_allocs, r->alloc_blocks + r->mmap_blocks);
        if (h->c.active_requested != sum_requested)
            violate(r, "active_requested counter %llu != sum over blocks %zu",
                    (unsigned long long)h->c.active_requested, sum_requested);
        if (h->c.active_payload != sum_payload)
            violate(r, "active_payload counter %llu != sum over blocks %zu",
                    (unsigned long long)h->c.active_payload, sum_payload);
        if (h->c.mmap_active_bytes != mmap_bytes)
            violate(r, "mmap_active_bytes counter %llu != sum of mappings %zu",
                    (unsigned long long)h->c.mmap_active_bytes, mmap_bytes);
    }
    return r->ok;
}

int hf_check_heap(hf_check_report_t *rep)
{
    struct hf_heap *h = hf_heap();
    hf_heap_lock(h);
    int ok = hf_check_locked(h, rep);
    hf_heap_unlock(h);
    return ok;
}

void hf_check_print(FILE *out, const hf_check_report_t *r)
{
    fprintf(out, "Heap consistency check: %s\n", r->ok ? "PASS" : "FAIL");
    fprintf(out, "  blocks checked   : %zu (allocated %zu, free %zu)\n", r->blocks_checked, r->alloc_blocks, r->free_blocks);
    fprintf(out, "  free-list nodes  : %zu\n", r->free_list_nodes);
    fprintf(out, "  mmap blocks      : %zu\n", r->mmap_blocks);
    fprintf(out, "  violations       : %zu\n", r->violations);
    for (size_t i = 0; i < r->messages_stored; i++)
        fprintf(out, "  [!] %s\n", r->messages[i]);
    if (r->violations > r->messages_stored)
        fprintf(out, "  ... and %zu more\n", r->violations - r->messages_stored);
}

/* ------------------------------------------------------------ leak report */

size_t hf_leak_report(FILE *out, size_t max_rows, hf_leak_summary_t *summary)
{
    struct hf_heap *h = hf_heap();
    hf_leak_summary_t s;
    hf_heap_lock(h);
    s.total_allocs = h->c.total_allocs;
    s.total_frees = h->c.total_frees;
    s.active_allocs = h->c.active_allocs;
    s.active_requested_bytes = h->c.active_requested;
    s.active_payload_bytes = h->c.active_payload;
    s.mmap_blocks = h->c.mmap_active_blocks;

    fprintf(out, "Leak report (live allocations at this moment)\n");
    fprintf(out, "  total allocations : %llu\n  total frees       : %llu\n  active allocations: %llu\n"
                 "  active bytes      : %llu requested (%llu payload capacity)\n",
            (unsigned long long)s.total_allocs, (unsigned long long)s.total_frees,
            (unsigned long long)s.active_allocs, (unsigned long long)s.active_requested_bytes,
            (unsigned long long)s.active_payload_bytes);

    size_t rows = 0, live = 0, limit = h->managed_bytes / (HF_BLOCK_HDR + HF_MIN_PAYLOAD) + 4, n = 0;
    if (s.active_allocs) {
        fprintf(out, "  %-6s %-16s %10s  %-5s %s\n", "ID", "ADDRESS", "BYTES", "FROM", "ALLOCATED AT");
    }
    for (int pass = 0; pass < 2; pass++) {
        hf_block_t *b = pass == 0 ? h->head : h->mmap_head;
        for (; b && n++ < limit + 64; b = b->next) {
            if (pass == 0 && !hf_node_plausible(h, b)) break;
            if (pass == 0 && hf_block_is_free(b)) continue;
            live++;
            if (rows < max_rows) {
                char where[96];
                if (b->file) snprintf(where, sizeof where, "%s:%u", b->file, b->line);
                else snprintf(where, sizeof where, "(no call site: plain hf_malloc)");
                fprintf(out, "  #%-5llu %-16p %10zu  %-5s %s\n", (unsigned long long)b->seq,
                        (void *)hf_block_payload(b), b->requested, pass == 0 ? "sbrk" : "mmap", where);
                rows++;
            }
        }
    }
    if (live > rows) fprintf(out, "  ... and %zu more live blocks\n", live - rows);
    if (live == 0) fprintf(out, "  no live allocations: every block was freed\n");
    hf_heap_unlock(h);
    if (summary) *summary = s;
    return live;
}
