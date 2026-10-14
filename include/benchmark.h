/*
 * benchmark.h - Benchmark Engine and multi-threaded stress tester.
 *
 * Every number comes from clock_gettime(CLOCK_MONOTONIC) around real
 * allocator calls, or from hf_stats_get() on the real heap.
 *
 * Isolation: each (workload, allocator) measurement runs in its OWN
 * process (fork + exec of /proc/self/exe --bench-child ...) so that every
 * run starts with a pristine address space and heap. Results come back to
 * the parent through a pipe.
 */
#ifndef HF_BENCHMARK_H
#define HF_BENCHMARK_H

#include <stdint.h>
#include <stdio.h>
#include "config.h"

typedef struct hf_bench_ops {
    const char *name;
    void *(*alloc_fn)(size_t);
    void (*free_fn)(void *);
    void *(*realloc_fn)(void *, size_t);
} hf_bench_ops_t;

extern const hf_bench_ops_t hf_ops_heapforge;
extern const hf_bench_ops_t hf_ops_libc;

/* ------------------------------------------------------------- stress */
typedef struct hf_stress_result {
    int threads;
    uint64_t total_ops, allocs, frees, reallocs, failures;
    uint64_t integrity_errors;     /* payload pattern changed while owned */
    uint64_t elapsed_ns;
    /* HeapForge-only (check_ok == -1 means "not applicable") */
    uint64_t corruptions, invalid_frees, double_frees;
    int64_t  leaked_blocks;        /* active allocations after - before */
    int check_ok;
    size_t check_violations;
    uint64_t lock_acquired, lock_contended;
} hf_stress_result_t;

/* total_ops is split evenly between the threads. Returns 0 on success. */
int hf_stress_run(const hf_bench_ops_t *ops, int threads, uint64_t total_ops,
                  uint64_t seed, hf_stress_result_t *res);
void hf_stress_print(FILE *out, const hf_stress_result_t *r, const char *allocator);

/* ----------------------------------------------------------- workloads */
typedef enum {
    HF_WL_SMALL = 0,   /* many small fixed-size allocs, then free in order   */
    HF_WL_MIXED,       /* random sizes, free a random half, allocate again   */
    HF_WL_CHURN,       /* random alloc/free over a fixed set of slots        */
    HF_WL_FRAG,        /* alternating big/small, free big, refill the holes  */
    HF_WL_THREADS,     /* multi-threaded stress (mutex contention)           */
    HF_WL_COUNT
} hf_workload_t;

const char *hf_workload_name(hf_workload_t w);
const char *hf_workload_desc(hf_workload_t w);

typedef struct hf_bench_result {
    int ok;
    uint64_t ops, elapsed_ns, failures, integrity_errors;
    int have_stats;
    uint64_t heap_bytes, live_requested, free_blocks;
    double ext_frag, utilization;
} hf_bench_result_t;

typedef struct hf_bench_config {
    double scale;        /* 1.0 = default workload sizes */
    int runs;            /* repetitions per cell; median is reported */
    int threads;         /* for HF_WL_THREADS */
    int include_libc;
    const char *csv_path;
} hf_bench_config_t;

void hf_bench_config_default(hf_bench_config_t *c);
int hf_benchmark_run(const hf_bench_config_t *cfg, FILE *out);
/* Entry point of the child process (argv[1] == "--bench-child"). */
int hf_bench_child_main(int argc, char **argv);

#endif
