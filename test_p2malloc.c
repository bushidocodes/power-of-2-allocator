#include "unity/unity.h"
#include "p2malloc.h"
#include <string.h>

void setUp(void)    { p2reset(); }
void tearDown(void) {}

/* Cast size_t to unsigned long for portable Unity int comparisons */
#define ASSERT_SZ_EQ(expected, actual) \
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(expected), (uint64_t)(actual))

/* ------------------------------------------------------------------ */
/* Invalid / edge inputs                                               */
/* ------------------------------------------------------------------ */

void test_malloc_zero_returns_null(void)
{
	TEST_ASSERT_NULL(p2malloc(0));
}

void test_malloc_negative_size_is_zero_returns_null(void)
{
	/* size_t wraps; SIZE_MAX is astronomically large and os_alloc will fail */
	TEST_ASSERT_NULL(p2malloc((size_t)-1));
}

/* ------------------------------------------------------------------ */
/* Allocation tracking                                                 */
/* ------------------------------------------------------------------ */

void test_initial_allocated_is_zero(void)
{
	ASSERT_SZ_EQ(0, p2allocated());
}

void test_initial_totmem_is_zero(void)
{
	ASSERT_SZ_EQ(0, p2totmem());
}

void test_malloc_returns_nonnull(void)
{
	TEST_ASSERT_NOT_NULL(p2malloc(12));
}

void test_malloc_tracks_allocated(void)
{
	p2malloc(12);
	ASSERT_SZ_EQ(12, p2allocated());
}

void test_malloc_multiple_accumulates_allocated(void)
{
	p2malloc(12);
	p2malloc(12);
	p2malloc(12);
	ASSERT_SZ_EQ(36, p2allocated());
}

void test_malloc_mixed_sizes_tracks_allocated(void)
{
	p2malloc(10);
	p2malloc(50);
	p2malloc(200);
	ASSERT_SZ_EQ(260, p2allocated());
}

void test_first_alloc_claims_one_page(void)
{
	p2malloc(12);
	ASSERT_SZ_EQ(4096, p2totmem());
}

void test_same_bucket_allocs_share_page(void)
{
	/* idx 0: 32-byte blocks → 128 per page; 8 allocs should not trigger a 2nd page */
	for (int i = 0; i < 8; i++) TEST_ASSERT_NOT_NULL(p2malloc(12));
	ASSERT_SZ_EQ(4096, p2totmem());
}

void test_overflow_claims_second_page(void)
{
	/* Fill the first page (128 × 32-byte blocks), then one more */
	for (int i = 0; i < 128; i++) p2malloc(12);
	ASSERT_SZ_EQ(4096, p2totmem());
	p2malloc(12);
	ASSERT_SZ_EQ(8192, p2totmem());
}

void test_different_buckets_use_separate_pages(void)
{
	p2malloc(16); /* idx 0: 32-byte block  (16 + 16 = 32) */
	p2malloc(17); /* idx 1: 64-byte block  (17 + 16 = 33 > 32) */
	ASSERT_SZ_EQ(8192, p2totmem());
}

void test_allocs_return_distinct_pointers(void)
{
	void *a = p2malloc(12);
	void *b = p2malloc(12);
	void *c = p2malloc(12);
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_NOT_NULL(b);
	TEST_ASSERT_NOT_NULL(c);
	TEST_ASSERT_TRUE(a != b);
	TEST_ASSERT_TRUE(b != c);
	TEST_ASSERT_TRUE(a != c);
}

/* ------------------------------------------------------------------ */
/* Alignment                                                           */
/* ------------------------------------------------------------------ */

void test_returned_pointer_is_aligned(void)
{
	/* Every returned pointer must be aligned to __BIGGEST_ALIGNMENT__ (16 B) */
	for (int size = 1; size <= 512; size++) {
		void *p = p2malloc((size_t)size);
		TEST_ASSERT_NOT_NULL(p);
		TEST_ASSERT_EQUAL_INT(0, (int)((uintptr_t)p % __BIGGEST_ALIGNMENT__));
	}
}

void test_large_alloc_is_aligned(void)
{
	void *p = p2malloc(8000);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_EQUAL_INT(0, (int)((uintptr_t)p % __BIGGEST_ALIGNMENT__));
	p2free(p);
}

/* ------------------------------------------------------------------ */
/* Free behaviour                                                      */
/* ------------------------------------------------------------------ */

void test_free_null_is_noop(void)
{
	/* Must not crash */
	p2free(NULL);
	ASSERT_SZ_EQ(0, p2allocated());
}

void test_free_decrements_allocated(void)
{
	void *p = p2malloc(12);
	ASSERT_SZ_EQ(12, p2allocated());
	p2free(p);
	ASSERT_SZ_EQ(0, p2allocated());
}

