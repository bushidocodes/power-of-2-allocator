#include <stdio.h>
#include <string.h>
#include "p2malloc.h"

/* Each level was a separate program in xv6; p2reset() restores that isolation. */

/* ------------------------------------------------------------------ */
/* Level 0: smoke test — one alloc, one free                          */
/* ------------------------------------------------------------------ */
static int test_level0(void)
{
	printf("Level 0: basic alloc/free\n");
	void *addr = p2malloc(100);
	if (!addr) { printf("  FAIL: p2malloc returned NULL\n"); return 0; }
	p2free(addr);
	printf("  Allocated : %d\n", p2allocated());
	printf("  Total Mem : %d\n", p2totmem());
	printf("  PASS\n\n");
	return 1;
}

/* ------------------------------------------------------------------ */
/* Level 1: 1 M tight alloc/free cycles — no accumulation            */
/* ------------------------------------------------------------------ */
static int test_level1(void)
{
	printf("Level 1: 1 000 000 alloc/free cycles of 12 bytes\n");
	for (int i = 0; i < 1000000; i++) {
		void *thing = p2malloc(12);
		if (!thing) { printf("  FAIL: p2malloc returned NULL at i=%d\n", i); return 0; }
		memset(thing, 0, 12);
		p2free(thing);
	}
	printf("  PASS\n\n");
	return 1;
}

/* ------------------------------------------------------------------ */
/* Level 2: 8 live allocs, out-of-order free, zeroing check           */
/* ------------------------------------------------------------------ */
static int test_level2(void)
{
	printf("Level 2: 8 live 12-byte allocs, out-of-order free, zeroing check\n");
	const int size = 12;
	void *ptrs[8];

	for (int i = 0; i < 8; i++) {
		ptrs[i] = p2malloc(size);
		if (!ptrs[i]) { printf("  FAIL: p2malloc returned NULL at i=%d\n", i); return 0; }

		if (p2totmem() != 4096) {
			printf("  FAIL: totmem should be 4096 after alloc %d, got %d\n",
			       i + 1, p2totmem());
			return 0;
		}
		int got = p2allocated();
		int want = size * (i + 1);
		if (got != want) {
			printf("  FAIL: allocated should be %d, got %d\n", want, got);
			return 0;
		}
	}

	for (int i = 0; i < 8; i++) {
		*(int *)ptrs[i] = 42;
		printf("  slot %d at %p set to 42\n", i, ptrs[i]);
	}

	/* Free in an order different from allocation to exercise the ordered insert */
	p2free(ptrs[2]);
	p2free(ptrs[4]);
	p2free(ptrs[5]);
	p2free(ptrs[3]);
	p2free(ptrs[6]);
	p2free(ptrs[0]);
	p2free(ptrs[7]);
	p2free(ptrs[1]);

	for (int i = 0; i < 8; i++) {
		if (*(int *)ptrs[i] == 42) {
			printf("  FAIL: slot %d was not zeroed after free\n", i);
			return 0;
		}
	}
	printf("  PASS\n\n");
	return 1;
}

/* ------------------------------------------------------------------ */
/* Level 3: sweep all sizes 12..4091 step 7                           */
/* ------------------------------------------------------------------ */
static int test_level3(void)
{
	printf("Level 3: sweep sizes 12..4091 step 7 (no frees)\n");
	for (int size = 12; size < 4092; size += 7) {
		void *p = p2malloc(size);
		if (!p) { printf("  FAIL: p2malloc(%d) returned NULL\n", size); return 0; }
	}
	printf("  Allocated : %d\n", p2allocated());
	printf("  Total Mem : %d\n", p2totmem());
	printf("  PASS\n\n");
	return 1;
}

/* ------------------------------------------------------------------ */
/* Level 4: stress — 1 M allocs each of 12 and 300 bytes             */
/*          First 1000 of each size accumulate; the rest reuse freed  */
/* ------------------------------------------------------------------ */
static int test_level4(void)
{
	printf("Level 4: stress test 1 000 000 allocs x {12, 300} bytes\n");
	const int gate = 1000;

	for (int i = 0; i < 1000000; i++) {
		void *thing = p2malloc(12);
		if (!thing) { printf("  FAIL: p2malloc(12) NULL at i=%d\n", i); return 0; }
		memset(thing, 0, 12);
		if (i >= gate) p2free(thing);
	}
	printf("  After 12-byte phase  — allocated: %d  totmem: %d\n",
	       p2allocated(), p2totmem());

	for (int i = 0; i < 1000000; i++) {
		void *thing = p2malloc(300);
		if (!thing) { printf("  FAIL: p2malloc(300) NULL at i=%d\n", i); return 0; }
		memset(thing, 0, 300);
		if (i >= gate) p2free(thing);
	}
	printf("  After 300-byte phase — allocated: %d  totmem: %d\n",
	       p2allocated(), p2totmem());
	printf("  PASS\n\n");
	return 1;
}

/* ------------------------------------------------------------------ */

int main(void)
{
	printf("=== Power-of-2 Allocator Tests ===\n\n");

	int pass = 1;

	p2reset(); pass &= test_level0();
	p2reset(); pass &= test_level1();
	p2reset(); pass &= test_level2();
	p2reset(); pass &= test_level3();
	p2reset(); pass &= test_level4();

	if (pass)
		printf("All tests passed.\n");
	else
		printf("One or more tests FAILED.\n");

	return pass ? 0 : 1;
}
