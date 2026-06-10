/*
 * p2malloc — power-of-2 slab allocator with OS fallback for large requests.
 *
 * Slab layer  : 8 segregated free lists for block sizes 32–4096 bytes.
 *               Each list is protected by its own mutex.
 * Large layer : allocations above P2_SLAB_MAX go straight to the OS via
 *               mmap (POSIX) or VirtualAlloc (Windows) and are returned
 *               to the OS on free.
 * Alignment   : every returned pointer is aligned to _Alignof(max_align_t)
 *               (16 bytes on most 64-bit platforms) by sizing the header to
 *               that boundary.
 * Thread safety: per-freelist mutex; global counters use C11 atomics.
 */

#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ps_list.h"
#include "p2malloc.h"

/* ------------------------------------------------------------------ */
/* Platform: OS memory and mutual exclusion                            */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
#  include <windows.h>

static void *os_alloc(size_t n)
{
    return VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

static void os_free(void *p, size_t n)
{
    (void)n;
    VirtualFree(p, 0, MEM_RELEASE);
}

typedef SRWLOCK p2_mutex_t;
#define MUTEX_INIT(m)   InitializeSRWLock(&(m))
#define MUTEX_LOCK(m)   AcquireSRWLockExclusive(&(m))
#define MUTEX_UNLOCK(m) ReleaseSRWLockExclusive(&(m))

#else /* POSIX */
#  include <sys/mman.h>
#  include <pthread.h>

static void *os_alloc(size_t n)
{
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static void os_free(void *p, size_t n)
{
    munmap(p, n);
}

typedef pthread_mutex_t p2_mutex_t;
#define MUTEX_INIT(m)   pthread_mutex_init(&(m), NULL)
#define MUTEX_LOCK(m)   pthread_mutex_lock(&(m))
#define MUTEX_UNLOCK(m) pthread_mutex_unlock(&(m))

#endif /* _WIN32 */

/* ------------------------------------------------------------------ */
/* Allocation header                                                   */
/* ------------------------------------------------------------------ */

/*
 * Sits immediately before the pointer returned to the caller.
 * Aligned to max_align_t so that header + sizeof(p2header) keeps every
 * returned pointer naturally aligned.  Uses C11 _Alignas; no GCC extensions.
 */
typedef struct {
    _Alignas(max_align_t) size_t size; /* user-requested allocation size */
    size_t alloc_size;                 /* 0 = slab block; non-zero = bytes from OS (large) */
} p2header;

_Static_assert(sizeof(p2header) == P2_HEADER_SIZE,
               "p2header size must equal P2_HEADER_SIZE");

#define IS_LARGE(h) ((h)->alloc_size != 0)

/* Round n up to the next 4096-byte boundary */
static size_t page_align(size_t n)
{
    return (n + 4095) & ~(size_t)4095;
}

/* ------------------------------------------------------------------ */
/* Slab allocator state                                                */
/* ------------------------------------------------------------------ */

/*
 * Block sizes: 32, 64, 128, 256, 512, 1024, 2048, 4096 bytes  (2^5 – 2^12).
 * Minimum 32 B: sizeof(p2freelist_node) == 24 on 64-bit, so any block ≥ 32
 * can hold the freelist metadata while on the free list.
 * Maximum 4096 B: the largest block fits exactly in one OS page.
 */
#define NUM_FREELISTS 8
#define BASE_SHIFT    5
#define PAGE_BYTES    4096

struct p2freelist_head {
    int                 pow2;
    struct ps_list_head list_head;
    p2_mutex_t          lock;
};

struct p2freelist_node {
    /* list MUST be first so both pointers (bytes 0–15) stay inside the
     * header region and never overlap with user data (bytes 16+). */
    struct ps_list list;
    int            pow2;
};

static _Atomic int    init_state = 0;  /* 0 = uninit, 1 = initing, 2 = ready */
static _Atomic size_t allocated  = 0;
static _Atomic size_t totmem     = 0;
static struct p2freelist_head freelists[NUM_FREELISTS];

static void freelists_init(void)
{
    for (int i = 0; i < NUM_FREELISTS; i++) {
        ps_list_head_init(&freelists[i].list_head);
        freelists[i].pow2 = 1 << (i + BASE_SHIFT);
        MUTEX_INIT(freelists[i].lock);
    }
}

static void ensure_init(void)
{
    int expected = 0;
    if (atomic_compare_exchange_strong(&init_state, &expected, 1)) {
        freelists_init();
        atomic_store(&init_state, 2);
    } else {
        while (atomic_load(&init_state) != 2)
            ; /* spin — init is fast, no sleep needed */
    }
}

/*
 * Map a user-requested size to a freelist index.
 * Returns -1 when size + header exceeds one page (caller uses large path).
 */
static int get_freelist_idx(size_t size)
{
    size_t swh = size + P2_HEADER_SIZE;
    if      (swh > 4096) return -1;
    else if (swh > 2048) return 7;
    else if (swh > 1024) return 6;
    else if (swh > 512)  return 5;
    else if (swh > 256)  return 4;
    else if (swh > 128)  return 3;
    else if (swh > 64)   return 2;
    else if (swh > 32)   return 1;
    else                 return 0;
}

/*
 * Insert node into freelist in ascending address order.
 *
 * Ordering enables future coalescing.  We explicitly detect the sentinel
 * head rather than relying on its address being numerically lowest — that
 * assumption breaks when pages come from mmap/VirtualAlloc rather than a
 * static array, since OS pages can land anywhere in virtual address space.
 */
static void p2orderedinsert(struct p2freelist_node *node,
                            struct p2freelist_head *fl)
{
    if (ps_list_head_empty(&fl->list_head)) {
        ps_list_head_append_d(&fl->list_head, node);
        return;
    }

    struct p2freelist_node *cursor =
        ps_list_head_last_d(&fl->list_head, struct p2freelist_node);

    while ((uintptr_t)cursor > (uintptr_t)node) {
        if (cursor->list.previous == &fl->list_head.list) {
            /* node sorts before everything in the list */
            ps_list_ll_add(&fl->list_head.list, &node->list);
            return;
        }
        cursor = ps_list_prev_d(cursor);
    }
    ps_list_add_d(cursor, node);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void *p2malloc(size_t size)
{
    if (size == 0) return NULL;
    if (size > SIZE_MAX - P2_HEADER_SIZE) return NULL; /* overflow guard */
    ensure_init();

    int idx = get_freelist_idx(size);

    /* --- Large allocation: bypass the slab, allocate directly from the OS --- */
    if (idx == -1) {
        size_t alloc_size = page_align(P2_HEADER_SIZE + size);
        p2header *hdr = os_alloc(alloc_size);
        if (!hdr) return NULL;
        hdr->size       = size;
        hdr->alloc_size = alloc_size;
        atomic_fetch_add(&allocated, size);
        atomic_fetch_add(&totmem, alloc_size);
        return (char *)hdr + P2_HEADER_SIZE;
    }

    /* --- Slab allocation --- */
    struct p2freelist_head *fl = &freelists[idx];
    MUTEX_LOCK(fl->lock);

    if (ps_list_head_empty(&fl->list_head)) {
        void *page = os_alloc(PAGE_BYTES);
        if (!page) { MUTEX_UNLOCK(fl->lock); return NULL; }
        atomic_fetch_add(&totmem, PAGE_BYTES);
        for (char *a = (char *)page; a < (char *)page + PAGE_BYTES; a += fl->pow2) {
            struct p2freelist_node *n = (struct p2freelist_node *)a;
            ps_list_init_d(n);
            ps_list_head_add_d(&fl->list_head, n);
        }
    }

    struct p2freelist_node *first =
        ps_list_head_first_d(&fl->list_head, struct p2freelist_node);
    ps_list_rem_d(first);
    MUTEX_UNLOCK(fl->lock);

    p2header *hdr = (p2header *)first;
    hdr->size       = size;
    hdr->alloc_size = 0; /* mark as slab-managed */
    atomic_fetch_add(&allocated, size);
    return (char *)hdr + P2_HEADER_SIZE;
}

void p2free(void *ptr)
{
    if (!ptr) return; /* NULL is a no-op, matching standard free() */

    p2header *hdr = (p2header *)((char *)ptr - P2_HEADER_SIZE);

    if (IS_LARGE(hdr)) {
        size_t size       = hdr->size;
        size_t alloc_size = hdr->alloc_size;
        atomic_fetch_sub(&allocated, size);
        atomic_fetch_sub(&totmem,    alloc_size);
        os_free(hdr, alloc_size);
        return;
    }

    size_t size = hdr->size;
    atomic_fetch_sub(&allocated, size);

    /* Zero one pointer-width word at the start of the payload as a
     * use-after-free tripwire (mirrors the original xv6 behaviour). */
    memset(ptr, 0, sizeof(size_t));

    int idx = get_freelist_idx(size);
    assert(idx >= 0 && idx < NUM_FREELISTS);
    if (idx < 0) return;
    struct p2freelist_head *fl = &freelists[idx];
    struct p2freelist_node *node = (struct p2freelist_node *)hdr;

    MUTEX_LOCK(fl->lock);
    p2orderedinsert(node, fl);
    MUTEX_UNLOCK(fl->lock);
}

void *p2calloc(size_t nmemb, size_t size)
{
    if (nmemb == 0 || size == 0) return NULL;
    if (nmemb > SIZE_MAX / size) return NULL; /* overflow guard */
    void *p = p2malloc(nmemb * size);
    if (p) memset(p, 0, nmemb * size);
    return p;
}

void *p2realloc(void *ptr, size_t size)
{
    if (!ptr)      return p2malloc(size);
    if (size == 0) { p2free(ptr); return NULL; }

    p2header *hdr     = (p2header *)((char *)ptr - P2_HEADER_SIZE);
    size_t    old_size = hdr->size;

    /* Same slab bucket: update the header in-place, no copy needed */
    if (!IS_LARGE(hdr) && get_freelist_idx(size) == get_freelist_idx(old_size)) {
        atomic_fetch_sub(&allocated, old_size);
        atomic_fetch_add(&allocated, size);
        hdr->size = size;
        return ptr;
    }

    void *new_ptr = p2malloc(size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, old_size < size ? old_size : size);
    p2free(ptr);
    return new_ptr;
}

size_t p2allocated(void) { return atomic_load(&allocated); }
size_t p2totmem(void)    { return atomic_load(&totmem); }

void p2reset(void)
{
    /* Re-initialise list heads and counters; preserve mutexes (already init'd).
     * Previously claimed OS pages are abandoned and released at process exit. */
    if (atomic_load(&init_state) == 2) {
        for (int i = 0; i < NUM_FREELISTS; i++)
            ps_list_head_init(&freelists[i].list_head);
    } else {
        freelists_init();
        atomic_store(&init_state, 2);
    }
    atomic_store(&allocated, 0);
    atomic_store(&totmem,    0);
}
