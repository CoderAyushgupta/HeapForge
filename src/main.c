/*
 * main.c - HeapForge command-line entry point (argument parsing only).
 */
#include "allocator.h"
#include "benchmark.h"
#include "cli.h"
#include "visualizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--bench-child") == 0)
        return hf_bench_child_main(argc, argv);

    const char *script = NULL;
    int i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { hf_cli_usage(); return 0; }
        if (!strcmp(argv[i], "--version")) { printf("HeapForge %s\n", HF_VERSION); return 0; }
        if (!strcmp(argv[i], "--no-color")) { hf_ui_set_color(0); continue; }
        if (!strcmp(argv[i], "--strategy") && i + 1 < argc) {
            hf_strategy_t s;
            if (hf_strategy_parse(argv[i + 1], &s) != 0) { fprintf(stderr, "error: unknown strategy '%s'\n", argv[i + 1]); return 2; }
            hf_set_strategy(s);
            i++;
            continue;
        }
        if (!strcmp(argv[i], "--threshold") && i + 1 < argc) {
            char *end;
            unsigned long long v = strtoull(argv[i + 1], &end, 10);
            if (*end || v == 0) { fprintf(stderr, "error: bad threshold '%s'\n", argv[i + 1]); return 2; }
            hf_set_mmap_threshold((size_t)v);
            i++;
            continue;
        }
        if (!strcmp(argv[i], "-c") && i + 1 < argc) { script = argv[i + 1]; i++; continue; }
        break;
    }

    int rc;
    if (script) {
        rc = hf_cli_script(script);
    } else if (i < argc) {
        if (!hf_cli_is_command(argv[i])) {
            fprintf(stderr, "error: unknown command or option '%s' (try --help)\n", argv[i]);
            return 2;
        }
        char line[512] = "";
        for (int k = i; k < argc; k++) {
            if (strlen(line) + strlen(argv[k]) + 2 >= sizeof line) { fprintf(stderr, "error: arguments too long\n"); return 2; }
            strcat(line, argv[k]);
            strcat(line, " ");
        }
        rc = hf_cli_execute(line);
    } else {
        rc = hf_cli_repl();
    }
    return rc < 0 ? 0 : rc;
}
