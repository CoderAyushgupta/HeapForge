/*
 * demo.c - Faculty demo: a fixed scenario executed with real hf_* calls.
 * Every printed table, count and delta is read back from the allocator.
 */
#include "demo.h"
#include "allocator.h"
#include "benchmark.h"
#include "block.h"
#include "debug.h"
#include "stats.h"
#include "visualizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_step;
static int g_stage;

static void pause_if_needed(void)
{
    if (g_step && isatty(STDIN_FILENO)) {
        printf("\n  [press Enter to continue] ");
        fflush(stdout);
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) {}
    }
}

static void stage(const char *title, const char *explain)
{
    pause_if_needed();
    g_stage++;
    printf("\n%s=== STAGE %d: %s ===%s\n", hf_ui_color() ? "\033[1;36m" : "", g_stage, title, hf_ui_color() ? "\033[0m" : "");
    if (explain) printf("%s\n", explain);
    printf("\n");
    fflush(stdout);
}

static uint64_t coalesces_now(void)
{
    hf_stats_t s;
    hf_stats_get(&s);
    return s.c.coalesces;
}

static uint64_t splits_now(void)
{
    hf_stats_t s;
    hf_stats_get(&s);
    return s.c.splits;
}

int hf_demo_run(int step)
{
    g_step = step;
    g_stage = 0;
    hf_set_verbose(1);
    hf_set_poison(1);
    hf_set_paranoid(0);
    hf_set_strategy(HF_STRATEGY_FIRST_FIT);
    hf_set_mmap_threshold(HF_MMAP_THRESHOLD_DEFAULT);
    hf_reset();
    hf_trim();
    hf_reset();

    hf_viz_banner(stdout);
    printf("Everything below uses the real HeapForge allocator (strategy: first-fit).\n"
           "Each block header is %zu bytes; payloads are 16-byte aligned; a %u-byte canary\n"
           "follows every request.\n", (size_t)HF_BLOCK_HDR, HF_CANARY_SIZE);

    /* ---------------------------------------------------------------- 1 */
    stage("ALLOCATION", "We allocate 256, 512, 128 and 1024 bytes. The very first hf_malloc finds an\n"
          "empty free list, so the Heap Manager calls sbrk() for a 128 KiB chunk and the\n"
          "Block Manager carves our blocks out of it by SPLITTING.");
    hf_stats_t s0, s1;
    hf_stats_get(&s0);
    void *a = HF_MALLOC(256);
    void *b = HF_MALLOC(512);
    void *c = HF_MALLOC(128);
    void *d = HF_MALLOC(1024);
    hf_stats_get(&s1);
    if (!a || !b || !c || !d) { fprintf(stderr, "demo: allocation failed\n"); return 1; }
    printf("a=%p (256)  b=%p (512)  c=%p (128)  d=%p (1024)\n", a, b, c, d);
    printf("sbrk calls during this stage: %llu   splits: %llu\n\n",
           (unsigned long long)(s1.c.sbrk_calls - s0.c.sbrk_calls),
           (unsigned long long)(s1.c.splits - s0.c.splits));
    hf_viz_blocks(stdout, 16);
    printf("\n");
    hf_viz_map(stdout);

    /* ---------------------------------------------------------------- 2 */
    stage("FREE ONE BLOCK  ->  FRAGMENTATION",
          "We free b (512 B). Its neighbours a and c are still allocated, so it cannot\n"
          "coalesce: a free 'hole' appears between live blocks. The free list now has two\n"
          "nodes: that hole and the large untouched tail of the heap.");
    hf_free(b);
    hf_viz_map(stdout);
    hf_viz_freelist(stdout);
    {
        hf_stats_t s;
        hf_stats_get(&s);
        printf("\nExternal fragmentation = 1 - largest_free/free_bytes = 1 - %zu/%zu = %.2f %%\n",
               s.largest_free, s.free_bytes, s.external_frag * 100.0);
        printf("(the 512 B hole is free memory that a request bigger than it cannot use)\n");
    }

    /* ---------------------------------------------------------------- 3 */
    stage("REUSE + BLOCK SPLITTING",
          "A 100-byte request needs 112 payload bytes (100 + 8 canary, rounded to 16).\n"
          "First-fit scans the address-ordered free list and takes the first block that\n"
          "fits: the hole left by b. It is bigger than needed, so it is SPLIT.");
    uint64_t sp0 = splits_now();
    void *e = HF_MALLOC(100);
    printf("e=%p   same address as freed b? %s\n", e, e == b ? "YES (hole reused)" : "no");
    printf("splits performed by this request: %llu\n\n", (unsigned long long)(splits_now() - sp0));
    hf_viz_blocks(stdout, 16);
    printf("\n");
    hf_viz_freelist(stdout);

    /* ---------------------------------------------------------------- 4 */
    stage("COALESCING",
          "Now we free adjacent blocks and watch the free neighbours merge:\n"
          "  free(c): its PREVIOUS neighbour (split remainder of b) is free -> merge with previous\n"
          "  free(e): its NEXT neighbour (the merged block) is free         -> merge with next\n"
          "  free(a): same, next side only                                  -> merge with next\n"
          "  free(d): previous merged block AND the heap tail are free      -> merge BOTH sides");
    uint64_t co;
    co = coalesces_now(); hf_free(c); printf("free(c): coalesces +%llu\n", (unsigned long long)(coalesces_now() - co));
    co = coalesces_now(); hf_free(e); printf("free(e): coalesces +%llu\n", (unsigned long long)(coalesces_now() - co));
    co = coalesces_now(); hf_free(a); printf("free(a): coalesces +%llu\n", (unsigned long long)(coalesces_now() - co));
    co = coalesces_now(); hf_free(d); printf("free(d): coalesces +%llu  (both neighbours)\n\n", (unsigned long long)(coalesces_now() - co));
    hf_viz_map(stdout);
    hf_viz_freelist(stdout);

    /* ---------------------------------------------------------------- 5 */
    stage("sbrk vs mmap",
          "Requests below the threshold (128 KiB) live on the sbrk heap. A 300000-byte\n"
          "request gets its OWN anonymous mmap region, which is unmapped on free. The\n"
          "kernel's view in /proc/self/maps shows both.");
    void *leak1 = HF_MALLOC(77);
    void *leak2 = HF_MALLOC(1500);
    void *big = HF_MALLOC(300000);
    if (!leak1 || !leak2 || !big) { fprintf(stderr, "demo: allocation failed\n"); return 1; }
    memset(big, 0x5A, 300000);
    hf_viz_blocks(stdout, 16);
    printf("\n");
    hf_viz_proc_maps(stdout);

    /* ---------------------------------------------------------------- 6 */
    stage("STATISTICS, CHECK, LEAKS",
          "The statistics panel is recomputed from the block lists. The consistency\n"
          "checker verifies every invariant. The leak report lists live allocations with\n"
          "the file:line recorded by the HF_MALLOC macro (we have not freed 3 blocks yet).");
    hf_viz_stats(stdout);
    printf("\n");
    hf_check_report_t rep;
    hf_check_heap(&rep);
    hf_check_print(stdout, &rep);
    printf("\n");
    hf_leak_report(stdout, 20, NULL);

    /* ---------------------------------------------------------------- 7 */
    stage("MEMORY SAFETY: CANARY, DOUBLE FREE, INVALID FREE",
          "We deliberately misuse the allocator. Each diagnostic below (printed to stderr)\n"
          "is produced by inspecting the real header / canary bytes.");
    char *x = HF_MALLOC(64);
    char *y = HF_MALLOC(128);
    void *g = HF_MALLOC(64);   /* guard so y does not merge with its neighbours */
    printf("--> writing ONE byte past the end of a 64-byte block, then freeing it:\n");
    fflush(stdout);
    x[64] = 'X';
    hf_free(y);
    printf("--> freeing y a second time:\n");
    fflush(stdout);
    hf_free(y);
    printf("--> freeing an interior pointer (leak1 + 16):\n");
    fflush(stdout);
    hf_free((char *)leak1 + 16);
    printf("--> now freeing x (canary was overwritten):\n");
    fflush(stdout);
    hf_free(x);
    hf_free(g);
    {
        hf_stats_t s;
        hf_stats_get(&s);
        fflush(stderr);
        printf("\nCounters: double_frees=%llu invalid_frees=%llu corruptions=%llu\n",
               (unsigned long long)s.c.double_frees, (unsigned long long)s.c.invalid_frees,
               (unsigned long long)s.c.corruptions);
        printf("None of these bad calls damaged the heap:\n");
        hf_check_heap(&rep);
        hf_check_print(stdout, &rep);
    }

    /* ---------------------------------------------------------------- 8 */
    stage("CLEAN UP", "We free the three remaining blocks. The 300000-byte mapping is munmap()ed.");
    hf_free(leak1);
    hf_free(leak2);
    hf_free(big);
    hf_leak_report(stdout, 20, NULL);
    hf_viz_map(stdout);

    /* ---------------------------------------------------------------- 9 */
    stage("CONCURRENCY STRESS",
          "4 POSIX threads hammer hf_malloc/hf_realloc/hf_free. Each thread writes a\n"
          "unique byte pattern into every block it owns and re-verifies it before freeing:\n"
          "if two threads ever received overlapping memory the pattern would change.\n"
          "Afterwards the whole heap is validated.");
    hf_stress_result_t sr;
    if (hf_stress_run(&hf_ops_heapforge, 4, 40000, 2024, &sr) != 0) { fprintf(stderr, "demo: stress failed\n"); return 1; }
    hf_stress_print(stdout, &sr, "heapforge");

    /* --------------------------------------------------------------- 10 */
    stage("BENCHMARK (quick)",
          "First-fit vs best-fit vs worst-fit vs glibc malloc on small workloads. Each\n"
          "number is a median of measured runs executed in fresh child processes.\n"
          "glibc is a production allocator; do not expect HeapForge to beat it.");
    fflush(stdout);
    hf_bench_config_t cfg;
    hf_bench_config_default(&cfg);
    cfg.scale = 0.25;
    cfg.runs = 1;
    int rc = hf_benchmark_run(&cfg, stdout);

    printf("\nDemo finished. Final check: ");
    hf_check_heap(&rep);
    printf("%s\n", rep.ok ? "PASS" : "FAIL");
    return rc != 0 || !rep.ok || sr.integrity_errors != 0 || !sr.check_ok;
}
