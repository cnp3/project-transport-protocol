#include <stdlib.h>
#include <stdio.h>
#include <CUnit/CUnit.h>
#include <CUnit/Automated.h>


#include "../src/common/macros.h"
#include "test_pktbuf.h"
#include "test_oob_receive.h"

static void noop() {  }

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	if (CU_initialize_registry() != CUE_SUCCESS)
		goto_trace(err, "Could not initialize the test registry");

	CU_SuiteInfo suites[] = {
		/* no per-test tear up/tear down func */
	  { "test_pktbuf", test_pktbuf_init, test_pktbuf_cleanup,
		  noop, noop, test_pktbuf_list() },
	  { "test_oob_handling", test_oob_init, test_oob_cleanup,
		  noop, noop, test_oob_list() },
	  CU_SUITE_INFO_NULL,
	};
	if (CU_register_suites(suites))
		goto_trace(err, "Could not register test suites!");

	CU_automated_run_tests();
	CU_cleanup_registry();

	return EXIT_SUCCESS;

err:
	return EXIT_FAILURE;
}
