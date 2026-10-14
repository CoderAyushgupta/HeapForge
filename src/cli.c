/*
 * cli.c - interactive shell + command dispatcher. All commands operate on
 * the real allocator; this file contains no allocator logic.
 */
#include "cli.h"
#include "allocator.h"
#include "benchmark.h"
#include "debug.h"
#include "demo.h"
#include "stats.h"
#include "visualizer.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ARGS 16

typedef int (*cmd_fn)(int argc, char **argv);

typedef struct {
    const char *name;
    cmd_fn fn;
    const char *usage;
    const char *help;
} command_t;

/* ----------------------------------------------------------- parsing */

static int parse_size(const char *s, size_t *out)
{
    if (!s || !*s || *s == '-' || *s == '+') return -1;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno == ERANGE || end == s) return -1;
    unsigned long long mult = 1;
    if (*end) {
        if (end[1] != '\0') return -1;
        switch (tolower((unsigned char)*end)) {
        case 'k': mult = 1024ull; break;
        case 'm': mult = 1024ull * 1024; break;
        case 'g': mult = 1024ull * 1024 * 1024; break;
        default: return -1;
        }
    }
    if (v > (unsigned long long)SIZE_MAX / mult) return -1;
    *out = (size_t)(v * mult);
    return 0;
}

/* "#id" (allocation id) or a hexadecimal address */
static int parse_ptr(const char *s, void **out)
{
    if (!s || !*s) return -1;
    if (s[0] == '#') {
        size_t id;
        if (parse_size(s + 1, &id) != 0 || id == 0) { fprintf(stderr, "error: bad allocation id '%s'\n", s); return -1; }
        void *p = hf_ptr_from_id(id);
        if (!p) { fprintf(stderr, "error: no live allocation with id %zu\n", id); return -1; }
        *out = p;
        return 0;
    }
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 16);
    if (errno == ERANGE || end == s || *end != '\0' || v == 0) {
        fprintf(stderr, "error: '%s' is not a valid hexadecimal address or #id\n", s);
        return -1;
    }
    *out = (void *)(uintptr_t)v;
    return 0;
}

static int need_args(int argc, int min, int max, const char *usage)
{
    if (argc - 1 < min || argc - 1 > max) {
        fprintf(stderr, "usage: %s\n", usage);
        return -1;
    }
    return 0;
}

static void announce(const char *what, void *p, size_t req)
{
    if (!p) {
        fprintf(stderr, "%s failed: %s\n", what, strerror(errno ? errno : ENOMEM));
        return;
    }
    hf_blockinfo_t i;
    if (hf_block_info(p, &i) == 0)
        printf("%s -> %p  (id #%llu, %zu bytes requested, %zu payload, %s)\n", what, p,
               (unsigned long long)i.id, req, i.size, i.state == HF_BLOCK_MMAP ? "mmap" : "sbrk heap");
    else
        printf("%s -> %p\n", what, p);
}

/* ---------------------------------------------------------- commands */

static int cmd_malloc(int argc, char **argv)
{
    size_t n;
    if (need_args(argc, 1, 1, "malloc <size>") ) return 1;
    if (parse_size(argv[1], &n) != 0) { fprintf(stderr, "error: bad size '%s' (use digits, optional k/m/g)\n", argv[1]); return 1; }
    if (n == 0) { fprintf(stderr, "note: hf_malloc(0) returns NULL by design\n"); return 1; }
    errno = 0;
    void *p = HF_MALLOC(n);
    announce("malloc", p, n);
    return p ? 0 : 1;
}

static int cmd_calloc(int argc, char **argv)
{
    size_t n, sz;
    if (need_args(argc, 2, 2, "calloc <count> <size>")) return 1;
    if (parse_size(argv[1], &n) != 0 || parse_size(argv[2], &sz) != 0) { fprintf(stderr, "error: bad count or size\n"); return 1; }
    errno = 0;
    void *p = HF_CALLOC(n, sz);
    if (!p && n && sz > SIZE_MAX / n) fprintf(stderr, "calloc: count * size overflows size_t - rejected\n");
    else if (n == 0 || sz == 0) fprintf(stderr, "note: a zero-byte calloc returns NULL by design\n");
    else announce("calloc", p, n * sz);
    return p ? 0 : 1;
}

