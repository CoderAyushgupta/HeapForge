#include "test_util.h"
#include <math.h>
#include <stdint.h>
#include <unistd.h>

static uint64_t rng = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void) { rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27; return rng * 2685821657736338717ull; }

typedef struct { unsigned char *p; size_t n; unsigned char pat; } slot_t;

static void random_workload(int ops)
{
    enum { S = 200 };
    slot_t s[S];
    memset(s, 0, sizeof s);
    int bad = 0;
    for (int i = 0; i < ops; i++) {
        uint64_t r = rnd();
        slot_t *x = &s[r % S];
        size_t n = (size_t)((r >> 16) % 3000) + 1;
        if (!x->p) {
            x->p = hf_malloc(n); x->n = n; x->pat = (unsigned char)(r >> 50) | 1;
            if (x->p) memset(x->p, x->pat, n);
        } else if (((r >> 40) & 3) == 0) {
            unsigned char *q = hf_realloc(x->p, n);
            if (q) {
                size_t keep = n < x->n ? n : x->n;
                if (!pattern_ok(q, keep, x->pat)) bad++;
                x->p = q; x->n = n; memset(q, x->pat, n);
            }
        } else {
            if (!pattern_ok(x->p, x->n, x->pat)) bad++;
            hf_free(x->p); x->p = NULL;
        }
        if (i % 1000 == 0 && !heap_ok()) bad++;
    }
    for (int k = 0; k < S; k++) if (s[k].p) { if (!pattern_ok(s[k].p, s[k].n, s[k].pat)) bad++; hf_free(s[k].p); }
    CHECK(bad == 0);
    CHECK(get_stats().c.active_allocs == 0);
    CHECK(get_stats().free_blocks == 1);               /* everything coalesced back */
    CHECK(heap_ok());
}

static void test_random_first_fit(void) { hf_set_strategy(HF_STRATEGY_FIRST_FIT); random_workload(20000); }
static void test_random_best_fit(void)  { hf_set_strategy(HF_STRATEGY_BEST_FIT);  random_workload(20000); }
static void test_random_worst_fit(void) { hf_set_strategy(HF_STRATEGY_WORST_FIT); random_workload(20000); }

static void test_fragmentation_metrics_match_independent_computation(void)
{
    void *p[40];
    for (int i = 0; i < 40; i++) p[i] = hf_malloc(100 + (size_t)i * 10);
    for (int i = 0; i < 40; i += 2) hf_free(p[i]);     /* leave holes between live blocks */

    hf_stats_t s = get_stats();
    hf_blockinfo_t b[256];
    size_t total, n = hf_blocks_snapshot(b, 256, &total);
    size_t free_bytes = 0, largest = 0, nfree = 0, heap = 0, req = 0, alloc_payload = 0;
    for (size_t i = 0; i < n; i++) {
        heap += b[i].size + HF_BLOCK_HDR;
        if (b[i].state == HF_BLOCK_FREE) { free_bytes += b[i].size; nfree++; if (b[i].size > largest) largest = b[i].size; }
        else { req += b[i].requested; alloc_payload += b[i].size; }
    }
    CHECK(s.free_blocks == nfree && s.free_bytes == free_bytes && s.largest_free == largest);
    CHECK(s.heap_size == heap);
    CHECK(fabs(s.external_frag - (1.0 - (double)largest / (double)free_bytes)) < 1e-12);
    CHECK(fabs(s.utilization - (double)req / (double)heap) < 1e-12);
    CHECK(fabs(s.internal_frag - (double)(alloc_payload - req) / (double)alloc_payload) < 1e-12);
    CHECK(s.external_frag > 0.0 && s.external_frag < 1.0);
    CHECK(nfree >= 20);
    for (int i = 1; i < 40; i += 2) hf_free(p[i]);
    s = get_stats();
    CHECK(s.external_frag == 0.0 && s.free_blocks == 1);
}

static void test_reset_restores_single_free_block(void)
{
    for (int i = 0; i < 10; i++) (void)hf_malloc(500);
    void *big = hf_malloc(400000);
    (void)big;
    hf_reset();                                        /* blocks are discarded, not freed one by one */
    hf_stats_t s = get_stats();
    CHECK(s.c.active_allocs == 0 && s.free_blocks == 1 && s.mmap_blocks == 0 && s.alloc_blocks == 0);
    CHECK(s.c.total_allocs == 0);
    CHECK(heap_ok());
}

static void test_trim_returns_memory_to_kernel(void)
{
    void *p = hf_malloc(300);
    void *q = hf_malloc(HF_MMAP_THRESHOLD_DEFAULT - 100);     /* forces a second 128 KiB chunk */
    hf_free(q); hf_free(p);
    size_t before = get_stats().heap_size;
    void *brk_before = sbrk(0);
    size_t r = hf_trim();
    if (r > 0) {
        CHECK(get_stats().heap_size == before - r);
        CHECK((char *)sbrk(0) == (char *)brk_before - r);     /* the kernel's break really moved */
        CHECK(get_stats().c.trim_bytes == r);
    } else {
        printf("    note: break not at heap end (libc allocated above us); trim skipped\n");
    }
    CHECK(heap_ok());
}

int main(void)
{
    TEST_BEGIN("test_stress");
    RUN(test_random_first_fit);
    RUN(test_random_best_fit);
    RUN(test_random_worst_fit);
    RUN(test_fragmentation_metrics_match_independent_computation);
    RUN(test_reset_restores_single_free_block);
    RUN(test_trim_returns_memory_to_kernel);
    TEST_END();
}
