#include "test_util.h"
#include <pthread.h>
#include <stdint.h>

#define NT 8
#define PER 1500

static pthread_barrier_t barrier;
static int errors[NT];

typedef struct { int id; } arg_t;

static void *worker(void *v)
{
    int id = ((arg_t *)v)->id;
    unsigned char *p[PER];
    size_t sz[PER];
    for (int i = 0; i < PER; i++) {
        sz[i] = (size_t)((i * 31 + id * 17) % 400) + 1;
        p[i] = hf_malloc(sz[i]);
        if (!p[i]) { errors[id]++; continue; }
        memset(p[i], id * 16 + (i & 15), sz[i]);       /* unique per thread+index */
    }
    pthread_barrier_wait(&barrier);                     /* everybody holds memory now */
    for (int i = 0; i < PER; i++) {
        if (!p[i]) continue;
        if (!pattern_ok(p[i], sz[i], (unsigned char)(id * 16 + (i & 15)))) errors[id]++;
        if (i % 3 == 0) {
            unsigned char *q = hf_realloc(p[i], sz[i] + 300);
            if (!q) { errors[id]++; continue; }
            if (!pattern_ok(q, sz[i], (unsigned char)(id * 16 + (i & 15)))) errors[id]++;
            p[i] = q;
        }
    }
    for (int i = 0; i < PER; i++) hf_free(p[i]);
    return NULL;
}

static void test_threads_hold_disjoint_memory(void)
{
    pthread_t t[NT];
    arg_t a[NT];
    pthread_barrier_init(&barrier, NULL, NT);
    memset(errors, 0, sizeof errors);
    for (int i = 0; i < NT; i++) { a[i].id = i; CHECK(pthread_create(&t[i], NULL, worker, &a[i]) == 0); }
    for (int i = 0; i < NT; i++) pthread_join(t[i], NULL);
    pthread_barrier_destroy(&barrier);
    int total = 0;
    for (int i = 0; i < NT; i++) total += errors[i];
    CHECK(total == 0);
    CHECK(get_stats().c.active_allocs == 0);
    CHECK(heap_ok());
}

static void test_stress_run_all_strategies(void)
{
    hf_strategy_t st[] = { HF_STRATEGY_FIRST_FIT, HF_STRATEGY_BEST_FIT, HF_STRATEGY_WORST_FIT };
    for (int i = 0; i < 3; i++) {
        hf_set_strategy(st[i]);
        hf_stress_result_t r;
        CHECK(hf_stress_run(&hf_ops_heapforge, NT, 80000, 7 + (uint64_t)i, &r) == 0);
        CHECK(r.total_ops == 80000);
        CHECK(r.integrity_errors == 0);
        CHECK(r.check_ok == 1 && r.check_violations == 0);
        CHECK(r.leaked_blocks == 0);
        CHECK(r.corruptions == 0 && r.invalid_frees == 0 && r.double_frees == 0);
        CHECK(r.failures == 0);
        printf("    %-9s: %llu ops, %.1f ms, lock contended %llu/%llu\n", hf_strategy_name(st[i]),
               (unsigned long long)r.total_ops, (double)r.elapsed_ns / 1e6,
               (unsigned long long)r.lock_contended, (unsigned long long)r.lock_acquired);
    }
}

static void test_stress_argument_validation(void)
{
    hf_stress_result_t r;
    CHECK(hf_stress_run(&hf_ops_heapforge, 0, 100, 1, &r) == -1);
    CHECK(hf_stress_run(&hf_ops_heapforge, 4, 0, 1, &r) == -1);
}

int main(void)
{
    TEST_BEGIN("test_threads");
    RUN(test_threads_hold_disjoint_memory);
    RUN(test_stress_run_all_strategies);
    RUN(test_stress_argument_validation);
    TEST_END();
}
