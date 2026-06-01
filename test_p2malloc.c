#include "unity/unity.h"
#include "p2malloc.h"

void setUp(void)    { p2reset(); }
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Invalid / boundary inputs                                           */
/* ------------------------------------------------------------------ */

void test_malloc_zero_returns_null(void)
{
	TEST_ASSERT_NULL(p2malloc(0));
}

void test_malloc_negative_returns_null(void)
{
	TEST_ASSERT_NULL(p2malloc(-1));
}

/* 4093 + 4-byte header = 4097 > 4096 — exceeds largest bucket */
void test_malloc_too_large_returns_null(void)
{
	TEST_ASSERT_NULL(p2malloc(4093));
}

/* 4092 + 4 = 4096 — exactly fills the largest bucket (idx 7) */
void test_malloc_max_valid_returns_nonnull(void)
{
	TEST_ASSERT_NOT_NULL(p2malloc(4092));
}

/* ------------------------------------------------------------------ */
/* Allocation basics                                                   */
/* ------------------------------------------------------------------ */

void test_malloc_returns_nonnull(void)
{
	TEST_ASSERT_NOT_NULL(p2malloc(12));
}

void test_initial_allocated_is_zero(void)
{
	TEST_ASSERT_EQUAL_INT(0, p2allocated());
}

void test_initial_totmem_is_zero(void)
{
	TEST_ASSERT_EQUAL_INT(0, p2totmem());
}

void test_malloc_tracks_allocated(void)
{
	p2malloc(12);
	TEST_ASSERT_EQUAL_INT(12, p2allocated());
}

void test_malloc_multiple_accumulates_allocated(void)
{
	p2malloc(12);
	p2malloc(12);
	p2malloc(12);
	TEST_ASSERT_EQUAL_INT(36, p2allocated());
}

/* Each allocation of different sizes independently tracked */
void test_malloc_mixed_sizes_tracks_allocated(void)
{
	p2malloc(10);
	p2malloc(50);
	p2malloc(200);
	TEST_ASSERT_EQUAL_INT(260, p2allocated());
}

/* First allocation from an empty freelist claims exactly one page */
void test_first_alloc_claims_one_page(void)
{
	p2malloc(12);
	TEST_ASSERT_EQUAL_INT(4096, p2totmem());
}

/* Multiple allocs from the same bucket should not claim a second page
 * until the first page is exhausted (32-byte blocks → 128 per page). */
void test_same_bucket_allocs_share_page(void)
{
	for (int i = 0; i < 128; i++)
		TEST_ASSERT_NOT_NULL(p2malloc(12));
	TEST_ASSERT_EQUAL_INT(4096, p2totmem());
}

/* The 129th alloc of 12 bytes overflows the first page */
void test_overflow_claims_second_page(void)
{
	for (int i = 0; i < 128; i++) p2malloc(12);
	TEST_ASSERT_EQUAL_INT(4096, p2totmem());
	p2malloc(12);
	TEST_ASSERT_EQUAL_INT(8192, p2totmem());
}

/* Different bucket sizes get separate pages */
void test_different_buckets_use_separate_pages(void)
{
	p2malloc(28); /* idx 0: 32-byte block */
	p2malloc(29); /* idx 1: 64-byte block (28+4=32 ≤ 32, 29+4=33 > 32) */
	TEST_ASSERT_EQUAL_INT(8192, p2totmem());
}

/* All returned pointers are distinct */
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
/* Free behaviour                                                      */
/* ------------------------------------------------------------------ */

void test_free_decrements_allocated(void)
{
	void *p = p2malloc(12);
	TEST_ASSERT_EQUAL_INT(12, p2allocated());
	p2free(p);
	TEST_ASSERT_EQUAL_INT(0, p2allocated());
}

void test_free_all_returns_allocated_to_zero(void)
{
	void *a = p2malloc(10);
	void *b = p2malloc(20);
	void *c = p2malloc(30);
	p2free(a);
	p2free(b);
	p2free(c);
	TEST_ASSERT_EQUAL_INT(0, p2allocated());
}

