#include "test_util.h"
#include <stdint.h>

static void test_split_creates_remainder(void)
{
    void *a = hf_malloc(512), *g = hf_malloc(16);          /* hole will be 528 payload */
    hf_free(a);
    CHECK(info_of(a).size == 528);
    uint64_t sp = get_stats().c.splits;
    void *b = hf_malloc(100);                              /* needs 112 */
    CHECK(b == a);
    CHECK(get_stats().c.splits == sp + 1);
    CHECK(info_of(b).size == 112);
    hf_blockinfo_t rem = next_block_after(b);
    CHECK(rem.state == HF_BLOCK_FREE);
    CHECK(rem.size == 528 - 112 - HF_BLOCK_HDR);           /* = 336 */
    CHECK(rem.size == 336);
    hf_blockinfo_t fl[64];
    size_t total, n = hf_freelist_snapshot(fl, 64, &total);
    int on_list = 0;
    for (size_t i = 0; i < n; i++) if (fl[i].payload_addr == rem.payload_addr) on_list = 1;
    CHECK(on_list);                                        /* remainder is on the free list */
    CHECK(heap_ok());
    hf_free(b); hf_free(g);
}

static void test_no_split_below_threshold(void)
{
    void *x = hf_malloc(100), *g = hf_malloc(16);          /* x payload = 112 */
    hf_free(x);
    uint64_t sp = get_stats().c.splits;
    void *y = hf_malloc(40);                               /* needs 48: remainder < 0 */
    CHECK(y == x);
    CHECK(get_stats().c.splits == sp);
    CHECK(info_of(y).size == 112);                         /* whole block handed out */
    CHECK(heap_ok());
    hf_free(y); hf_free(g);
}

static void test_split_threshold_boundary(void)
{
    /* hole payload 304. need 160 -> remainder 304-160-80 = 64 == threshold -> split.
     * need 176 -> remainder 48 < 64 -> no split. */
    void *a = hf_malloc(290), *g = hf_malloc(16);
    hf_free(a);
    CHECK(info_of(a).size == 304);
    uint64_t sp = get_stats().c.splits;
    void *b = hf_malloc(152);                              /* 152+8 = 160 */
    CHECK(get_stats().c.splits == sp + 1);
    CHECK(next_block_after(b).size == 64);
    hf_free(b);                                            /* merges back into a 304 hole */
    CHECK(info_of(a).size >= 304);
    hf_free(g);
    hf_reset();
    a = hf_malloc(290); g = hf_malloc(16);
    hf_free(a);
    sp = get_stats().c.splits;
    b = hf_malloc(168);                                    /* 168+8 = 176 */
    CHECK(get_stats().c.splits == sp);
    CHECK(info_of(b).size == 304);
    CHECK(heap_ok());
    hf_free(b); hf_free(g);
}

static void test_first_alloc_splits_initial_chunk(void)
{
    void *p = hf_malloc(1000);
    hf_stats_t s = get_stats();
    CHECK(s.c.splits == 1 && s.free_blocks == 1 && s.alloc_blocks == 1);
    CHECK(s.heap_size == HF_HEAP_CHUNK);
    CHECK(s.free_bytes + s.alloc_payload + s.header_bytes == s.heap_size);   /* accounting adds up */
    hf_free(p);
}

int main(void)
{
    TEST_BEGIN("test_split");
    RUN(test_split_creates_remainder);
    RUN(test_no_split_below_threshold);
    RUN(test_split_threshold_boundary);
    RUN(test_first_alloc_splits_initial_chunk);
    TEST_END();
}
