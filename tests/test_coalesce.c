#include "test_util.h"
#include <stdint.h>

#define PAY 112    /* payload of a 100-byte request */

static void four(void **a, void **b, void **c, void **d)
{
    *a = hf_malloc(100); *b = hf_malloc(100); *c = hf_malloc(100); *d = hf_malloc(100);
}

static void test_setup_is_contiguous(void)
{
    void *a, *b, *c, *d;
    four(&a, &b, &c, &d);
    CHECK(adjacent(a, b) && adjacent(b, c) && adjacent(c, d));
    hf_free(a); hf_free(b); hf_free(c); hf_free(d);
}

static void test_no_coalesce_between_live_neighbours(void)
{
    void *a, *b, *c, *d;
    four(&a, &b, &c, &d);
    uint64_t co = get_stats().c.coalesces;
    hf_free(b);
    CHECK(get_stats().c.coalesces == co);
    CHECK(get_stats().free_blocks == 2);                  /* hole + tail */
    CHECK(heap_ok());
    hf_free(a); hf_free(c); hf_free(d);
}

static void test_coalesce_with_previous(void)
{
    void *a, *b, *c, *d;
    four(&a, &b, &c, &d);
    hf_free(a);
    uint64_t co = get_stats().c.coalesces;
    hf_free(b);                                            /* previous (a) is free */
    CHECK(get_stats().c.coalesces == co + 1);
    CHECK(info_of(a).size == 2 * PAY + HF_BLOCK_HDR);
    CHECK(heap_ok());
    void *r = hf_malloc(2 * PAY - 8);                      /* merged block is usable */
    CHECK(r == a);
    hf_free(r); hf_free(c); hf_free(d);
}

static void test_coalesce_with_next(void)
{
    void *a, *b, *c, *d;
    four(&a, &b, &c, &d);
    hf_free(b);
    uint64_t co = get_stats().c.coalesces;
    hf_free(a);                                            /* next (b) is free */
    CHECK(get_stats().c.coalesces == co + 1);
    CHECK(info_of(a).size == 2 * PAY + HF_BLOCK_HDR);
    CHECK(heap_ok());
    hf_free(c); hf_free(d);
}

static void test_coalesce_both_sides(void)
{
    void *a, *b, *c, *d;
    four(&a, &b, &c, &d);
    hf_free(a); hf_free(c);
    uint64_t co = get_stats().c.coalesces;
    hf_free(b);                                            /* previous AND next free */
    CHECK(get_stats().c.coalesces == co + 2);
    CHECK(info_of(a).size == 3 * PAY + 2 * HF_BLOCK_HDR);
    CHECK(get_stats().free_blocks == 2);                   /* merged block + tail */
    CHECK(heap_ok());
    hf_free(d);                                            /* d merges with merged block AND tail */
    CHECK(get_stats().free_blocks == 1);
    CHECK(heap_ok());
}

static void test_everything_merges_back_to_one_block(void)
{
    void *p[40];
    for (int i = 0; i < 40; i++) p[i] = hf_malloc((size_t)(30 + i * 7));
    for (int i = 0; i < 40; i += 2) hf_free(p[i]);
    for (int i = 39; i > 0; i -= 2) hf_free(p[i]);         /* odd ones in reverse order */
    hf_stats_t s = get_stats();
    CHECK(s.free_blocks == 1 && s.alloc_blocks == 0);
    CHECK(s.free_bytes + HF_BLOCK_HDR == s.heap_size);
    CHECK(heap_ok());
}

int main(void)
{
    TEST_BEGIN("test_coalesce");
    RUN(test_setup_is_contiguous);
    RUN(test_no_coalesce_between_live_neighbours);
    RUN(test_coalesce_with_previous);
    RUN(test_coalesce_with_next);
    RUN(test_coalesce_both_sides);
    RUN(test_everything_merges_back_to_one_block);
    TEST_END();
}
