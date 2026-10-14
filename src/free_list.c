/*
 * free_list.c - Free List Manager (address-ordered doubly linked list).
 */
#include "free_list.h"

void hf_fl_insert(struct hf_heap *h, hf_block_t *blk)
{
    hf_block_t *prev = NULL, *cur = h->free_head;
    while (cur && (uintptr_t)cur < (uintptr_t)blk) {
        prev = cur;
        cur = cur->free_next;
    }
    blk->free_prev = prev;
    blk->free_next = cur;
    if (prev) prev->free_next = blk; else h->free_head = blk;
    if (cur) cur->free_prev = blk;
}

void hf_fl_remove(struct hf_heap *h, hf_block_t *blk)
{
    if (blk->free_prev) blk->free_prev->free_next = blk->free_next;
    else h->free_head = blk->free_next;
    if (blk->free_next) blk->free_next->free_prev = blk->free_prev;
    blk->free_prev = blk->free_next = NULL;
}

hf_block_t *hf_fl_find(struct hf_heap *h, size_t need, hf_strategy_t strategy)
{
    hf_block_t *best = NULL;
    for (hf_block_t *b = h->free_head; b; b = b->free_next) {
        if (b->size < need)
            continue;
        switch (strategy) {
        case HF_STRATEGY_FIRST_FIT:
            return b;                       /* lowest address that fits */
        case HF_STRATEGY_BEST_FIT:
            if (!best || b->size < best->size) best = b;
            if (b->size == need) return b;  /* cannot do better */
            break;
        case HF_STRATEGY_WORST_FIT:
            if (!best || b->size > best->size) best = b;
            break;
        }
    }
    return best;
}