static int cmd_realloc(int argc, char **argv)
{
    void *p;
    size_t n;
    if (need_args(argc, 2, 2, "realloc <ptr|#id> <size>")) return 1;
    if (strcmp(argv[1], "0") == 0 || strcmp(argv[1], "null") == 0 || strcmp(argv[1], "NULL") == 0) p = NULL;
    else if (parse_ptr(argv[1], &p) != 0) return 1;
    if (parse_size(argv[2], &n) != 0) { fprintf(stderr, "error: bad size '%s'\n", argv[2]); return 1; }
    errno = 0;
    void *q = HF_REALLOC(p, n);
    if (n == 0 && p) { printf("realloc(ptr, 0) freed the block and returned NULL\n"); return 0; }
    announce("realloc", q, n);
    if (q && p && q == p) printf("(resized in place)\n");
    else if (q && p) printf("(block moved; old block released)\n");
    return q ? 0 : 1;
}

static int cmd_free(int argc, char **argv)
{
    void *p;
    if (need_args(argc, 1, 1, "free <ptr|#id>")) return 1;
    if (parse_ptr(argv[1], &p) != 0) return 1;
    hf_stats_t a, b;
    hf_stats_get(&a);
    hf_free(p);
    hf_stats_get(&b);
    if (b.c.total_frees > a.c.total_frees) printf("freed %p (coalesces +%llu)\n", p, (unsigned long long)(b.c.coalesces - a.c.coalesces));
    else return 1;
    return 0;
}

static int cmd_map(int argc, char **argv) { (void)argc; (void)argv; hf_viz_map(stdout); return 0; }
static int cmd_stats(int argc, char **argv) { (void)argc; (void)argv; hf_viz_stats(stdout); return 0; }
static int cmd_freelist(int argc, char **argv) { (void)argc; (void)argv; hf_viz_freelist(stdout); return 0; }
static int cmd_procmap(int argc, char **argv) { (void)argc; (void)argv; hf_viz_proc_maps(stdout); return 0; }

static int cmd_inspect(int argc, char **argv)
{
    if (need_args(argc, 0, 1, "inspect [ptr|#id]")) return 1;
    if (argc == 1) { hf_viz_blocks(stdout, 64); return 0; }
    void *p;
    if (parse_ptr(argv[1], &p) != 0) return 1;
    hf_viz_block_detail(stdout, p);
    return 0;
}

static int cmd_check(int argc, char **argv)
{
    (void)argc; (void)argv;
    hf_check_report_t r;
    hf_check_heap(&r);
    hf_check_print(stdout, &r);
    return r.ok ? 0 : 1;
}

static int cmd_leaks(int argc, char **argv)
{
    (void)argc; (void)argv;
    hf_leak_report(stdout, 50, NULL);
    return 0;
}

static int cmd_strategy(int argc, char **argv)
{
    if (need_args(argc, 0, 1, "strategy [first-fit|best-fit|worst-fit]")) return 1;
    if (argc == 2) {
        hf_strategy_t s;
        if (hf_strategy_parse(argv[1], &s) != 0) { fprintf(stderr, "error: unknown strategy '%s'\n", argv[1]); return 1; }
        hf_set_strategy(s);
    }
    printf("strategy: %s\n", hf_strategy_name(hf_get_strategy()));
    return 0;
}

static int cmd_threshold(int argc, char **argv)
{
    if (need_args(argc, 0, 1, "threshold [bytes]")) return 1;
    if (argc == 2) {
        size_t n;
        if (parse_size(argv[1], &n) != 0 || n == 0) { fprintf(stderr, "error: bad threshold\n"); return 1; }
        hf_set_mmap_threshold(n);
    }
    printf("mmap threshold: %zu bytes\n", hf_get_mmap_threshold());
    return 0;
}

