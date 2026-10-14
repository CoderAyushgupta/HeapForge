/* concurrency.c - 8 threads share one HeapForge heap. */
#include "heapforge.h"
#include <stdio.h>

int main(void)
{
    hf_stress_result_t r;
    if (hf_stress_run(&hf_ops_heapforge, 8, 100000, 1, &r) != 0) return 2;
    hf_stress_print(stdout, &r, "heapforge");
    return (r.integrity_errors == 0 && r.check_ok == 1 && r.leaked_blocks == 0) ? 0 : 1;
}
