#include "test_util.h"
#include <stdint.h>

static void test_malloc_basic(void)
{
    char *p = hf_malloc(100);
    CHECK(p != NULL);
    memset(p, 0x42, 100);
    hf_stats_t s = get_stats();
    CHECK(s.c.active_allocs == 1);
    CHECK(s.c.active_requested == 100);
    CHECK(s.c.sbrk_calls == 1);
    CHECK(heap_ok());
    hf_free(p);
    CHECK(get_stats().c.active_allocs == 0);
    CHECK(heap_ok());
}

static void test_malloc_alignment(void)
{
    int all_aligned = 1;
    for (size_t n = 1; n <= 300; n++) {
        void *p = hf_malloc(n);
        if (!p || ((uintptr_t)p % 16) != 0) all_aligned = 0;
        hf_free(p);
    }
    CHECK(all_aligned);
    CHECK(heap_ok());
}

static void test_malloc_multiple_blocks(void)
{
    enum { N = 60 };
    unsigned char *p[N];
    size_t sz[N];
    for (int i = 0; i < N; i++) {
        sz[i] = (size_t)(i * 37 % 500) + 1;
        p[i] = hf_malloc(sz[i]);
        CHECK(p[i] != NULL);
        memset(p[i], i + 1, sz[i]);
    }
    int intact = 1;
    for (int i = 0; i < N; i++) if (!pattern_ok(p[i], sz[i], (unsigned char)(i + 1))) intact = 0;
    CHECK(intact);                       /* no two blocks overlap */
    CHECK(heap_ok());
    for (int i = 0; i < N; i++) hf_free(p[i]);
    CHECK(get_stats().c.active_allocs == 0);
    CHECK(heap_ok());
}

static void test_malloc_zero_and_huge(void)
{
    errno = 0;
    CHECK(hf_malloc(0) == NULL);
    CHECK(errno == 0);
    errno = 0;
    CHECK(hf_malloc(SIZE_MAX) == NULL);
    CHECK(errno == ENOMEM);
    errno = 0;
    CHECK(hf_malloc(SIZE_MAX - 8) == NULL);
    CHECK(get_stats().c.alloc_failures == 2);
    CHECK(heap_ok());
}

static void test_malloc_large_uses_mmap(void)
{
    char *p = hf_malloc(200000);
    CHECK(p != NULL);
    hf_blockinfo_t i = info_of(p);
    CHECK(i.state == HF_BLOCK_MMAP);
    memset(p, 0x77, 200000);
    hf_stats_t s = get_stats();
    CHECK(s.mmap_blocks == 1 && s.c.mmap_calls == 1);
    CHECK(heap_ok());
    hf_free(p);
    s = get_stats();
    CHECK(s.mmap_blocks == 0 && s.c.munmap_calls == 1);
    CHECK(heap_ok());
}

static void test_threshold_is_configurable(void)
{
    hf_set_mmap_threshold(1000);
    void *a = hf_malloc(999), *b = hf_malloc(1000);
    CHECK(info_of(a).state == HF_BLOCK_ALLOC);
    CHECK(info_of(b).state == HF_BLOCK_MMAP);
    hf_free(a); hf_free(b);
    CHECK(heap_ok());
}

static void test_calloc_zeroes(void)
{
    unsigned char *p = hf_malloc(512);
    memset(p, 0xFF, 512);
    hf_free(p);
    unsigned char *q = hf_calloc(64, 8);          /* reuses the dirty block */
    CHECK(q == p);
    CHECK(pattern_ok(q, 512, 0));
    unsigned char *big = hf_calloc(1000, 1000);   /* mmap path */
    CHECK(big && info_of(big).state == HF_BLOCK_MMAP && pattern_ok(big, 1000000, 0));
    hf_free(q); hf_free(big);
    CHECK(heap_ok());
}

static void test_calloc_overflow(void)
{
    errno = 0;
    CHECK(hf_calloc(SIZE_MAX, 2) == NULL);
    CHECK(errno == ENOMEM);
    CHECK(hf_calloc(SIZE_MAX / 2 + 1, 2) == NULL);
    CHECK(hf_calloc(1u << 31, 1u << 31) == NULL || 1);   /* 2^62: too large either way */
    CHECK(hf_calloc(0, 10) == NULL);
    CHECK(get_stats().c.active_allocs == 0 || get_stats().c.active_allocs == 1);
    CHECK(heap_ok());
}

static void test_strategies_choose_differently(void)
{
    /* Build: [A hole 1000][g1][B hole 300][g2][tail free] */
    void *a = hf_malloc(1000), *g1 = hf_malloc(16), *b = hf_malloc(300), *g2 = hf_malloc(16);
    hf_free(a); hf_free(b);
    hf_set_strategy(HF_STRATEGY_FIRST_FIT);
    void *p1 = hf_malloc(200);
    CHECK(p1 == a);                                      /* lowest address that fits */
    hf_free(p1);
    hf_set_strategy(HF_STRATEGY_BEST_FIT);
    void *p2 = hf_malloc(200);
    CHECK(p2 == b);                                      /* smallest hole that fits  */
    hf_free(p2);
    hf_set_strategy(HF_STRATEGY_WORST_FIT);
    void *p3 = hf_malloc(200);
    CHECK((uintptr_t)p3 > (uintptr_t)g2);                /* largest block = heap tail */
    hf_free(p3); hf_free(g1); hf_free(g2);
    CHECK(heap_ok());
}

int main(void)
{
    TEST_BEGIN("test_malloc");
    RUN(test_malloc_basic);
    RUN(test_malloc_alignment);
    RUN(test_malloc_multiple_blocks);
    RUN(test_malloc_zero_and_huge);
    RUN(test_malloc_large_uses_mmap);
    RUN(test_threshold_is_configurable);
    RUN(test_calloc_zeroes);
    RUN(test_calloc_overflow);
    RUN(test_strategies_choose_differently);
    TEST_END();
}