static int cmd_reset(int argc, char **argv)
{
    (void)argc; (void)argv;
    hf_reset();
    printf("heap reset: every block freed and merged, statistics cleared\n");
    return 0;
}

static int cmd_trim(int argc, char **argv)
{
    (void)argc; (void)argv;
    size_t r = hf_trim();
    if (r) printf("returned %zu bytes to the kernel via sbrk(-%zu)\n", r, r);
    else printf("nothing to trim (top block not free, too small, or break not at heap end)\n");
    return 0;
}

static int cmd_paranoid(int argc, char **argv)
{
    if (need_args(argc, 1, 1, "paranoid on|off")) return 1;
    if (!strcmp(argv[1], "on")) hf_set_paranoid(1);
    else if (!strcmp(argv[1], "off")) hf_set_paranoid(0);
    else { fprintf(stderr, "usage: paranoid on|off\n"); return 1; }
    printf("paranoid mode %s (full heap check after every operation)\n", argv[1]);
    return 0;
}

/* Deliberate fault injection for demonstrating the canary. */
static int cmd_overflow(int argc, char **argv)
{
    void *p;
    size_t n;
    if (need_args(argc, 2, 2, "overflow <ptr|#id> <bytes>")) return 1;
    if (parse_ptr(argv[1], &p) != 0 || parse_size(argv[2], &n) != 0 || n == 0) { fprintf(stderr, "error: bad arguments\n"); return 1; }
    hf_blockinfo_t i;
    if (hf_block_info(p, &i) != 0 || i.state == HF_BLOCK_FREE) { fprintf(stderr, "error: not a live allocation\n"); return 1; }
    size_t max = i.size - i.requested + ((i.state == HF_BLOCK_MMAP || i.is_last) ? 0 : HF_BLOCK_HDR);
    if (n > max) { fprintf(stderr, "error: refusing to write %zu bytes; maximum for this block is %zu\n", n, max); return 1; }
    memset((char *)p + i.requested, 'A', n);
    printf("FAULT INJECTION: wrote %zu bytes of 'A' starting at the first byte past the %zu requested\n", n, i.requested);
    printf("run 'check' to see the canary violation, or 'free' to see the free-time report\n");
    return 0;
}

static int cmd_stress(int argc, char **argv)
{
    int threads = 4;
    uint64_t ops = 100000;
    for (int i = 1; i < argc; i++) {
        size_t v;
        if (!strcmp(argv[i], "--threads") && i + 1 < argc && parse_size(argv[i + 1], &v) == 0 && v >= 1 && v <= 256) { threads = (int)v; i++; }
        else if (!strcmp(argv[i], "--operations") && i + 1 < argc && parse_size(argv[i + 1], &v) == 0 && v >= 1) { ops = v; i++; }
        else { fprintf(stderr, "usage: stress [--threads N(1..256)] [--operations N]\n"); return 1; }
    }
    hf_stress_result_t r;
    if (hf_stress_run(&hf_ops_heapforge, threads, ops, 42, &r) != 0) { fprintf(stderr, "stress run failed: %s\n", strerror(errno)); return 1; }
    hf_stress_print(stdout, &r, "heapforge");
    return (r.integrity_errors || !r.check_ok || r.leaked_blocks != 0) ? 1 : 0;
}

