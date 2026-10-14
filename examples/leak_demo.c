/* leak_demo.c - forget to free two blocks and let the leak report find them. */
#include "heapforge.h"
#include <stdio.h>

static void leaky(void)
{
    void *kept = HF_MALLOC(128);
    void *forgotten = HF_MALLOC(256);   /* never freed */
    (void)forgotten;
    hf_free(kept);
}

int main(void)
{
    leaky();
    void *also_forgotten = HF_MALLOC(64);
    (void)also_forgotten;
    size_t live = hf_leak_report(stdout, 20, NULL);
    return live == 2 ? 0 : 1;           /* exactly the two blocks we leaked */
}