/* p2free zeroes the first int of the freed payload */
void test_free_zeroes_first_int_of_payload(void)
{
	int *p = (int *)p2malloc(12);
	*p = 0xDEADBEEF;
	p2free(p);
	TEST_ASSERT_EQUAL_INT(0, *p);
}

/* Freeing does not reclaim totmem (pages are never returned to the OS) */
void test_free_does_not_shrink_totmem(void)
{
	void *p = p2malloc(12);
	int mem = p2totmem();
	p2free(p);
	TEST_ASSERT_EQUAL_INT(mem, p2totmem());
}

/* ------------------------------------------------------------------ */
/* Freelist reuse                                                       */
/* ------------------------------------------------------------------ */

/* Fill an entire page, free everything, then refill — no new page needed */
void test_freelist_reuse_avoids_new_page(void)
{
	void *ptrs[128];
	for (int i = 0; i < 128; i++) ptrs[i] = p2malloc(12);
	int mem = p2totmem();
	for (int i = 0; i < 128; i++) p2free(ptrs[i]);
	for (int i = 0; i < 128; i++) TEST_ASSERT_NOT_NULL(p2malloc(12));
	TEST_ASSERT_EQUAL_INT(mem, p2totmem());
}

/* ------------------------------------------------------------------ */
/* Bucket slot counts                                                   */
/* ------------------------------------------------------------------ */

/* idx 0: 32-byte blocks → 128 per page.  28 is the max payload for idx 0:
 *   28 + 4 (header) = 32 ≤ 32 → idx 0
 *   29 + 4          = 33 > 32 → idx 1                                   */
void test_idx0_holds_128_blocks_per_page(void)
{
	for (int i = 0; i < 128; i++) TEST_ASSERT_NOT_NULL(p2malloc(28));
	TEST_ASSERT_EQUAL_INT(4096, p2totmem());
	p2malloc(28);
	TEST_ASSERT_EQUAL_INT(8192, p2totmem());
}

/* idx 1: 64-byte blocks → 64 per page */
void test_idx1_holds_64_blocks_per_page(void)
{
	for (int i = 0; i < 64; i++) TEST_ASSERT_NOT_NULL(p2malloc(29));
	TEST_ASSERT_EQUAL_INT(4096, p2totmem());
	p2malloc(29);
	TEST_ASSERT_EQUAL_INT(8192, p2totmem());
}

/* idx 7: 4096-byte blocks → 1 per page */
void test_idx7_holds_1_block_per_page(void)
{
	TEST_ASSERT_NOT_NULL(p2malloc(4092));
	TEST_ASSERT_EQUAL_INT(4096, p2totmem());
	p2malloc(4092);
	TEST_ASSERT_EQUAL_INT(8192, p2totmem());
}

/* ------------------------------------------------------------------ */

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_malloc_zero_returns_null);
	RUN_TEST(test_malloc_negative_returns_null);
	RUN_TEST(test_malloc_too_large_returns_null);
	RUN_TEST(test_malloc_max_valid_returns_nonnull);

	RUN_TEST(test_malloc_returns_nonnull);
	RUN_TEST(test_initial_allocated_is_zero);
	RUN_TEST(test_initial_totmem_is_zero);
	RUN_TEST(test_malloc_tracks_allocated);
	RUN_TEST(test_malloc_multiple_accumulates_allocated);
	RUN_TEST(test_malloc_mixed_sizes_tracks_allocated);
	RUN_TEST(test_first_alloc_claims_one_page);
	RUN_TEST(test_same_bucket_allocs_share_page);
	RUN_TEST(test_overflow_claims_second_page);
	RUN_TEST(test_different_buckets_use_separate_pages);
	RUN_TEST(test_allocs_return_distinct_pointers);

	RUN_TEST(test_free_decrements_allocated);
	RUN_TEST(test_free_all_returns_allocated_to_zero);
	RUN_TEST(test_free_zeroes_first_int_of_payload);
	RUN_TEST(test_free_does_not_shrink_totmem);

	RUN_TEST(test_freelist_reuse_avoids_new_page);

	RUN_TEST(test_idx0_holds_128_blocks_per_page);
	RUN_TEST(test_idx1_holds_64_blocks_per_page);
	RUN_TEST(test_idx7_holds_1_block_per_page);

	return UNITY_END();
}