static int cmd_benchmark(int argc, char **argv)
{
    hf_bench_config_t cfg;
    hf_bench_config_default(&cfg);
    for (int i = 1; i < argc; i++) {
        size_t v;
        if (!strcmp(argv[i], "--quick")) { cfg.scale = 0.2; cfg.runs = 1; }
        else if (!strcmp(argv[i], "--no-libc")) cfg.include_libc = 0;
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc && parse_size(argv[i + 1], &v) == 0 && v >= 1 && v <= 15) { cfg.runs = (int)v; i++; }
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc && parse_size(argv[i + 1], &v) == 0 && v >= 1 && v <= 64) { cfg.threads = (int)v; i++; }
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) { cfg.scale = strtod(argv[i + 1], NULL); i++; if (cfg.scale <= 0 || cfg.scale > 20) { fprintf(stderr, "error: scale must be in (0,20]\n"); return 1; } }
        else if (!strcmp(argv[i], "--csv") && i + 1 < argc) { cfg.csv_path = argv[i + 1]; i++; }
        else { fprintf(stderr, "usage: benchmark [--quick] [--no-libc] [--runs N] [--scale X] [--threads N] [--csv file]\n"); return 1; }
    }
    return hf_benchmark_run(&cfg, stdout);
}

static int cmd_demo(int argc, char **argv)
{
    int step = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--step")) step = 1;
        else { fprintf(stderr, "usage: demo [--step]\n"); return 1; }
    }
    return hf_demo_run(step);
}

static int cmd_help(int argc, char **argv);
static int cmd_quit(int argc, char **argv) { (void)argc; (void)argv; return -1; }

static const command_t commands[] = {
    { "help", cmd_help, "help", "list commands" },
    { "malloc", cmd_malloc, "malloc <size>", "allocate (size accepts k/m/g suffix)" },
    { "calloc", cmd_calloc, "calloc <count> <size>", "allocate zeroed memory (overflow-checked)" },
    { "realloc", cmd_realloc, "realloc <ptr|#id> <size>", "resize a block ('0' as ptr = NULL)" },
    { "free", cmd_free, "free <ptr|#id>", "release a block (double/invalid frees are detected)" },
    { "map", cmd_map, "map", "heap observatory: proportional map + block strip" },
    { "stats", cmd_stats, "stats", "statistics and fragmentation metrics" },
    { "inspect", cmd_inspect, "inspect [ptr|#id]", "block table, or details of one block" },
    { "freelist", cmd_freelist, "freelist", "show the free list in list order" },
    { "procmap", cmd_procmap, "procmap", "kernel view: [heap] and mmap regions from /proc/self/maps" },
    { "check", cmd_check, "check", "heap consistency checker (PASS/FAIL)" },
    { "leaks", cmd_leaks, "leaks", "live allocations (potential leaks) with call sites" },
    { "strategy", cmd_strategy, "strategy [name]", "show/set first-fit | best-fit | worst-fit" },
    { "threshold", cmd_threshold, "threshold [bytes]", "show/set the mmap threshold" },
    { "benchmark", cmd_benchmark, "benchmark [options]", "compare strategies (and glibc) on real workloads" },
    { "stress", cmd_stress, "stress [--threads N] [--operations N]", "multi-threaded stress test + validation" },
    { "demo", cmd_demo, "demo [--step]", "scripted faculty demonstration" },
    { "overflow", cmd_overflow, "overflow <ptr|#id> <bytes>", "FAULT INJECTION: overwrite past a block's end" },
    { "paranoid", cmd_paranoid, "paranoid on|off", "run the full heap check after every operation" },
    { "trim", cmd_trim, "trim", "return the top free block to the kernel (sbrk negative)" },
    { "reset", cmd_reset, "reset", "free everything and clear statistics" },
    { "quit", cmd_quit, "quit", "leave the shell" },
    { "exit", cmd_quit, "exit", "leave the shell" },
};
#define NCOMMANDS (sizeof commands / sizeof commands[0])

static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("HeapForge commands:\n");
    for (size_t i = 0; i < NCOMMANDS; i++) {
        if (!strcmp(commands[i].name, "exit")) continue;
        printf("  %-38s %s\n", commands[i].usage, commands[i].help);
    }
    printf("\nPointers are hex addresses (as printed) or '#id' allocation ids.\n");
    return 0;
}

