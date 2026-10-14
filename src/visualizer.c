/*
 * visualizer.c - Heap Observatory.
 */
#include "visualizer.h"
#include "allocator.h"
#include "stats.h"
#include "block.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PANEL_W 70
#define INNER (PANEL_W - 4)
#define BAR_W INNER

static int g_color = -1;

void hf_ui_set_color(int on) { g_color = on; }

int hf_ui_color(void)
{
    if (g_color < 0)
        g_color = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL;
    return g_color;
}

#define C(code) (hf_ui_color() ? code : "")
#define RST C("\033[0m")

static void hr(FILE *f)
{
    fputc('+', f);
    for (int i = 0; i < PANEL_W - 2; i++) fputc('-', f);
    fputs("+\n", f);
}

static void title(FILE *f, const char *t)
{
    int len = (int)strlen(t), pad = (INNER - len) / 2;
    if (pad < 0) pad = 0;
    fprintf(f, "| %*s%s%*s |\n", pad, "", t, INNER - pad - len > 0 ? INNER - pad - len : 0, "");
}

static void row(FILE *f, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(f, "| %-*.*s |\n", INNER, INNER, buf);
}

void hf_viz_banner(FILE *f)
{
    hr(f);
    fprintf(f, "| %s%-*s%s |\n", C("\033[1;36m"), INNER, "", RST);
    title(f, "H E A P F O R G E");
    title(f, "Linux Memory Allocator & Heap Observatory");
    hr(f);
}

/* ---------------------------------------------------------------- stats */

void hf_viz_stats(FILE *f)
{
    hf_stats_t s;
    hf_stats_get(&s);
    hr(f);
    title(f, "HEAPFORGE  HEAP OBSERVATORY  -  STATISTICS");
    hr(f);
    row(f, "Strategy          : %s", hf_strategy_name(s.strategy));
    row(f, "mmap threshold    : %zu bytes (larger requests use mmap)", s.mmap_threshold);
    row(f, "Heap size (sbrk)  : %zu bytes  (peak %llu)", s.heap_size, (unsigned long long)s.c.peak_managed);
    row(f, "Allocated blocks  : %zu  (payload %zu B, requested %zu B)", s.alloc_blocks, s.alloc_payload, s.alloc_requested);
    row(f, "Free              : %zu bytes in %zu block(s)", s.free_bytes, s.free_blocks);
    row(f, "Largest free block: %zu bytes   Avg free block: %.1f bytes", s.largest_free, s.avg_free_block);
    row(f, "Header overhead   : %zu bytes (%zu x %zu B)", s.header_bytes, s.alloc_blocks + s.free_blocks, (size_t)HF_BLOCK_HDR);
    row(f, "mmap regions      : %zu live, %zu bytes mapped", s.mmap_blocks, s.mmap_bytes);
    hr(f);
    row(f, "Utilization       : %6.2f %%   = requested / heap_size", s.utilization * 100.0);
    row(f, "Internal frag     : %6.2f %%   = (payload - requested) / payload", s.internal_frag * 100.0);
    row(f, "External frag     : %6.2f %%   = 1 - largest_free / free_bytes", s.external_frag * 100.0);
    hr(f);
    row(f, "malloc/calloc/realloc/free calls : %llu / %llu / %llu / %llu",
        (unsigned long long)s.c.malloc_calls, (unsigned long long)s.c.calloc_calls,
        (unsigned long long)s.c.realloc_calls, (unsigned long long)s.c.free_calls);
    row(f, "Allocations %llu  Frees %llu  Active %llu  Failures %llu",
        (unsigned long long)s.c.total_allocs, (unsigned long long)s.c.total_frees,
        (unsigned long long)s.c.active_allocs, (unsigned long long)s.c.alloc_failures);
    row(f, "Bytes requested (cumulative): %llu   Peak live: %llu",
        (unsigned long long)s.c.bytes_requested_total, (unsigned long long)s.c.peak_active_requested);
    row(f, "Splits %llu   Coalesces %llu", (unsigned long long)s.c.splits, (unsigned long long)s.c.coalesces);
    row(f, "sbrk calls %llu (%llu B)  mmap %llu  munmap %llu  mremap %llu  trims %llu",
        (unsigned long long)s.c.sbrk_calls, (unsigned long long)s.c.sbrk_bytes,
        (unsigned long long)s.c.mmap_calls, (unsigned long long)s.c.munmap_calls,
        (unsigned long long)s.c.mremap_calls, (unsigned long long)s.c.trim_calls);
    row(f, "Detected: double-free %llu  invalid-free %llu  canary-corruption %llu",
        (unsigned long long)s.c.double_frees, (unsigned long long)s.c.invalid_frees,
        (unsigned long long)s.c.corruptions);
    row(f, "Lock acquisitions %llu  (contended: %llu)",
        (unsigned long long)s.c.lock_acquired, (unsigned long long)s.c.lock_contended);
    hr(f);
}

/* ----------------------------------------------------------------- map */

