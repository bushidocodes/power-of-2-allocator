#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ps_list.h"
#include "p2malloc.h"

/*
 * Simulates sbrk() using a static backing buffer.
 *
 * The original xv6 implementation called sbrk(4096) to grow the heap one page
 * at a time.  sbrk() is not available on Windows, so we maintain a monotonically
 * advancing pointer into a large static array instead.  The semantics are
 * identical: each call returns a fresh, non-overlapping 4096-byte region, or
 * (void*)-1 on exhaustion.
 */
#define BACKING_HEAP_SIZE (16 * 1024 * 1024) /* 16 MB */
static char   backing_heap[BACKING_HEAP_SIZE];
static size_t heap_offset = 0;

static void *fake_sbrk(int n)
{
	if (n <= 0 || (size_t)n > BACKING_HEAP_SIZE - heap_offset) return (void *)-1;
	void *ptr  = backing_heap + heap_offset;
	heap_offset += (size_t)n;
	return ptr;
}

/* ------------------------------------------------------------------ */

struct p2header {
	int pow2; /* stores the user-requested allocation size */
};

struct p2freelist_head {
	int pow2; /* block size managed by this list */
	struct ps_list_head list_head;
};

struct p2freelist_node {
	int pow2; /* block size (when on freelist) or user size (when allocated) */
	struct ps_list list;
};

/*
 * Block sizes: 32, 64, 128, 256, 512, 1024, 2048, 4096 bytes.
 *
 * The xv6 version used 9 buckets starting at 16 bytes (2^4).  On a 64-bit
 * host, sizeof(struct p2freelist_node) == 24 bytes, so a 16-byte block cannot
 * hold the freelist metadata.  Starting at 32 bytes (2^5) fixes this.
 *
 * We keep 8 buckets so the largest block (4096 bytes) still fits exactly in
 * one page — the same invariant as the original.  A 9th bucket at 8192 bytes
 * would overrun the 4096-byte sbrk region used to carve it.
 */
#define NUM_FREELISTS 8
#define BASE_SHIFT    5 /* minimum block = 2^5 = 32 bytes */

static int was_init = 0;
static int allocated = 0; /* sum of all live user-requested sizes */
static int totmem    = 0; /* total bytes obtained from fake_sbrk */
static struct p2freelist_head freelists[NUM_FREELISTS];

static int idx_to_blocksize(int idx)
{
	return 1 << (idx + BASE_SHIFT);
}

static void freelists_init(void)
{
	for (int i = 0; i < NUM_FREELISTS; i++) {
		ps_list_head_init(&freelists[i].list_head);
		freelists[i].pow2 = idx_to_blocksize(i);
	}
	was_init = 1;
}

/*
 * Returns the freelist index for a request of `size` bytes.
 * The index is the smallest bucket whose block fits both the header and the
 * user payload.  Returns -1 if size is out of range.
 */
static int get_freelist_idx(int size)
{
	if (size <= 0) return -1;
	int swh = size + (int)sizeof(struct p2header);
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

void *p2malloc(int size)
{
	if (!was_init) freelists_init();

	int idx = get_freelist_idx(size);
	if (idx == -1) return NULL;

	struct p2freelist_head *freelist = &freelists[idx];

	if (ps_list_head_empty(&freelist->list_head)) {
		void *page = fake_sbrk(4096);
		if (page == (void *)-1) return NULL;

		totmem += 4096;
		for (char *addr = (char *)page; addr < (char *)page + 4096; addr += freelist->pow2) {
			struct p2freelist_node *node = (struct p2freelist_node *)addr;
			ps_list_init_d(node);
			node->pow2 = freelist->pow2;
			ps_list_head_add_d(&freelist->list_head, node);
		}
	}

	if (ps_list_head_empty(&freelist->list_head)) {
		fprintf(stderr, "Error: Failed to populate freelist\n");
		return NULL;
	}

	struct p2freelist_node *first_free =
		ps_list_head_first_d(&freelists[idx].list_head, struct p2freelist_node);
	ps_list_rem_d(first_free);
	memset(first_free, 0, sizeof(first_free->pow2));
	((struct p2header *)first_free)->pow2 = size;
	allocated += size;
	return (char *)first_free + sizeof(struct p2header);
}

/*
 * Insert `node` into `freelist` in ascending address order.
 * Keeping the list sorted by address enables future coalescing and makes
 * debugging easier (blocks come back in a predictable sequence).
 */
static void p2orderedinsert(struct p2freelist_node *node, struct p2freelist_head *freelist)
{
	if (ps_list_head_empty(&freelist->list_head)) {
		ps_list_head_append_d(&freelist->list_head, node);
	} else {
		struct p2freelist_node *predecessor =
			ps_list_head_last_d(&freelist->list_head, struct p2freelist_node);
		for (; predecessor > node; predecessor = ps_list_prev_d(predecessor))
			;
		ps_list_add_d(predecessor, node);
	}
}

void p2free(void *ptr)
{
	struct p2freelist_node *node =
		(struct p2freelist_node *)((char *)ptr - sizeof(struct p2header));
	allocated -= node->pow2;
	memset(ptr, 0, sizeof(node->pow2)); /* zero first int of user data (use-after-free detection) */
	struct p2freelist_head *freelist = &freelists[get_freelist_idx(node->pow2)];
	p2orderedinsert(node, freelist);
}

int p2allocated(void) { return allocated; }
int p2totmem(void)    { return totmem; }

/*
 * Reset all allocator state.  Needed when running multiple test levels in one
 * process; in the original xv6 assignment each level was a separate program.
 */
void p2reset(void)
{
	memset(backing_heap, 0, heap_offset);
	heap_offset = 0;
	was_init    = 0;
	allocated   = 0;
	totmem      = 0;
}
