/*
 * benchmark.c - Benchmark Engine + multi-threaded stress tester.
 */
#include "benchmark.h"
#include "allocator.h"
#include "debug.h"
#include "stats.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const hf_bench_ops_t hf_ops_heapforge = { "heapforge", hf_malloc, hf_free, hf_realloc };
const hf_bench_ops_t hf_ops_libc      = { "libc", malloc, free, realloc };

/* ------------------------------------------------------------- helpers */

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

typedef struct { uint64_t acc, t0; } stopwatch_t;
static void sw_start(stopwatch_t *s) { s->t0 = now_ns(); }
static void sw_stop(stopwatch_t *s) { s->acc += now_ns() - s->t0; }

/* xorshift64*: tiny deterministic PRNG, one state per thread */
static uint64_t rng_next(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return x * 2685821657736338717ull;
}

static size_t skewed_size(uint64_t *rng, size_t lo, size_t hi)
{
    /* 7/8 of requests are small, 1/8 span the full range */
    uint64_t r = rng_next(rng);
    size_t span = hi - lo + 1;
    if ((r & 7) != 0) span = span / 16 + 1;
    return lo + (size_t)((r >> 8) % span);
}

/* ------------------------------------------------------------- stress  */

#define SLOTS 128

typedef struct {
    const hf_bench_ops_t *ops;
    int tid;
    uint64_t nops, seed;
    uint64_t allocs, frees, reallocs, failures, integrity;
} worker_t;

typedef struct { void *p; size_t size; uint8_t pat; } slot_t;

static size_t stress_size(uint64_t *rng)
{
    uint64_t r = rng_next(rng) >> 11;
    uint64_t k = r % 1000;
    if (k < 800) return 16 + (size_t)(r % 241);              /* 16..256    */
    if (k < 950) return 257 + (size_t)(r % 1792);            /* 257..2048  */
    if (k < 999) return 2049 + (size_t)(r % 14336);          /* ..16384    */
    return 140000 + (size_t)(r % 160000);                    /* mmap path  */
}

static int verify(const uint8_t *p, size_t n, uint8_t pat)
{
    for (size_t i = 0; i < n; i++)
        if (p[i] != pat) return 0;
    return 1;
}

static void *worker_main(void *arg)
{
    worker_t *w = arg;
    const hf_bench_ops_t *ops = w->ops;
    uint64_t rng = w->seed ? w->seed : 88172645463325252ull;
    slot_t slots[SLOTS];
    memset(slots, 0, sizeof slots);

    for (uint64_t i = 0; i < w->nops; i++) {
        uint64_t r = rng_next(&rng);
        slot_t *s = &slots[(r >> 20) % SLOTS];
        unsigned choice = (unsigned)((r >> 40) % 100);

        if (!s->p) {
            size_t sz = stress_size(&rng);
            void *p = ops->alloc_fn(sz);
            if (!p) { w->failures++; continue; }
            s->pat = (uint8_t)(w->tid * 37 + (int)(i * 13) + 1);
            memset(p, s->pat, sz);
            s->p = p; s->size = sz;
            w->allocs++;
        } else if (choice < 60) {                        /* free */
            if (!verify(s->p, s->size, s->pat)) w->integrity++;
            ops->free_fn(s->p);
            s->p = NULL;
            w->frees++;
        } else if (choice < 85) {                        /* realloc */
            size_t nsz = stress_size(&rng);
            void *np = ops->realloc_fn(s->p, nsz);
            if (!np) { w->failures++; continue; }        /* old block still valid */
            size_t keep = nsz < s->size ? nsz : s->size;
            if (!verify(np, keep, s->pat)) w->integrity++;
            s->pat = (uint8_t)(s->pat + 7);
            memset(np, s->pat, nsz);
            s->p = np; s->size = nsz;
            w->reallocs++;
        } else {                                         /* just re-check */
            if (!verify(s->p, s->size, s->pat)) w->integrity++;
        }
    }
    for (int k = 0; k < SLOTS; k++) {
        if (slots[k].p) {
            if (!verify(slots[k].p, slots[k].size, slots[k].pat)) w->integrity++;
            ops->free_fn(slots[k].p);
            w->frees++;
        }
    }
    return NULL;
}