static hf_blockinfo_t *take_snapshot(size_t *count, size_t *total)
{
    size_t t = 0;
    hf_stats_t s;
    hf_stats_get(&s);
    size_t cap = (size_t)(s.alloc_blocks + s.free_blocks + s.mmap_blocks) + 64;
    hf_blockinfo_t *arr = calloc(cap, sizeof *arr);
    if (!arr) { *count = 0; *total = 0; return NULL; }
    *count = hf_blocks_snapshot(arr, cap, &t);
    *total = t;
    return arr;
}

void hf_viz_map(FILE *f)
{
    size_t n, total;
    hf_blockinfo_t *b = take_snapshot(&n, &total);
    hf_stats_t s;
    hf_stats_get(&s);

    hr(f);
    title(f, "HEAPFORGE  HEAP OBSERVATORY  -  MEMORY MAP");
    hr(f);
    row(f, "Heap Size : %zu B   Allocated: %zu B   Free: %zu B", s.heap_size,
        s.alloc_payload, s.free_bytes);
    row(f, "Util %.2f%%   Free blocks %zu   External frag %.2f%%", s.utilization * 100.0,
        s.free_blocks, s.external_frag * 100.0);
    hr(f);

    /* ---- proportional bar over the sbrk heap ---- */
    size_t heap_blocks = 0;
    for (size_t i = 0; i < n; i++) if (b && b[i].state != HF_BLOCK_MMAP) heap_blocks++;
    if (!b || heap_blocks == 0 || s.heap_size == 0) {
        row(f, "(sbrk heap is empty - no memory obtained from the kernel yet)");
    } else {
        size_t alloc_in[BAR_W], free_in[BAR_W];
        memset(alloc_in, 0, sizeof alloc_in);
        memset(free_in, 0, sizeof free_in);
        size_t off = 0, M = s.heap_size;
        for (size_t i = 0; i < n; i++) {
            if (b[i].state == HF_BLOCK_MMAP) continue;
            size_t len = b[i].size + HF_BLOCK_HDR, lo = off, hi = off + len;
            size_t c0 = lo * BAR_W / M, c1 = (hi - 1) * BAR_W / M;
            if (c1 >= BAR_W) c1 = BAR_W - 1;
            for (size_t c = c0; c <= c1; c++) {
                size_t cs = c * M / BAR_W, ce = (c + 1) * M / BAR_W;
                size_t a = lo > cs ? lo : cs, e = hi < ce ? hi : ce;
                if (e > a) {
                    if (b[i].state == HF_BLOCK_FREE) free_in[c] += e - a; else alloc_in[c] += e - a;
                }
            }
            off = hi;
        }
        char line[BAR_W * 16 + 8];
        line[0] = 0;
        for (size_t c = 0; c < BAR_W; c++) {
            const char *tok;
            if (alloc_in[c] && free_in[c]) tok = hf_ui_color() ? "\033[33m%\033[0m" : "%";
            else if (alloc_in[c]) tok = hf_ui_color() ? "\033[32m#\033[0m" : "#";
            else if (free_in[c]) tok = hf_ui_color() ? "\033[90m.\033[0m" : ".";
            else tok = " ";
            strcat(line, tok);
        }
        fprintf(f, "| %s |\n", line);
        row(f, "# allocated   . free   %% both   (1 cell = ~%zu bytes of heap)", M / BAR_W ? M / BAR_W : 1);
    }
    hr(f);

    /* ---- block strip in address order (wrapped) ---- */
    char line[1024];
    int vis = 0;
    line[0] = 0;
    size_t shown = 0;
    const size_t max_tokens = 40;
    for (size_t i = 0; i < n && b; i++) {
        char vtok[48], ctok[96];
        const char *name = b[i].state == HF_BLOCK_FREE ? "FREE" : b[i].state == HF_BLOCK_MMAP ? "MMAP" : "ALLOC";
        if (shown >= max_tokens) break;
        snprintf(vtok, sizeof vtok, "[%s %zu]", name, b[i].size);
        const char *col = b[i].state == HF_BLOCK_FREE ? C("\033[90m") : b[i].state == HF_BLOCK_MMAP ? C("\033[35m") : C("\033[32m");
        snprintf(ctok, sizeof ctok, "%s%s%s", col, vtok, RST);
        int tl = (int)strlen(vtok);
        if (vis + tl > INNER) {
            fprintf(f, "| %s%*s |\n", line, INNER - vis, "");
            line[0] = 0; vis = 0;
        }
        strcat(line, ctok);
        vis += tl;
        shown++;
    }
    if (vis > 0) fprintf(f, "| %s%*s |\n", line, INNER - vis, "");
    if (n == 0) row(f, "(no blocks)");
    if (total > shown) row(f, "... %zu more block(s) not shown (use 'inspect' for the table)", total - shown);
    hr(f);
    free(b);
}

/* --------------------------------------------------------------- tables */

static const char *state_name(int st)
{
    return st == HF_BLOCK_FREE ? "FREE" : st == HF_BLOCK_MMAP ? "MMAP" : "ALLOC";
}