void test_free_all_returns_allocated_to_zero(void)
{
	void *a = p2malloc(10);
	void *b = p2malloc(20);
	void *c = p2malloc(30);
	p2free(a); p2free(b); p2free(c);
	ASSERT_SZ_EQ(0, p2allocated());
}

void test_free_zeroes_first_word_of_payload(void)
{
	int *p = (int *)p2malloc(12);
	*p = 0xDEADBEEF;
	p2free(p);
	TEST_ASSERT_EQUAL_INT(0, *p);
}

void test_free_does_not_shrink_totmem(void)
{
	void *p = p2malloc(12);
	size_t before = p2totmem();
	p2free(p);
	ASSERT_SZ_EQ(before, p2totmem());
}

/* ------------------------------------------------------------------ */
/* Freelist reuse                                                      */
/* ------------------------------------------------------------------ */

void test_freelist_reuse_avoids_new_page(void)
{
	void *ptrs[128];
	for (int i = 0; i < 128; i++) ptrs[i] = p2malloc(12);
	size_t mem = p2totmem();
	for (int i = 0; i < 128; i++) p2free(ptrs[i]);
	for (int i = 0; i < 128; i++) TEST_ASSERT_NOT_NULL(p2malloc(12));
	ASSERT_SZ_EQ(mem, p2totmem());
}

/* ------------------------------------------------------------------ */
/* Bucket slot counts                                                  */
/* ------------------------------------------------------------------ */

/* idx 0: 32-byte blocks, 128 per page.
 * Max payload for idx 0: 32 - P2_HEADER_SIZE = 32 - 16 = 16 bytes.
 * size=16 → swh=32 ≤ 32 → idx 0.
 * size=17 → swh=33 > 32 → idx 1.                                     */
void test_idx0_holds_128_blocks_per_page(void)
{
	for (int i = 0; i < 128; i++) TEST_ASSERT_NOT_NULL(p2malloc(16));
	ASSERT_SZ_EQ(4096, p2totmem());
	p2malloc(16);
	ASSERT_SZ_EQ(8192, p2totmem());
}

/* idx 1: 64-byte blocks, 64 per page */
void test_idx1_holds_64_blocks_per_page(void)
{
	for (int i = 0; i < 64; i++) TEST_ASSERT_NOT_NULL(p2malloc(17));
	ASSERT_SZ_EQ(4096, p2totmem());
	p2malloc(17);
	ASSERT_SZ_EQ(8192, p2totmem());
}

/* idx 7: 4096-byte blocks, 1 per page.
 * Max slab payload: 4096 - 16 = 4080 = P2_SLAB_MAX. */
void test_idx7_holds_1_block_per_page(void)
{
	TEST_ASSERT_NOT_NULL(p2malloc(P2_SLAB_MAX));
	ASSERT_SZ_EQ(4096, p2totmem());
	TEST_ASSERT_NOT_NULL(p2malloc(P2_SLAB_MAX));
	ASSERT_SZ_EQ(8192, p2totmem());
}

/* ------------------------------------------------------------------ */
/* Large allocations (above P2_SLAB_MAX → direct OS path)            */
/* ------------------------------------------------------------------ */

void test_large_alloc_succeeds(void)
{
	void *p = p2malloc(P2_SLAB_MAX + 1);
	TEST_ASSERT_NOT_NULL(p);
	p2free(p);
}

void test_large_alloc_tracks_allocated(void)
{
	size_t sz = P2_SLAB_MAX + 100;
	p2malloc(sz);
	ASSERT_SZ_EQ(sz, p2allocated());
}

void test_large_alloc_free_recovers_allocated_and_totmem(void)
{
	size_t sz = P2_SLAB_MAX + 100;
	void *p = p2malloc(sz);
	size_t mem = p2totmem();
	TEST_ASSERT_TRUE(mem > 0);
	p2free(p);
	ASSERT_SZ_EQ(0, p2allocated());
	/* Large pages are returned to the OS on free */
	ASSERT_SZ_EQ(0, p2totmem());
}

void test_large_alloc_is_readable_and_writable(void)
{
	size_t sz = 1024 * 1024; /* 1 MB */
	char *p = (char *)p2malloc(sz);
	TEST_ASSERT_NOT_NULL(p);
	memset(p, 0xAB, sz);
	TEST_ASSERT_EQUAL_HEX8(0xAB, p[0]);
	TEST_ASSERT_EQUAL_HEX8(0xAB, p[sz - 1]);
	p2free(p);
}

/* ------------------------------------------------------------------ */
/* p2calloc                                                            */
/* ------------------------------------------------------------------ */

void test_calloc_returns_nonnull(void)
{
	TEST_ASSERT_NOT_NULL(p2calloc(4, 8));
}

void test_calloc_zeroes_memory(void)
{
	int *p = (int *)p2calloc(16, sizeof(int));
	TEST_ASSERT_NOT_NULL(p);
	for (int i = 0; i < 16; i++)
		TEST_ASSERT_EQUAL_INT(0, p[i]);
}