int hf_stress_run(const hf_bench_ops_t *ops, int threads, uint64_t total_ops,
                  uint64_t seed, hf_stress_result_t *res)
{
    if (threads < 1 || threads > 256 || total_ops == 0) { errno = EINVAL; return -1; }
    memset(res, 0, sizeof *res);
    res->threads = threads;
    res->check_ok = -1;

    int is_hf = ops == &hf_ops_heapforge;
    hf_stats_t before, after;
    if (is_hf) hf_stats_get(&before);

    pthread_t *tids = calloc((size_t)threads, sizeof *tids);
    worker_t *ws = calloc((size_t)threads, sizeof *ws);
    int *started = calloc((size_t)threads, sizeof *started);
    if (!tids || !ws || !started) { free(tids); free(ws); free(started); errno = ENOMEM; return -1; }

    uint64_t per = total_ops / (uint64_t)threads, extra = total_ops % (uint64_t)threads;
    uint64_t t0 = now_ns();
    for (int t = 0; t < threads; t++) {
        ws[t].ops = ops;
        ws[t].tid = t;
        ws[t].nops = per + ((uint64_t)t < extra ? 1 : 0);
        ws[t].seed = seed * 0x9E3779B97F4A7C15ull + (uint64_t)t * 0xBF58476D1CE4E5B9ull + 1;
        started[t] = pthread_create(&tids[t], NULL, worker_main, &ws[t]) == 0;
    }
    int rc = 0;
    for (int t = 0; t < threads; t++) {
        if (started[t]) pthread_join(tids[t], NULL);
        else rc = -1;
    }
    res->elapsed_ns = now_ns() - t0;

    for (int t = 0; t < threads; t++) {
        res->allocs += ws[t].allocs; res->frees += ws[t].frees; res->reallocs += ws[t].reallocs;
        res->failures += ws[t].failures; res->integrity_errors += ws[t].integrity;
        res->total_ops += ws[t].nops;
    }
    free(tids); free(ws); free(started);

    if (is_hf) {
        hf_check_report_t rep;
        res->check_ok = hf_check_heap(&rep);
        res->check_violations = rep.violations;
        hf_stats_get(&after);
        res->corruptions = after.c.corruptions - before.c.corruptions;
        res->invalid_frees = after.c.invalid_frees - before.c.invalid_frees;
        res->double_frees = after.c.double_frees - before.c.double_frees;
        res->leaked_blocks = (int64_t)after.c.active_allocs - (int64_t)before.c.active_allocs;
        res->lock_acquired = after.c.lock_acquired - before.c.lock_acquired;
        res->lock_contended = after.c.lock_contended - before.c.lock_contended;
    }
    return rc;
}

void hf_stress_print(FILE *out, const hf_stress_result_t *r, const char *allocator)
{
    double ms = (double)r->elapsed_ns / 1e6;
    fprintf(out, "Stress test (%s)\n", allocator);
    fprintf(out, "  threads              : %d\n", r->threads);
    fprintf(out, "  operations           : %llu (allocs %llu, frees %llu, reallocs %llu)\n",
            (unsigned long long)r->total_ops, (unsigned long long)r->allocs,
            (unsigned long long)r->frees, (unsigned long long)r->reallocs);
    fprintf(out, "  allocation failures  : %llu\n", (unsigned long long)r->failures);
    fprintf(out, "  data-integrity errors: %llu   (payload pattern of an owned block changed)\n",
            (unsigned long long)r->integrity_errors);
    if (r->check_ok >= 0) {
        fprintf(out, "  corruptions detected : %llu   invalid frees: %llu   double frees: %llu\n",
                (unsigned long long)r->corruptions, (unsigned long long)r->invalid_frees,
                (unsigned long long)r->double_frees);
        fprintf(out, "  leaked blocks        : %lld\n", (long long)r->leaked_blocks);
        fprintf(out, "  heap consistency     : %s (%zu violations)\n", r->check_ok ? "PASS" : "FAIL", r->check_violations);
        fprintf(out, "  lock acquisitions    : %llu (contended: %llu = %.2f%%)\n",
                (unsigned long long)r->lock_acquired, (unsigned long long)r->lock_contended,
                r->lock_acquired ? 100.0 * (double)r->lock_contended / (double)r->lock_acquired : 0.0);
    }
    fprintf(out, "  elapsed              : %.2f ms  (%.0f ops/s)\n", ms, ms > 0 ? (double)r->total_ops / (ms / 1000.0) : 0.0);
}

/* ----------------------------------------------------------- workloads */

const char *hf_workload_name(hf_workload_t w)
{
    static const char *n[] = { "small", "mixed", "churn", "frag", "threads" };
    return (unsigned)w < HF_WL_COUNT ? n[w] : "?";
}

