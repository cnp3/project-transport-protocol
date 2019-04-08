#include <stdlib.h>
#include <stdint.h>

#include "../src/common/macros.h"
#include "../src/receiver/receive.h"
#include "test_oob_receive.h"

/* Forward declaration of the private members of receiver.c that we want to
 * test */
extern uint32_t oos_mask;
extern uint8_t expected_seq;
unsigned int window_size();


int test_oob_init()
{
	return 0;
}

int test_oob_cleanup()
{
	return 0;
}

static void test_window_size()
{
	oos_mask = 2546; /* i.e there are no packets in slot 1 */
	CU_ASSERT(window_size() == max_window);
	oos_mask = 0b1011111;
	CU_ASSERT(window_size() == max_window - 5);
}


CU_TestInfo test_oob[] = {
	{"test_window_size", test_window_size},
	CU_TEST_INFO_NULL,
};
CU_pTestInfo test_oob_list() { return test_oob; }
