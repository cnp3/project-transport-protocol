#include <stdlib.h>

#include "../src/common/macros.h"
#include "../src/common/pktbuf.h"
#include <CUnit/CUnit.h>


static pktbuf_t *buf;

int test_pktbuf_init()
{
	if (!(buf = pktbuf_new(32)))
		return -1;
	return 0;
}

int test_pktbuf_cleanup()
{
	pktbuf_free(buf);
	return 0;
}

static void test_size_tracking()
{
	CU_ASSERT(pktbuf_empty(buf));

	pktbuf_enqueue(buf)->seq = 1;
	CU_ASSERT(pktbuf_first(buf)->seq == 1);
	CU_ASSERT(pktbuf_last(buf)->seq == 1);
	pktbuf_enqueue(buf)->seq = 2;
	CU_ASSERT(pktbuf_first(buf)->seq == 1);
	CU_ASSERT(pktbuf_last(buf)->seq == 2);
	pktbuf_enqueue(buf)->seq = 3;
	CU_ASSERT(pktbuf_first(buf)->seq == 1);
	CU_ASSERT(pktbuf_last(buf)->seq == 3);

	CU_ASSERT(pktbuf_used(buf) == 3);
	CU_ASSERT(pktbuf_freeslots(buf) == 32 - 3);
	CU_ASSERT(!pktbuf_empty(buf));
	CU_ASSERT(!pktbuf_full(buf));

	pktbuf_dequeue(buf);
	CU_ASSERT(pktbuf_used(buf) == 2);
	pktbuf_dequeue(buf);
	CU_ASSERT(pktbuf_used(buf) == 1);
	pktbuf_dequeue(buf);
	CU_ASSERT(pktbuf_used(buf) == 0);

	CU_ASSERT(pktbuf_empty(buf));
}

static void test_slotforseq()
{
	CU_ASSERT(pktbuf_slotfor_seq(buf, 2)->seq == 2);
	CU_ASSERT(pktbuf_used(buf) == 1);
	CU_ASSERT(pktbuf_first(buf)->seq == 2);
	CU_ASSERT(pktbuf_last(buf)->seq == 2);

	CU_ASSERT(pktbuf_slotfor_seq(buf, 5)->seq == 5);
	CU_ASSERT(pktbuf_first(buf)->seq == 2);
	CU_ASSERT(pktbuf_last(buf)->seq == 5);

	CU_ASSERT(pktbuf_slotfor_seq(buf, 2)->seq == 2);
	CU_ASSERT(pktbuf_used(buf) == 4);

	CU_ASSERT(pktbuf_dequeue(buf)->seq == 5);
	CU_ASSERT(pktbuf_dequeue(buf)->seq == 4);
	CU_ASSERT(pktbuf_dequeue(buf)->seq == 3);
	CU_ASSERT(pktbuf_dequeue(buf)->seq == 2);

	CU_ASSERT(pktbuf_empty(buf));
}

CU_TestInfo test_pktbuf[] = {
	{"test_size_tracking", test_size_tracking},
	{"test_slotforseq", test_slotforseq},
	CU_TEST_INFO_NULL,
};
CU_pTestInfo test_pktbuf_list() { return test_pktbuf; }
