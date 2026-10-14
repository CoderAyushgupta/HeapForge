#include "test_util.h"
#include <stdint.h>

static void test_canary_detected_on_free(void)
{
    char *p = hf_malloc(64), *g = hf_malloc(64);
    p[64] = 'X';                                  /* one byte past the end */
    uint64_t before = get_stats().c.corruptions;
    hf_free(p);
    CHECK(get_stats().c.corruptions == before + 1);
    CHECK(heap_ok());                             /* the block was still released safely */
    hf_free(g);
}

static void test_canary_intact_means_no_report(void)
{
    char *p = hf_malloc(64);
    memset(p, 'A', 64);                           /* exactly the requested size */
    hf_free(p);
    CHECK(get_stats().c.corruptions == 0);
}

static void test_checker_reports_canary_violation(void)
{
    char *p = hf_malloc(100), *g = hf_malloc(100);
    CHECK(heap_ok());
    p[100] = 0;                                   /* overwrite first canary byte */
    hf_check_report_t r;
    CHECK(hf_check_heap(&r) == 0);
    CHECK(r.violations >= 1 && strstr(r.messages[0], "CANARY") != NULL);
    hf_free(p);                                   /* removes the corrupt block */
    CHECK(heap_ok());
    hf_free(g);
}

static void test_checker_detects_damaged_headers(void)
{
    void *a = hf_malloc(100), *b = hf_malloc(100), *g = hf_malloc(100);
    hf_block_t *hb = hf_payload_block(b);
    uint32_t saved = hb->magic;
    hb->magic = 0x12345678;
    CHECK(hf_check_heap(NULL) == 0);
    hb->magic = saved;
    CHECK(heap_ok());

    hf_free(b);                                   /* now b is on the free list */
    uint32_t sf = hb->flags;
    hb->flags = 0;                                /* magic says free, flag says allocated */
    CHECK(hf_check_heap(NULL) == 0);
    hb->flags = sf;
    CHECK(heap_ok());

    hf_block_t *hg = hf_payload_block(g);
    size_t ssz = hg->size;
    hg->size += 16;                               /* size no longer matches layout */
    CHECK(hf_check_heap(NULL) == 0);
    hg->size = ssz;
    CHECK(heap_ok());
    hf_free(a); hf_free(g);
}

static void test_checker_detects_counter_mismatch(void)
{
    void *p = hf_malloc(100);
    struct hf_heap *h = hf_heap();
    h->c.active_allocs += 1;                      /* make bookkeeping lie */
    hf_check_report_t r;
    CHECK(hf_check_heap(&r) == 0);
    h->c.active_allocs -= 1;
    CHECK(heap_ok());
    hf_free(p);
}

static void test_leak_report_counts_live_blocks(void)
{
    void *a = HF_MALLOC(10), *b = HF_MALLOC(20), *c = HF_MALLOC(300000);
    FILE *f = tmpfile();
    hf_leak_summary_t s;
    size_t live = hf_leak_report(f, 10, &s);
    CHECK(live == 3 && s.active_allocs == 3 && s.active_requested_bytes == 300030);
    CHECK(s.total_allocs == 3 && s.total_frees == 0);
    rewind(f);
    char line[256];
    int saw_site = 0;
    while (fgets(line, sizeof line, f)) if (strstr(line, "test_debug.c:")) saw_site = 1;
    CHECK(saw_site);                              /* real __FILE__:__LINE__ captured */
    fclose(f);
    hf_free(a); hf_free(b);
    f = tmpfile();
    live = hf_leak_report(f, 10, &s);
    CHECK(live == 1 && s.active_allocs == 1 && s.total_frees == 2);
    hf_free(c);
    live = hf_leak_report(f, 10, &s);
    CHECK(live == 0 && s.active_allocs == 0);
    fclose(f);
}

static void test_poison_on_free(void)
{
    unsigned char *p = hf_malloc(64), *g = hf_malloc(64);
    memset(p, 'A', 64);
    hf_free(p);
    volatile unsigned char *v = p;                 /* inspect freed heap memory */
    int all = 1;
    for (int i = 0; i < 64; i++) if (v[i] != HF_POISON_BYTE) all = 0;
    CHECK(all);
    hf_free(g);
}

static void test_block_info_rejects_garbage(void)
{
    hf_blockinfo_t i;
    int x;
    CHECK(hf_block_info(&x, &i) == -1);
    CHECK(hf_block_info(NULL, &i) == -1);
    void *p = hf_malloc(50);
    CHECK(hf_block_info(p, &i) == 0 && i.requested == 50 && i.state == HF_BLOCK_ALLOC);
    CHECK(hf_block_info((char *)p + 16, &i) == -1);
    hf_free(p);
}

static void test_paranoid_mode_random_ops(void)
{
    hf_set_paranoid(1);
    void *slot[48] = {0};
    uint64_t x = 88172645463325252ull;
    for (int i = 0; i < 1500; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        int k = (int)(x % 48);
        size_t sz = (size_t)((x >> 20) % 700) + 1;
        if (!slot[k]) slot[k] = hf_malloc(sz);
        else if ((x >> 40) & 1) { hf_free(slot[k]); slot[k] = NULL; }
        else { void *q = hf_realloc(slot[k], sz); if (q) slot[k] = q; }
    }
    for (int k = 0; k < 48; k++) hf_free(slot[k]);
    hf_set_paranoid(0);
    CHECK(get_stats().c.paranoid_checks >= 1500);  /* a full check ran after every call */
    CHECK(get_stats().c.active_allocs == 0);
    CHECK(heap_ok());
}

int main(void)
{
    TEST_BEGIN("test_debug");
    RUN(test_canary_detected_on_free);
    RUN(test_canary_intact_means_no_report);
    RUN(test_checker_reports_canary_violation);
    RUN(test_checker_detects_damaged_headers);
    RUN(test_checker_detects_counter_mismatch);
    RUN(test_leak_report_counts_live_blocks);
    RUN(test_poison_on_free);
    RUN(test_block_info_rejects_garbage);
    RUN(test_paranoid_mode_random_ops);
    TEST_END();
}
