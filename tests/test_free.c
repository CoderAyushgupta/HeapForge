#include "test_util.h"
#include <stdint.h>

static void test_free_null(void)
{
    hf_free(NULL);
    hf_stats_t s = get_stats();
    CHECK(s.c.total_frees == 0 && s.c.invalid_frees == 0);
}

static void test_free_reuses_block(void)
{
    void *a = hf_malloc(100), *g = hf_malloc(100);
    hf_free(a);
    void *b = hf_malloc(100);
    CHECK(b == a);
    hf_free(b); hf_free(g);
    CHECK(heap_ok());
}

static void test_invalid_pointers_are_rejected(void)
{
    char *p = hf_malloc(100);
    memset(p, 0x33, 100);
    int on_stack = 0;
    uint64_t before = get_stats().c.invalid_frees;
    hf_free(&on_stack);                         /* foreign pointer          */
    hf_free(p + 1);                             /* misaligned               */
    hf_free(p + 16);                            /* aligned interior pointer */
    hf_free((void *)(uintptr_t)0x10);           /* wild address             */
    hf_free((void *)(uintptr_t)0xdeadbeef0);    /* wild address, aligned    */
    CHECK(get_stats().c.invalid_frees - before == 5);
    CHECK(pattern_ok(p, 100, 0x33));            /* live block untouched     */
    CHECK(get_stats().c.active_allocs == 1);
    CHECK(heap_ok());
    hf_free(p);
    CHECK(get_stats().c.active_allocs == 0);
}

static void test_double_free_detected(void)
{
    void *a = hf_malloc(128), *b = hf_malloc(128), *c = hf_malloc(128);
    hf_free(b);                                 /* neighbours allocated: stays a free block */
    hf_free(b);
    hf_stats_t s = get_stats();
    CHECK(s.c.double_frees == 1);
    CHECK(s.c.total_frees == 1);                /* the second call changed nothing */
    CHECK(heap_ok());
    hf_free(a); hf_free(c);
    CHECK(heap_ok());
}

static void test_double_free_after_coalescing(void)
{
    void *a = hf_malloc(100), *b = hf_malloc(100), *g = hf_malloc(100);
    hf_free(a);
    hf_free(b);                                 /* b's header is absorbed into a */
    hf_free(b);
    CHECK(get_stats().c.double_frees == 1);     /* reported as STALE pointer     */
    CHECK(heap_ok());
    hf_free(g);
}

static void test_free_mmap_block(void)
{
    void *p = hf_malloc(300000);
    hf_free(p);
    hf_stats_t s = get_stats();
    CHECK(s.c.munmap_calls == 1 && s.mmap_blocks == 0);
    hf_free(p);                                 /* mapping is gone: must not crash */
    CHECK(get_stats().c.invalid_frees == 1);
    CHECK(heap_ok());
}

static void test_allocation_failure_is_clean(void)
{
    void *warm = hf_malloc(1);
    hf_free(warm);
    hf_set_heap_limit(get_stats().heap_size);   /* no further sbrk growth */
    void *p[8];
    int fails = 0, n = 0;
    for (int i = 0; i < 8; i++) {
        errno = 0;
        p[i] = hf_malloc(60000);
        if (!p[i]) { fails++; CHECK(errno == ENOMEM); } else n++;
    }
    CHECK(n >= 1 && fails >= 1);
    CHECK(get_stats().c.alloc_failures == (uint64_t)fails);
    CHECK(heap_ok());
    for (int i = 0; i < 8; i++) hf_free(p[i]);
    hf_set_heap_limit(0);
    CHECK(get_stats().c.active_allocs == 0);
    CHECK(heap_ok());
}

int main(void)
{
    TEST_BEGIN("test_free");
    RUN(test_free_null);
    RUN(test_free_reuses_block);
    RUN(test_invalid_pointers_are_rejected);
    RUN(test_double_free_detected);
    RUN(test_double_free_after_coalescing);
    RUN(test_free_mmap_block);
    RUN(test_allocation_failure_is_clean);
    TEST_END();
}
