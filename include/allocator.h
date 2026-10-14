/*
 * allocator.h - Allocation Engine: the public HeapForge C API.
 *
 * Semantics (documented choices):
 *   hf_malloc(0)          -> NULL (no allocation, errno untouched)
 *   hf_free(NULL)         -> no-op
 *   hf_realloc(NULL, n)   -> hf_malloc(n)
 *   hf_realloc(p, 0)      -> hf_free(p), returns NULL
 *   failure               -> NULL and errno = ENOMEM (or EINVAL for a bad
 *                            pointer passed to realloc); the original block
 *                            of a failed realloc stays valid.
 *   invalid/double free   -> diagnostic + counter, heap left untouched.
 */
#ifndef HF_ALLOCATOR_H
#define HF_ALLOCATOR_H

#include <stddef.h>
#include "config.h"

void *hf_malloc(size_t size);
void  hf_free(void *ptr);
void *hf_calloc(size_t n, size_t size);
void *hf_realloc(void *ptr, size_t size);

/* Variants that record the call site (shown by the leak report). `file`
 * must be a string with static storage duration (e.g. __FILE__). */
void *hf_malloc_dbg(size_t size, const char *file, int line);
void *hf_calloc_dbg(size_t n, size_t size, const char *file, int line);
void *hf_realloc_dbg(void *ptr, size_t size, const char *file, int line);
#define HF_MALLOC(sz)        hf_malloc_dbg((sz), __FILE__, __LINE__)
#define HF_CALLOC(n, sz)     hf_calloc_dbg((n), (sz), __FILE__, __LINE__)
#define HF_REALLOC(p, sz)    hf_realloc_dbg((p), (sz), __FILE__, __LINE__)

/* ---- configuration (all thread-safe) ------------------------------ */
void hf_set_strategy(hf_strategy_t s);
hf_strategy_t hf_get_strategy(void);
const char *hf_strategy_name(hf_strategy_t s);
int hf_strategy_parse(const char *name, hf_strategy_t *out); /* 0 ok, -1 bad */
void hf_set_mmap_threshold(size_t bytes);
size_t hf_get_mmap_threshold(void);
void hf_set_poison(int on);
void hf_set_verbose(int on);      /* diagnostics to stderr */
void hf_set_paranoid(int on);     /* full heap check after every operation */
void hf_set_heap_limit(size_t bytes); /* cap sbrk growth (0 = none); for tests */

/* ---- maintenance --------------------------------------------------- */
void hf_reset(void);              /* release everything, clear statistics */
size_t hf_trim(void);             /* give the top free block back via sbrk(-n) */

#endif
