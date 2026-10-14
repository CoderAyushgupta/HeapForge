#include "test_util.h"
#include <stdint.h>

static void test_realloc_null_is_malloc(void)
{
    char *p = hf_realloc(NULL, 64);
    CHECK(p != NULL && info_of(p).state == HF_BLOCK_ALLOC);
    CHECK(get_stats().c.active_allocs == 1);
    hf_free(p);
}

static void test_realloc_zero_frees(void)
{
    void *p = hf_malloc(64);
    CHECK(hf_realloc(p, 0) == NULL);
    CHECK(get_stats().c.active_allocs == 0);
    CHECK(heap_ok());
}

static void test_realloc_grow_moves_and_preserves(void)
{
    unsigned char *a = hf_malloc(100), *g = hf_malloc(100);   /* g blocks in-place growth */
    memset(a, 0xAB, 100);
    unsigned char *b = hf_realloc(a, 2000);
    CHECK(b != NULL && b != a);
    CHECK(pattern_ok(b, 100, 0xAB));
    CHECK(info_of(a).state == HF_BLOCK_FREE);                 /* old block was released */
    CHECK(heap_ok());
    hf_free(b); hf_free(g);
    CHECK(heap_ok());
}

static void test_realloc_grow_in_place(void)
{
    unsigned char *a = hf_malloc(100), *b = hf_malloc(100), *g = hf_malloc(100);
    memset(a, 0xCD, 100);
    hf_free(b);                                               /* free neighbour right after a */
    unsigned char *r = hf_realloc(a, 150);
    CHECK(r == a);                                            /* grew into the neighbour */
    CHECK(pattern_ok(r, 100, 0xCD));
    CHECK(info_of(r).requested == 150);
    CHECK(heap_ok());
    hf_free(r); hf_free(g);
}

static void test_realloc_shrink_in_place_and_releases_tail(void)
{
    unsigned char *a = hf_malloc(1000), *g = hf_malloc(16);
    memset(a, 0xEE, 1000);
    uint64_t sp = get_stats().c.splits;
    unsigned char *r = hf_realloc(a, 100);
    CHECK(r == a);
    CHECK(pattern_ok(r, 100, 0xEE));
    CHECK(get_stats().c.splits == sp + 1);                    /* surplus split off ... */
    CHECK(next_block_after(r).state == HF_BLOCK_FREE);        /* ... and freed */
    CHECK(get_stats().c.active_requested == 100 + 16);
    CHECK(heap_ok());
    hf_free(r); hf_free(g);
}

static void test_realloc_same_size(void)
{
    void *a = hf_malloc(200);
    CHECK(hf_realloc(a, 200) == a);
    hf_free(a);
    CHECK(heap_ok());
}

static void test_realloc_mmap_grow_and_shrink(void)
{
    unsigned char *p = hf_malloc(200000);
    memset(p, 0x5C, 200000);
    unsigned char *q = hf_realloc(p, 600000);
    CHECK(q != NULL && pattern_ok(q, 200000, 0x5C));
    CHECK(info_of(q).state == HF_BLOCK_MMAP);
    CHECK(get_stats().c.mremap_calls == 1);
    CHECK(heap_ok());
    unsigned char *r = hf_realloc(q, 40);                    /* shrinks below threshold: moves to sbrk heap */
    CHECK(r != NULL && pattern_ok(r, 40, 0x5C));
    CHECK(info_of(r).state == HF_BLOCK_ALLOC);
    CHECK(get_stats().mmap_blocks == 0);
    CHECK(heap_ok());
    hf_free(r);
}

static void test_realloc_heap_to_mmap(void)
{
    unsigned char *p = hf_malloc(1000);
    memset(p, 0x11, 1000);
    unsigned char *q = hf_realloc(p, 300000);
    CHECK(q && info_of(q).state == HF_BLOCK_MMAP && pattern_ok(q, 1000, 0x11));
    CHECK(info_of(p).state == HF_BLOCK_FREE || info_of(p).size == (size_t)-1);
    CHECK(heap_ok());
    hf_free(q);
}

static void test_realloc_invalid_pointer(void)
{
    int x;
    errno = 0;
    CHECK(hf_realloc(&x, 10) == NULL);
    CHECK(errno == EINVAL);
    CHECK(get_stats().c.invalid_frees == 1);
    CHECK(heap_ok());
}

static void test_realloc_failure_keeps_original(void)
{
    unsigned char *p = hf_malloc(100);
    memset(p, 0x9A, 100);
    errno = 0;
    CHECK(hf_realloc(p, SIZE_MAX) == NULL);
    CHECK(errno == ENOMEM);
    CHECK(info_of(p).state == HF_BLOCK_ALLOC && pattern_ok(p, 100, 0x9A));
    hf_free(p);
    CHECK(get_stats().c.active_allocs == 0);
    CHECK(heap_ok());
}

static void test_realloc_double_free_style_misuse(void)
{
    void *a = hf_malloc(100), *g = hf_malloc(100);
    hf_free(a);
    CHECK(hf_realloc(a, 200) == NULL);                        /* realloc of a freed block */
    CHECK(get_stats().c.double_frees == 1);
    CHECK(heap_ok());
    hf_free(g);
}

int main(void)
{
    TEST_BEGIN("test_realloc");
    RUN(test_realloc_null_is_malloc);
    RUN(test_realloc_zero_frees);
    RUN(test_realloc_grow_moves_and_preserves);
    RUN(test_realloc_grow_in_place);
    RUN(test_realloc_shrink_in_place_and_releases_tail);
    RUN(test_realloc_same_size);
    RUN(test_realloc_mmap_grow_and_shrink);
    RUN(test_realloc_heap_to_mmap);
    RUN(test_realloc_invalid_pointer);
    RUN(test_realloc_failure_keeps_original);
    RUN(test_realloc_double_free_style_misuse);
    TEST_END();
}
