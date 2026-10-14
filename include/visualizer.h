/*
 * visualizer.h - Heap Observatory: terminal views of the REAL heap state.
 * Every view takes a snapshot of block metadata under the allocator lock
 * and renders only what the snapshot contains.
 */
#ifndef HF_VISUALIZER_H
#define HF_VISUALIZER_H

#include <stdio.h>

void hf_ui_set_color(int on);       /* force on/off (-1 = auto-detect) */
int  hf_ui_color(void);

void hf_viz_banner(FILE *f);
void hf_viz_stats(FILE *f);         /* statistics panel with formulas */
void hf_viz_map(FILE *f);           /* proportional heap bar + block strip */
void hf_viz_blocks(FILE *f, size_t limit);   /* block table */
void hf_viz_freelist(FILE *f);      /* free list in list order */
void hf_viz_block_detail(FILE *f, void *ptr);
void hf_viz_proc_maps(FILE *f);     /* [heap] and mmap regions from /proc/self/maps */

#endif