const char *hf_workload_desc(hf_workload_t w)
{
    static const char *d[] = {
        "60k x 32 B allocs, then free all in order",
        "20k random-size allocs, free random half, 5k more allocs, free rest",
        "200k random alloc/free ops over 1024 slots, sizes 16..2048",
        "4k pairs of 1024 B/48 B, free the 1024 B ones, carve holes with 100..900 B then 32..128 B",
        "multi-threaded (default 4) alloc/free/realloc with data-integrity checking",
    };
    return (unsigned)w < HF_WL_COUNT ? d[w] : "?";
}

static void snap(hf_bench_result_t *r, const hf_bench_ops_t *ops)
{
    if (ops != &hf_ops_heapforge) return;
    hf_stats_t s;
    hf_stats_get(&s);
    r->have_stats = 1;
    r->heap_bytes = s.heap_size;
    r->live_requested = s.alloc_requested;
    r->free_blocks = s.free_blocks;
    r->ext_frag = s.external_frag;
    r->utilization = s.utilization;
}

static size_t scaled(size_t base, double scale)
{
    size_t v = (size_t)((double)base * scale);
    return v < 16 ? 16 : v;
}

static int wl_small(const hf_bench_ops_t *ops, double scale, hf_bench_result_t *r)
{
    size_t n = scaled(60000, scale);
    void **p = calloc(n, sizeof *p);
    if (!p) return -1;
    stopwatch_t sw = {0, 0};
    sw_start(&sw);
    for (size_t i = 0; i < n; i++) { p[i] = ops->alloc_fn(32); if (!p[i]) r->failures++; }
    sw_stop(&sw);
    snap(r, ops);
    sw_start(&sw);
    for (size_t i = 0; i < n; i++) if (p[i]) ops->free_fn(p[i]);
    sw_stop(&sw);
    r->ops = 2 * n;
    r->elapsed_ns = sw.acc;
    free(p);
    return 0;
}

static int wl_mixed(const hf_bench_ops_t *ops, double scale, hf_bench_result_t *r)
{
    size_t n = scaled(20000, scale);
    void **p = calloc(n, sizeof *p);
    uint8_t *freed = calloc(n, 1);
    if (!p || !freed) { free(p); free(freed); return -1; }
    uint64_t rng = 0x1234567ull;
    stopwatch_t sw = {0, 0};
    uint64_t ops_done = 0;

    sw_start(&sw);
    for (size_t i = 0; i < n; i++) { p[i] = ops->alloc_fn(skewed_size(&rng, 16, 4096)); if (!p[i]) r->failures++; ops_done++; }
    for (size_t i = 0; i < n; i++)
        if (p[i] && (rng_next(&rng) & 1)) { ops->free_fn(p[i]); p[i] = NULL; freed[i] = 1; ops_done++; }
    size_t again = 0;
    for (size_t i = 0; i < n && again < n / 4; i++)
        if (freed[i]) { p[i] = ops->alloc_fn(skewed_size(&rng, 16, 4096)); if (!p[i]) r->failures++; ops_done++; again++; }
    sw_stop(&sw);
    snap(r, ops);
    sw_start(&sw);
    for (size_t i = 0; i < n; i++) if (p[i]) { ops->free_fn(p[i]); ops_done++; }
    sw_stop(&sw);
    r->ops = ops_done;
    r->elapsed_ns = sw.acc;
    free(p); free(freed);
    return 0;
}

static int wl_churn(const hf_bench_ops_t *ops, double scale, hf_bench_result_t *r)
{
    size_t n = scaled(200000, scale);
    void *slots[1024];
    memset(slots, 0, sizeof slots);
    uint64_t rng = 0xBADC0FFEEull;
    stopwatch_t sw = {0, 0};
    sw_start(&sw);
    for (size_t i = 0; i < n; i++) {
        uint64_t x = rng_next(&rng);
        void **s = &slots[(x >> 16) % 1024];
        if (*s) { ops->free_fn(*s); *s = NULL; }
        else { *s = ops->alloc_fn(skewed_size(&rng, 16, 2048)); if (!*s) r->failures++; }
    }
    sw_stop(&sw);
    snap(r, ops);
    for (int k = 0; k < 1024; k++) if (slots[k]) ops->free_fn(slots[k]);
    r->ops = n;
    r->elapsed_ns = sw.acc;
    return 0;
}

