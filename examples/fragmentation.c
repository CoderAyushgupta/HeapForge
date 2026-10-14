/* fragmentation.c - create holes by freeing every other block and read the
 * real fragmentation metrics, then show how coalescing repairs the heap. */
#include "heapforge.h"
#include <stdio.h>

int main(void)
{
    void *p[32];
    for (int i = 0; i < 32; i++) p[i] = hf_malloc(200);
    for (int i = 0; i < 32; i += 2) hf_free(p[i]);

    hf_stats_t s;
    hf_stats_get(&s);
    printf("free blocks: %zu  free bytes: %zu  largest free: %zu\n", s.free_blocks, s.free_bytes, s.largest_free);
    printf("external fragmentation = 1 - %zu/%zu = %.2f%%\n", s.largest_free, s.free_bytes, s.external_frag * 100.0);
    hf_viz_map(stdout);

    for (int i = 1; i < 32; i += 2) hf_free(p[i]);
    hf_stats_get(&s);
    printf("after freeing the rest: free blocks %zu, external fragmentation %.2f%%\n", s.free_blocks, s.external_frag * 100.0);
    return 0;
}
