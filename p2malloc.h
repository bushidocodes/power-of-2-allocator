#ifndef P2MALLOC_H
#define P2MALLOC_H

#include <stddef.h>

/*
 * Size of the header prepended to every allocation.
 * Equals sizeof(p2header): 2*sizeof(size_t) rounded up to max_align_t
 * alignment, so every returned pointer satisfies the platform's maximum
 * alignment requirement.  Uses only C11 standard types/operators.
 */
#define P2_HEADER_SIZE \
    (((size_t)2 * sizeof(size_t) + _Alignof(max_align_t) - 1) \
     & ~(_Alignof(max_align_t) - 1))

/*
 * Largest payload served by the slab allocator.
 * Requests above this succeed via direct OS page allocation.
 */
#define P2_SLAB_MAX     (4096U - P2_HEADER_SIZE)

void  *p2malloc(size_t size);
void   p2free(void *ptr);
void  *p2calloc(size_t nmemb, size_t size);
void  *p2realloc(void *ptr, size_t size);
size_t p2allocated(void);
size_t p2totmem(void);

/*
 * Reset all allocator state.  Not thread-safe; call only when no other
 * threads are using the allocator.  Intended for use between test cases.
 *
 * After p2reset(), p2allocated() returns 0 and p2totmem() returns 0, but
 * slab pages claimed before the reset are NOT returned to the OS — they
 * are abandoned and released at process exit.  p2totmem() therefore
 * reflects only allocations made after the most recent p2reset(), not
 * total OS memory held by the process.  Do not use p2totmem() across a
 * reset boundary to measure peak or cumulative memory consumption.
 */
void p2reset(void);

/*
 * Drop-in replacement for the system allocator.
 * Define P2_REPLACE_SYSTEM_MALLOC before including this header to redirect
 * malloc/free/calloc/realloc to the p2 allocator.
 */
#ifdef P2_REPLACE_SYSTEM_MALLOC
#  define malloc(n)     p2malloc(n)
#  define free(p)       p2free(p)
#  define calloc(n, s)  p2calloc((n), (s))
#  define realloc(p, n) p2realloc((p), (n))
#endif

#endif /* P2MALLOC_H */