static int wl_frag(const hf_bench_ops_t *ops, double scale, hf_bench_result_t *r)
{
    size_t pairs = scaled(4000, scale);
    size_t total = pairs * 2 + pairs * 3 + pairs * 2;
    void **p = calloc(total, sizeof *p);
    if (!p) return -1;
    uint64_t rng = 0xFEEDFACEull;
    stopwatch_t sw = {0, 0};
    uint64_t done = 0;
    size_t k = 0;

    sw_start(&sw);
    for (size_t i = 0; i < pairs; i++) {                /* A: big, small, big, small... */
        p[k++] = ops->alloc_fn(1024);
        p[k++] = ops->alloc_fn(48);
        done += 2;
    }
    for (size_t i = 0; i < pairs; i++) {                /* B: free every big one */
        if (p[2 * i]) ops->free_fn(p[2 * i]);
        p[2 * i] = NULL;
        done++;
    }
    for (size_t i = 0; i < pairs; i++) {                /* C: medium requests carve the holes */
        p[k] = ops->alloc_fn(100 + (size_t)(rng_next(&rng) % 801));
        if (!p[k]) r->failures++;
        k++; done++;
    }
    for (size_t i = 0; i < pairs / 2; i++) {            /* D: small requests */
        p[k] = ops->alloc_fn(32 + (size_t)(rng_next(&rng) % 97));
        if (!p[k]) r->failures++;
        k++; done++;
    }
    sw_stop(&sw);
    snap(r, ops);
    for (size_t i = 0; i < k; i++) if (p[i]) ops->free_fn(p[i]);
    r->ops = done;
    r->elapsed_ns = sw.acc;
    free(p);
    return 0;
}

static int wl_threads(const hf_bench_ops_t *ops, double scale, int threads, hf_bench_result_t *r)
{
    hf_stress_result_t s;
    if (hf_stress_run(ops, threads, scaled(100000, scale), 12345, &s) != 0) return -1;
    r->ops = s.total_ops;
    r->elapsed_ns = s.elapsed_ns;
    r->failures = s.failures;
    r->integrity_errors = s.integrity_errors;
    snap(r, ops);
    return 0;
}

static int run_workload(hf_workload_t w, const hf_bench_ops_t *ops, double scale, int threads, hf_bench_result_t *r)
{
    memset(r, 0, sizeof *r);
    int rc;
    switch (w) {
    case HF_WL_SMALL:   rc = wl_small(ops, scale, r); break;
    case HF_WL_MIXED:   rc = wl_mixed(ops, scale, r); break;
    case HF_WL_CHURN:   rc = wl_churn(ops, scale, r); break;
    case HF_WL_FRAG:    rc = wl_frag(ops, scale, r); break;
    case HF_WL_THREADS: rc = wl_threads(ops, scale, threads, r); break;
    default: rc = -1;
    }
    r->ok = rc == 0;
    return rc;
}

/* ------------------------------------------------ isolated child process */

int hf_bench_child_main(int argc, char **argv)
{
    /* argv: heapforge --bench-child <fd> <workload> <allocator> <scale> <threads> */
    if (argc < 7) return 2;
    int fd = atoi(argv[2]);
    int wl = atoi(argv[3]);
    const char *alloc = argv[4];
    double scale = strtod(argv[5], NULL);
    int threads = atoi(argv[6]);
    if (wl < 0 || wl >= HF_WL_COUNT || scale <= 0.0) return 2;

    const hf_bench_ops_t *ops = &hf_ops_heapforge;
    if (!strcmp(alloc, "libc")) {
        ops = &hf_ops_libc;
    } else {
        hf_strategy_t st;
        if (hf_strategy_parse(alloc, &st) != 0) return 2;
        hf_set_strategy(st);
        hf_set_poison(0);          /* measure the allocator, not the debug poisoning */
        hf_set_verbose(0);
    }
    hf_bench_result_t r;
    run_workload((hf_workload_t)wl, ops, scale, threads, &r);
    ssize_t n = write(fd, &r, sizeof r);
    return n == (ssize_t)sizeof r ? 0 : 1;
}

