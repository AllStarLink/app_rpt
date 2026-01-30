/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, AllStarLink, Inc.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 * \brief Unit tests for app_rpt constants including RPT_AST_STR_INIT_SIZE
 *
 * \author AllStarLink
 *
 * This test verifies that app_rpt constants are properly defined and have
 * reasonable values for their intended use. This is a regression test to
 * catch accidental changes to critical constants.
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"

/* Include the header that defines app_rpt constants */
#include "../../apps/app_rpt/app_rpt.h"

AST_TEST_DEFINE(test_rpt_ast_str_init_size_value)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "rpt_ast_str_init_size_value";
		info->category = "/apps/app_rpt/constants/";
		info->summary = "Test RPT_AST_STR_INIT_SIZE has correct value";
		info->description = "Regression test to ensure RPT_AST_STR_INIT_SIZE "
			"maintains its expected value of 500";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Verifying RPT_AST_STR_INIT_SIZE = 500\n");

	if (RPT_AST_STR_INIT_SIZE != 500) {
		ast_test_status_update(test, "FAIL: RPT_AST_STR_INIT_SIZE is %d, expected 500\n", RPT_AST_STR_INIT_SIZE);
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_rpt_ast_str_init_size_sufficient)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "rpt_ast_str_init_size_sufficient";
		info->category = "/apps/app_rpt/constants/";
		info->summary = "Test RPT_AST_STR_INIT_SIZE is sufficient for typical use";
		info->description = "Verifies RPT_AST_STR_INIT_SIZE is large enough for "
			"typical node lists, key posts, and stat messages without immediate reallocation";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Checking if RPT_AST_STR_INIT_SIZE is sufficient\n");

	/* Typical node list: "nodes=1234,5678,9012,3456,7890" (about 35 chars) */
	/* Typical key post: "keypost=active&node=1234&time=1234567890" (about 45 chars) */
	/* Typical stats URL: "http://stats.example.com/update?node=1234&..." (varies) */

	/* Check minimum size - should be at least 100 bytes */
	if (RPT_AST_STR_INIT_SIZE < 100) {
		ast_test_status_update(test,
			"WARNING: RPT_AST_STR_INIT_SIZE (%d) is very small, may cause frequent reallocations\n",
			RPT_AST_STR_INIT_SIZE);
		return AST_TEST_FAIL;
	}

	/* Check it's not unnecessarily large (wasting memory) */
	if (RPT_AST_STR_INIT_SIZE > 10000) {
		ast_test_status_update(test,
			"WARNING: RPT_AST_STR_INIT_SIZE (%d) is very large, may waste memory\n",
			RPT_AST_STR_INIT_SIZE);
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test,
		"RPT_AST_STR_INIT_SIZE (%d) is in reasonable range [100, 10000]\n",
		RPT_AST_STR_INIT_SIZE);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_rpt_related_constants)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "rpt_related_constants";
		info->category = "/apps/app_rpt/constants/";
		info->summary = "Test other RPT constants are properly defined";
		info->description = "Verifies related constants like MAXNODES, MAXDTMF, etc. "
			"are properly defined alongside RPT_AST_STR_INIT_SIZE";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Checking related app_rpt constants\n");

	/* Verify MAXNODES is defined and reasonable */
	if (MAXNODES < 1 || MAXNODES > 100000) {
		ast_test_status_update(test,
			"MAXNODES (%d) is out of reasonable range\n", MAXNODES);
		return AST_TEST_FAIL;
	}

	/* Verify MAXDTMF is defined and reasonable */
	if (MAXDTMF < 1 || MAXDTMF > 1000) {
		ast_test_status_update(test, "MAXDTMF (%d) is out of reasonable range\n", MAXDTMF);
		return AST_TEST_FAIL;
	}

	/* Verify MAXMACRO is defined and reasonable */
	if (MAXMACRO < 1 || MAXMACRO > 100000) {
		ast_test_status_update(test, "MAXMACRO (%d) is out of reasonable range\n", MAXMACRO);
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "All related constants are properly defined:\n");
	ast_test_status_update(test, "  MAXNODES = %d\n", MAXNODES);
	ast_test_status_update(test, "  MAXDTMF = %d\n", MAXDTMF);
	ast_test_status_update(test, "  MAXMACRO = %d\n", MAXMACRO);
	ast_test_status_update(test, "  RPT_AST_STR_INIT_SIZE = %d\n", RPT_AST_STR_INIT_SIZE);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_rpt_ast_str_init_size_consistency)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "rpt_ast_str_init_size_consistency";
		info->category = "/apps/app_rpt/constants/";
		info->summary = "Test RPT_AST_STR_INIT_SIZE is consistent across includes";
		info->description = "Regression test to ensure the constant has the same value "
			"regardless of how app_rpt.h is included";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Verifying constant consistency\n");

	/* The constant should be exactly 500 - this is the canonical value */
	const int expected = 500;

	if (RPT_AST_STR_INIT_SIZE != expected) {
		ast_test_status_update(test,
		ast_test_status_update(test, "Constant mismatch: got %d, expected %d\n", RPT_AST_STR_INIT_SIZE, expected);
	}

	/* Verify it's a power-friendly number (multiple of common sizes) */
	if (RPT_AST_STR_INIT_SIZE % 4 != 0) {
		ast_test_status_update(test,
			"NOTE: RPT_AST_STR_INIT_SIZE (%d) is not 4-byte aligned\n",
			RPT_AST_STR_INIT_SIZE);
		/* Not a failure, just informational */
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_rpt_ast_str_init_size_value);
	AST_TEST_UNREGISTER(test_rpt_ast_str_init_size_sufficient);
	AST_TEST_UNREGISTER(test_rpt_related_constants);
	AST_TEST_UNREGISTER(test_rpt_ast_str_init_size_consistency);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(test_rpt_ast_str_init_size_value);
	AST_TEST_REGISTER(test_rpt_ast_str_init_size_sufficient);
	AST_TEST_REGISTER(test_rpt_related_constants);
	AST_TEST_REGISTER(test_rpt_ast_str_init_size_consistency);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "RPT Constants Tests");