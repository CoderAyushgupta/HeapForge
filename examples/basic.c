/* basic.c - allocate, use, resize and free with the HeapForge API. */
#include "heapforge.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    char *s = HF_MALLOC(32);
    strcpy(s, "hello, heapforge");
    int *v = HF_CALLOC(10, sizeof *v);          /* zeroed, overflow-checked */
    s = HF_REALLOC(s, 4096);                    /* data is preserved */
    printf("s = \"%s\"  v[3] = %d\n", s, v[3]);
    hf_viz_blocks(stdout, 10);
    hf_free(s);
    hf_free(v);
    hf_check_report_t r;
    printf("heap check after freeing everything: %s\n", hf_check_heap(&r) ? "PASS" : "FAIL");
    return 0;
}