void test_calloc_zero_count_returns_null(void)
{
	TEST_ASSERT_NULL(p2calloc(0, 8));
}

void test_calloc_zero_size_returns_null(void)
{
	TEST_ASSERT_NULL(p2calloc(4, 0));
}

void test_calloc_overflow_returns_null(void)
{
	/* nmemb * size overflows size_t */
	TEST_ASSERT_NULL(p2calloc((size_t)-1, 2));
}

/* ------------------------------------------------------------------ */
/* p2realloc                                                           */
/* ------------------------------------------------------------------ */

void test_realloc_null_ptr_acts_as_malloc(void)
{
	void *p = p2realloc(NULL, 32);
	TEST_ASSERT_NOT_NULL(p);
}

void test_realloc_zero_size_acts_as_free(void)
{
	void *p = p2malloc(32);
	p2realloc(p, 0);
	ASSERT_SZ_EQ(0, p2allocated());
}

void test_realloc_same_bucket_returns_same_ptr(void)
{
	/* Both 12 and 16 fit in the 32-byte bucket; realloc should be in-place */
	void *a = p2malloc(12);
	void *b = p2realloc(a, 16);
	TEST_ASSERT_EQUAL_PTR(a, b);
	ASSERT_SZ_EQ(16, p2allocated());
}

void test_realloc_larger_bucket_copies_data(void)
{
	char *p = (char *)p2malloc(10);
	memset(p, 'X', 10);
	char *q = (char *)p2realloc(p, 100); /* moves to a bigger bucket */
	TEST_ASSERT_NOT_NULL(q);
	/* First 10 bytes must be preserved */
	for (int i = 0; i < 10; i++)
		TEST_ASSERT_EQUAL_HEX8('X', (unsigned char)q[i]);
}

void test_realloc_smaller_bucket_copies_data(void)
{
	char *p = (char *)p2malloc(200);
	memset(p, 'Y', 200);
	char *q = (char *)p2realloc(p, 10); /* shrinks to smaller bucket */
	TEST_ASSERT_NOT_NULL(q);
	for (int i = 0; i < 10; i++)
		TEST_ASSERT_EQUAL_HEX8('Y', (unsigned char)q[i]);
}

/* ------------------------------------------------------------------ */

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_malloc_zero_returns_null);
	RUN_TEST(test_malloc_negative_size_is_zero_returns_null);

	RUN_TEST(test_initial_allocated_is_zero);
	RUN_TEST(test_initial_totmem_is_zero);
	RUN_TEST(test_malloc_returns_nonnull);
	RUN_TEST(test_malloc_tracks_allocated);
	RUN_TEST(test_malloc_multiple_accumulates_allocated);
	RUN_TEST(test_malloc_mixed_sizes_tracks_allocated);
	RUN_TEST(test_first_alloc_claims_one_page);
	RUN_TEST(test_same_bucket_allocs_share_page);
	RUN_TEST(test_overflow_claims_second_page);
	RUN_TEST(test_different_buckets_use_separate_pages);
	RUN_TEST(test_allocs_return_distinct_pointers);

	RUN_TEST(test_returned_pointer_is_aligned);
	RUN_TEST(test_large_alloc_is_aligned);

	RUN_TEST(test_free_null_is_noop);
	RUN_TEST(test_free_decrements_allocated);
	RUN_TEST(test_free_all_returns_allocated_to_zero);
	RUN_TEST(test_free_zeroes_first_word_of_payload);
	RUN_TEST(test_free_does_not_shrink_totmem);

	RUN_TEST(test_freelist_reuse_avoids_new_page);

	RUN_TEST(test_idx0_holds_128_blocks_per_page);
	RUN_TEST(test_idx1_holds_64_blocks_per_page);
	RUN_TEST(test_idx7_holds_1_block_per_page);

	RUN_TEST(test_large_alloc_succeeds);
	RUN_TEST(test_large_alloc_tracks_allocated);
	RUN_TEST(test_large_alloc_free_recovers_allocated_and_totmem);
	RUN_TEST(test_large_alloc_is_readable_and_writable);

	RUN_TEST(test_calloc_returns_nonnull);
	RUN_TEST(test_calloc_zeroes_memory);
	RUN_TEST(test_calloc_zero_count_returns_null);
	RUN_TEST(test_calloc_zero_size_returns_null);
	RUN_TEST(test_calloc_overflow_returns_null);

	RUN_TEST(test_realloc_null_ptr_acts_as_malloc);
	RUN_TEST(test_realloc_zero_size_acts_as_free);
	RUN_TEST(test_realloc_same_bucket_returns_same_ptr);
	RUN_TEST(test_realloc_larger_bucket_copies_data);
	RUN_TEST(test_realloc_smaller_bucket_copies_data);

	return UNITY_END();
}