void hf_viz_blocks(FILE *f, size_t limit)
{
    size_t n, total;
    hf_blockinfo_t *b = take_snapshot(&n, &total);
    fprintf(f, "%s%-4s %-16s %9s %9s %-6s %-6s %s%s\n", C("\033[1m"), "#", "PAYLOAD ADDR", "SIZE", "REQUESTED",
            "STATE", "ID", "CALL SITE", RST);
    uintptr_t first = 0;
    for (size_t i = 0; b && i < n && i < limit; i++) {
        if (!first) first = b[i].header_addr;
        char id[16] = "-", site[96] = "";
        if (b[i].state != HF_BLOCK_FREE) snprintf(id, sizeof id, "#%llu", (unsigned long long)b[i].id);
        if (b[i].file) snprintf(site, sizeof site, "%s:%u", b[i].file, b[i].line);
        const char *col = b[i].state == HF_BLOCK_FREE ? C("\033[90m") : b[i].state == HF_BLOCK_MMAP ? C("\033[35m") : C("\033[32m");
        fprintf(f, "%s%-4zu 0x%014lx %9zu %9zu %-6s %-6s %s%s\n", col, i, (unsigned long)b[i].payload_addr,
                b[i].size, b[i].requested, state_name(b[i].state), id, site, RST);
    }
    if (total > limit) fprintf(f, "... %zu more block(s)\n", total - limit);
    if (total == 0) fprintf(f, "(heap has no blocks)\n");
    free(b);
}

void hf_viz_freelist(FILE *f)
{
    hf_stats_t s;
    hf_stats_get(&s);
    size_t cap = s.free_blocks + 64, total = 0;
    hf_blockinfo_t *b = calloc(cap, sizeof *b);
    size_t n = b ? hf_freelist_snapshot(b, cap, &total) : 0;
    fprintf(f, "Free list (address ordered, %zu node%s):\n", total, total == 1 ? "" : "s");
    if (total == 0) fprintf(f, "  (empty)\n");
    for (size_t i = 0; i < n; i++)
        fprintf(f, "  HEAD%s-> 0x%lx [size %zu]%s\n", i ? "..." : "", (unsigned long)b[i].payload_addr, b[i].size,
                i + 1 == n ? " -> NULL" : "");
    free(b);
}

void hf_viz_block_detail(FILE *f, void *ptr)
{
    hf_blockinfo_t i;
    if (hf_block_info(ptr, &i) != 0) {
        fprintf(f, "%p is not the payload of a valid HeapForge block\n", ptr);
        return;
    }
    fprintf(f, "Block at payload %p\n", ptr);
    fprintf(f, "  header address : 0x%lx (header is %zu bytes)\n", (unsigned long)i.header_addr, (size_t)HF_BLOCK_HDR);
    fprintf(f, "  state          : %s\n", state_name(i.state));
    fprintf(f, "  payload size   : %zu bytes\n", i.size);
    if (i.state != HF_BLOCK_FREE) {
        fprintf(f, "  requested      : %zu bytes (+%u canary, rest is alignment padding)\n", i.requested, HF_CANARY_SIZE);
        fprintf(f, "  allocation id  : #%llu\n", (unsigned long long)i.id);
        fprintf(f, "  call site      : %s", i.file ? i.file : "(none recorded)");
        if (i.file) fprintf(f, ":%u", i.line);
        fputc('\n', f);
    }
    fprintf(f, "  source         : %s\n", i.state == HF_BLOCK_MMAP ? "mmap (own mapping)" : "sbrk heap");
}

/* ------------------------------------------------------------ /proc maps */

void hf_viz_proc_maps(FILE *f)
{
    FILE *m = fopen("/proc/self/maps", "r");
    if (!m) { fprintf(f, "cannot read /proc/self/maps\n"); return; }

    size_t cap = 4096, total = 0;
    hf_blockinfo_t *b = calloc(cap, sizeof *b);
    size_t n = b ? hf_blocks_snapshot(b, cap, &total) : 0;

    fprintf(f, "Kernel's view of this process (/proc/self/maps):\n");
    char line[512];
    int shown = 0;
    while (fgets(line, sizeof line, m)) {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) != 2) continue;
        int is_heap = strstr(line, "[heap]") != NULL;
        int has_mmap_block = 0;
        for (size_t i = 0; i < n; i++)
            if (b[i].state == HF_BLOCK_MMAP && b[i].header_addr >= lo && b[i].header_addr < hi) has_mmap_block = 1;
        if (is_heap || has_mmap_block) {
            line[strcspn(line, "\n")] = 0;
            fprintf(f, "  %s%s\n", line, has_mmap_block ? "   <== HeapForge mmap block" : "   <== brk heap (sbrk)");
            shown++;
        }
    }
    if (!shown) fprintf(f, "  (no [heap] line or HeapForge mmap region present yet)\n");
    fclose(m);
    free(b);
}