static int run_isolated(hf_workload_t w, const char *alloc, const hf_bench_config_t *cfg, hf_bench_result_t *out)
{
    int fds[2];
    memset(out, 0, sizeof *out);
    if (pipe(fds) != 0) return -1;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        close(fds[0]);
        char fdb[16], wb[16], sb[32], tb[16];
        snprintf(fdb, sizeof fdb, "%d", fds[1]);
        snprintf(wb, sizeof wb, "%d", (int)w);
        snprintf(sb, sizeof sb, "%.6f", cfg->scale);
        snprintf(tb, sizeof tb, "%d", cfg->threads);
        char *args[] = { "heapforge", "--bench-child", fdb, wb, (char *)alloc, sb, tb, NULL };
        execv("/proc/self/exe", args);
        _exit(127);
    }
    close(fds[1]);
    size_t got = 0;
    while (got < sizeof *out) {
        ssize_t n = read(fds[0], (char *)out + got, sizeof *out - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (got != sizeof *out || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || !out->ok) {
        out->ok = 0;
        return -1;
    }
    return 0;
}

static int cmp_u64(const void *a, const void *b)
{
    const hf_bench_result_t *x = a, *y = b;
    return x->elapsed_ns < y->elapsed_ns ? -1 : x->elapsed_ns > y->elapsed_ns;
}

void hf_bench_config_default(hf_bench_config_t *c)
{
    memset(c, 0, sizeof *c);
    c->scale = 1.0;
    c->runs = 3;
    c->threads = 4;
    c->include_libc = 1;
}

int hf_benchmark_run(const hf_bench_config_t *cfg, FILE *out)
{
    const char *allocs[] = { "first-fit", "best-fit", "worst-fit", "libc" };
    int nalloc = cfg->include_libc ? 4 : 3;
    int runs = cfg->runs < 1 ? 1 : cfg->runs > 15 ? 15 : cfg->runs;
    FILE *csv = NULL;
    if (cfg->csv_path) {
        csv = fopen(cfg->csv_path, "w");
        if (!csv) fprintf(out, "warning: cannot write %s: %s\n", cfg->csv_path, strerror(errno));
        else fprintf(csv, "workload,allocator,runs,ops,elapsed_ms,ns_per_op,heap_bytes,external_frag_pct,free_blocks,failures,integrity_errors\n");
    }

    fprintf(out, "HeapForge benchmark: scale %.2f, %d run(s) per cell (median shown), each run in a fresh process\n",
            cfg->scale, runs);
    fprintf(out, "Timing covers only the allocator calls of the workload (clock_gettime CLOCK_MONOTONIC).\n");
    fprintf(out, "Heap/frag columns are read from the real heap right after the main phase (libc: n/a).\n\n");

    int failed = 0;
    for (int w = 0; w < HF_WL_COUNT; w++) {
        fprintf(out, "Workload '%s': %s\n", hf_workload_name((hf_workload_t)w), hf_workload_desc((hf_workload_t)w));
        fprintf(out, "  %-10s %9s %10s %9s %11s %9s %9s %6s %5s\n", "allocator", "ops", "time(ms)", "ns/op",
                "heap(KiB)", "extFrag%", "freeBlks", "fail", "integ");
        double best_ms = -1;
        const char *best_name = "";
        for (int a = 0; a < nalloc; a++) {
            hf_bench_result_t res[15];
            int good = 0;
            for (int i = 0; i < runs; i++) {
                if (run_isolated((hf_workload_t)w, allocs[a], cfg, &res[good]) == 0) good++;
            }
            if (good == 0) {
                fprintf(out, "  %-10s  (measurement failed)\n", allocs[a]);
                failed = 1;
                continue;
            }
            qsort(res, (size_t)good, sizeof res[0], cmp_u64);
            hf_bench_result_t *m = &res[good / 2];
            double ms = (double)m->elapsed_ns / 1e6;
            double nsop = m->ops ? (double)m->elapsed_ns / (double)m->ops : 0;
            if (m->have_stats)
                fprintf(out, "  %-10s %9llu %10.2f %9.1f %11.1f %9.2f %9llu %6llu %5llu\n", allocs[a],
                        (unsigned long long)m->ops, ms, nsop, (double)m->heap_bytes / 1024.0, m->ext_frag * 100.0,
                        (unsigned long long)m->free_blocks, (unsigned long long)m->failures,
                        (unsigned long long)m->integrity_errors);
            else
                fprintf(out, "  %-10s %9llu %10.2f %9.1f %11s %9s %9s %6llu %5llu\n", allocs[a],
                        (unsigned long long)m->ops, ms, nsop, "n/a", "n/a", "n/a",
                        (unsigned long long)m->failures, (unsigned long long)m->integrity_errors);
            if (csv)
                fprintf(csv, "%s,%s,%d,%llu,%.3f,%.1f,%llu,%.3f,%llu,%llu,%llu\n", hf_workload_name((hf_workload_t)w),
                        allocs[a], good, (unsigned long long)m->ops, ms, nsop, (unsigned long long)m->heap_bytes,
                        m->ext_frag * 100.0, (unsigned long long)m->free_blocks, (unsigned long long)m->failures,
                        (unsigned long long)m->integrity_errors);
            if (best_ms < 0 || ms < best_ms) { best_ms = ms; best_name = allocs[a]; }
        }
        if (best_ms >= 0) fprintf(out, "  fastest in this run: %s\n", best_name);
        fprintf(out, "\n");
    }
    if (csv) { fclose(csv); fprintf(out, "CSV written to %s\n", cfg->csv_path); }
    return failed ? 1 : 0;
}
