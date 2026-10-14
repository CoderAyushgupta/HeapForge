/*
 * config.h - compile-time tunables and constants for HeapForge.
 *
 * Every constant here has a documented reason (see docs/design.md).
 */
#ifndef HF_CONFIG_H
#define HF_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#define HF_VERSION "1.0.0"

/* Payload alignment. 16 bytes == alignof(max_align_t) on x86-64 Linux, so
 * any C object type can live in a returned block (SSE types included). */
#define HF_ALIGNMENT 16u

/* Bytes of guard value ("canary") placed directly after the user-requested
 * bytes. One byte of overflow already hits it. */
#define HF_CANARY_SIZE 8u

/* Smallest payload a block may have (keeps blocks usable and 16B aligned). */
#define HF_MIN_PAYLOAD 32u

/* A free block is only split if the remainder would have at least this many
 * payload bytes (in addition to its own header). Prevents creating
 * unusable "sliver" blocks that only add list length and header overhead. */
#define HF_MIN_SPLIT_PAYLOAD 64u

/* The sbrk() heap grows in chunks of at least this size to amortise the
 * cost of the system call (must be a multiple of the page size). */
#define HF_HEAP_CHUNK (128u * 1024u)

/* Requests of at least this many bytes bypass the sbrk heap and get their
 * own anonymous mmap() mapping (same default as glibc's M_MMAP_THRESHOLD). */
#define HF_MMAP_THRESHOLD_DEFAULT (128u * 1024u)

/* Requests above this are rejected up-front (also guards size arithmetic). */
#define HF_MAX_REQUEST ((size_t)1 << 40)

/* Block header "magic" values. They double as the allocation-state tag and
 * as a first-line corruption/invalid-pointer detector. */
#define HF_MAGIC_ALLOC 0xA110C8EDu
#define HF_MAGIC_FREE  0xF4EEB10Cu
#define HF_MAGIC_DEAD  0xDEADB10Cu /* header swallowed by coalescing */

/* Base of the per-block canary; the actual value is XORed with the block
 * address so a stale copy of one canary does not validate another block. */
#define HF_CANARY_BASE UINT64_C(0xC0FFEE5AFEC0DE11)

/* Byte written over freed payloads (helps expose use-after-free). */
#define HF_POISON_BYTE 0xDD

/* Free-block selection policy. */
typedef enum {
    HF_STRATEGY_FIRST_FIT = 0,
    HF_STRATEGY_BEST_FIT,
    HF_STRATEGY_WORST_FIT
} hf_strategy_t;

#endif /* HF_CONFIG_H */