void hf_cli_usage(void)
{
    printf("HeapForge %s - adaptive, thread-safe, self-diagnosing Linux memory allocator\n\n", HF_VERSION);
    printf("Usage:\n  heapforge [options] [command [args...]]\n\n");
    printf("Options:\n"
           "  --strategy <first-fit|best-fit|worst-fit>   select the allocation strategy\n"
           "  --threshold <bytes>                          mmap threshold (default %u)\n"
           "  -c \"cmd; cmd; ...\"                           run commands, then exit\n"
           "  --no-color                                   disable ANSI colors\n"
           "  --help, --version\n\n", HF_MMAP_THRESHOLD_DEFAULT);
    printf("With no command, an interactive shell starts (type 'help').\n"
           "Commands such as demo, stress and benchmark also work as one-shot arguments:\n"
           "  ./heapforge demo\n  ./heapforge stress --threads 8 --operations 100000\n"
           "  ./heapforge benchmark --quick\n\n");
    printf("Note: allocator state lives inside one process. One-shot 'check', 'leaks',\n"
           "'stats' and 'map' therefore describe a fresh heap; use the shell, -c, or a\n"
           "script on stdin to inspect a heap you have built up.\n");
}

/* ---------------------------------------------------------- dispatch */

int hf_cli_is_command(const char *name)
{
    for (size_t i = 0; i < NCOMMANDS; i++)
        if (!strcmp(commands[i].name, name)) return 1;
    return 0;
}

int hf_cli_execute(const char *line)
{
    char buf[512];
    if (strlen(line) >= sizeof buf) { fprintf(stderr, "error: line too long\n"); return 1; }
    strcpy(buf, line);
    char *argv[MAX_ARGS + 1];
    int argc = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, " \t\r\n", &save); tok; tok = strtok_r(NULL, " \t\r\n", &save)) {
        if (argc >= MAX_ARGS) { fprintf(stderr, "error: too many arguments\n"); return 1; }
        argv[argc++] = tok;
    }
    if (argc == 0 || argv[0][0] == '#') return 0;
    for (size_t i = 0; i < NCOMMANDS; i++) {
        if (!strcmp(argv[0], commands[i].name)) {
            int rc = commands[i].fn(argc, argv);
            fflush(stdout);
            return rc;
        }
    }
    fprintf(stderr, "error: unknown command '%s' (type 'help')\n", argv[0]);
    return 1;
}

int hf_cli_repl(void)
{
    int tty = isatty(STDIN_FILENO);
    int last = 0;
    char line[512];
    if (tty) {
        hf_viz_banner(stdout);
        printf("Type 'help' for commands, 'demo' for the guided tour, 'quit' to leave.\n");
    }
    for (;;) {
        if (tty) {
            printf("%sheapforge>%s ", hf_ui_color() ? "\033[1;32m" : "", hf_ui_color() ? "\033[0m" : "");
            fflush(stdout);
        }
        if (!fgets(line, sizeof line, stdin)) { if (tty) printf("\n"); break; }
        if (!memchr(line, '\n', sizeof line) && !feof(stdin)) {       /* over-long line: discard the rest */
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) {}
            fprintf(stderr, "error: line too long\n");
            continue;
        }
        if (!tty) {
            line[strcspn(line, "\n")] = 0;
            if (line[0] && line[0] != '#') printf("heapforge> %s\n", line);
        }
        int rc = hf_cli_execute(line);
        if (rc < 0) break;
        last = rc;
    }
    return last;
}

int hf_cli_script(const char *commands_str)
{
    char *copy = strdup(commands_str);
    if (!copy) return 1;
    int last = 0;
    char *save = NULL;
    for (char *cmd = strtok_r(copy, ";", &save); cmd; cmd = strtok_r(NULL, ";", &save)) {
        while (*cmd == ' ') cmd++;
        if (!*cmd) continue;
        printf("heapforge> %s\n", cmd);
        int rc = hf_cli_execute(cmd);
        if (rc < 0) break;
        last = rc;
    }
    free(copy);
    return last;
}
