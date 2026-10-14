/* corruption_demo.c - a classic off-by-one buffer overflow, caught by the canary. */
#include "heapforge.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    char *name = HF_MALLOC(8);
    strcpy(name, "12345678");           /* needs 9 bytes: the NUL lands on the canary */
    hf_check_report_t r;
    printf("consistency check: %s\n", hf_check_heap(&r) ? "PASS" : "FAIL");
    hf_check_print(stdout, &r);
    printf("\nnow freeing the block - HeapForge reports the corruption:\n");
    fflush(stdout);
    hf_free(name);
    return 0;
}
