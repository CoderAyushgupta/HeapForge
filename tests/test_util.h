/* test_util.h - tiny test harness shared by all test programs. */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "heapforge.h"
#include "block.h"
#include "heap.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;

#define CHECK(cond) do { if (cond) { g_pass++; } else { g_fail++; \
    printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test_reset_state(void)
{
    hf_set_heap_limit(0);
    hf_set_paranoid(0);
    hf_set_verbose(0);          /* tests inspect counters, not stderr */
    hf_set_poison(1);
    hf_set_strategy(HF_STRATEGY_FIRST_FIT);
    hf_set_mmap_threshold(HF_MMAP_THRESHOLD_DEFAULT);
    hf_reset();
}

#define RUN(fn) do { int before_ = g_fail; test_reset_state(); fn(); \
    printf("  [%s] %s\n", g_fail == before_ ? "PASS" : "FAIL", #fn); } while (0)

#define TEST_BEGIN(name) printf("== %s ==\n", name)
#define TEST_END() do { hf_set_verbose(0); hf_reset(); \
    printf("== %d checks passed, %d failed ==\n", g_pass, g_fail); return g_fail ? 1 : 0; } while (0)

#define UNUSED __attribute__((unused))
UNUSED static hf_stats_t get_stats(void) { hf_stats_t s; hf_stats_get(&s); return s; }

UNUSED static int heap_ok(void)
{
    hf_check_report_t r;
    int ok = hf_check_heap(&r);
    if (!ok) hf_check_print(stdout, &r);
    return ok;
}

UNUSED static hf_blockinfo_t info_of(void *p)
{
    hf_blockinfo_t i;
    memset(&i, 0, sizeof i);
    if (hf_block_info(p, &i) != 0) i.size = (size_t)-1;
    return i;
}

/* are q's header bytes directly after p's payload? */
UNUSED static int adjacent(void *p, void *q)
{
    hf_blockinfo_t a = info_of(p), b = info_of(q);
    return a.payload_addr + a.size == b.header_addr;
}

UNUSED static int pattern_ok(const void *p, size_t n, unsigned char v)
{
    const unsigned char *c = p;
    for (size_t i = 0; i < n; i++) if (c[i] != v) return 0;
    return 1;
}

/* the free block that physically follows block p, or size 0 if none */
UNUSED static hf_blockinfo_t next_block_after(void *p)
{
    hf_blockinfo_t all[4096], none;
    size_t total, n = hf_blocks_snapshot(all, 4096, &total);
    memset(&none, 0, sizeof none);
    for (size_t i = 0; i < n; i++)
        if (all[i].payload_addr == (uintptr_t)p && i + 1 < n) return all[i + 1];
    return none;
}

#endif
