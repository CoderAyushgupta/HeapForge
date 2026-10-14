/*
 * free_list.h - Free List Manager.
 *
 * Doubly linked list of FREE blocks kept in ADDRESS ORDER. Address order
 * makes first-fit deterministic ("lowest address that fits"), keeps the
 * list aligned with the physical heap picture, and makes coalescing checks
 * easy to reason about.
 *
 * Costs: insert O(f), remove O(1), find O(f) where f = #free blocks.
 */
#ifndef HF_FREE_LIST_H
#define HF_FREE_LIST_H

#include "heap.h"

void hf_fl_insert(struct hf_heap *h, hf_block_t *blk);
void hf_fl_remove(struct hf_heap *h, hf_block_t *blk);
/* Pick a free block with payload >= need according to `strategy`. */
hf_block_t *hf_fl_find(struct hf_heap *h, size_t need, hf_strategy_t strategy);

#endif
