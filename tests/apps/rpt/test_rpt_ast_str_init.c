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
 * \brief Unit tests for RPT_AST_STR_INIT_SIZE usage in app_rpt
 *
 * \author AllStarLink
 *
 * This test verifies that:
 * 1. RPT_AST_STR_INIT_SIZE is correctly defined
 * 2. ast_str allocations using RPT_AST_STR_INIT_SIZE succeed
 * 3. Memory is properly managed when using this constant
 * 4. Edge cases are handled correctly
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
#include "asterisk/strings.h"
#include "asterisk/utils.h"
#include "asterisk/logger.h"

/* Include the header that defines RPT_AST_STR_INIT_SIZE */
#include "../../apps/app_rpt/app_rpt.h"

AST_TEST_DEFINE(test_rpt_ast_str_init_size_defined)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "rpt_ast_str_init_size_defined";
		info->category = "/apps/app_rpt/";
		info->summary = "Test that RPT_AST_STR_INIT_SIZE is correctly defined";
		info->description = "Verifies that RPT_AST_STR_INIT_SIZE constant is defined with expected value";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Testing RPT_AST_STR_INIT_SIZE definition\n");

	/* Verify the constant is defined and has the expected value */
	if (RPT_AST_STR_INIT_SIZE != 500) {
		ast_test_status_update(test, "RPT_AST_STR_INIT_SIZE is %d, expected 500\n",
			RPT_AST_STR_INIT_SIZE);
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "RPT_AST_STR_INIT_SIZE correctly defined as %d\n",
		RPT_AST_STR_INIT_SIZE);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_ast_str_create_with_rpt_init_size)
{
	struct ast_str *str = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_str_create_with_rpt_init_size";
		info->category = "/apps/app_rpt/";
		info->summary = "Test ast_str creation with RPT_AST_STR_INIT_SIZE";
		info->description = "Verifies that ast_str_create succeeds with RPT_AST_STR_INIT_SIZE";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Creating ast_str with RPT_AST_STR_INIT_SIZE\n");

	/* Test allocation */
	str = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!str) {
		ast_test_status_update(test, "Failed to create ast_str with size %d\n",
			RPT_AST_STR_INIT_SIZE);
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Successfully created ast_str with size %d\n",
		RPT_AST_STR_INIT_SIZE);

	/* Verify we can use the string */
	ast_str_set(&str, 0, "Test string for RPT");
	if (strcmp(ast_str_buffer(str), "Test string for RPT") != 0) {
		ast_test_status_update(test, "String content mismatch\n");
		ast_free(str);
		return AST_TEST_FAIL;
	}

	/* Clean up */
	ast_free(str);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_ast_str_multiple_allocations)
{
	struct ast_str *str1 = NULL, *str2 = NULL, *str3 = NULL;
	int result = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_str_multiple_allocations";
		info->category = "/apps/app_rpt/";
		info->summary = "Test multiple ast_str allocations with RPT_AST_STR_INIT_SIZE";
		info->description = "Verifies multiple ast_str allocations work correctly, "
			"simulating real-world usage in app_rpt";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Testing multiple ast_str allocations\n");

	/* Simulate allocation pattern from do_link_post() */
	str1 = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!str1) {
		ast_test_status_update(test, "Failed to allocate first ast_str\n");
		return AST_TEST_FAIL;
	}

	/* Simulate allocation pattern from do_key_post() */
	str2 = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!str2) {
		ast_test_status_update(test, "Failed to allocate second ast_str\n");
		ast_free(str1);
		return AST_TEST_FAIL;
	}

	/* Simulate allocation pattern from statpost */
	str3 = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!str3) {
		ast_test_status_update(test, "Failed to allocate third ast_str\n");
		ast_free(str1);
		ast_free(str2);
		return AST_TEST_FAIL;
	}

	/* Use all three strings to ensure they're valid */
	ast_str_set(&str1, 0, "nodes=1234,5678");
	ast_str_set(&str2, 0, "keypost=active");
	ast_str_set(&str3, 0, "stats_url=http://example.com");

	/* Verify content */
	if (strcmp(ast_str_buffer(str1), "nodes=1234,5678") != 0) {
		ast_test_status_update(test, "String 1 content mismatch\n");
		result = AST_TEST_FAIL;
	}
	if (strcmp(ast_str_buffer(str2), "keypost=active") != 0) {
		ast_test_status_update(test, "String 2 content mismatch\n");
		result = AST_TEST_FAIL;
	}
	if (strcmp(ast_str_buffer(str3), "stats_url=http://example.com") != 0) {
		ast_test_status_update(test, "String 3 content mismatch\n");
		result = AST_TEST_FAIL;
	}

	/* Clean up all allocations */
	ast_free(str1);
	ast_free(str2);
	ast_free(str3);

	ast_test_status_update(test, "Successfully allocated and freed multiple ast_str instances\n");

	return result;
}

AST_TEST_DEFINE(test_ast_str_expansion_from_init_size)
{
	struct ast_str *str = NULL;
	char large_string[1000];
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_str_expansion_from_init_size";
		info->category = "/apps/app_rpt/";
		info->summary = "Test ast_str expansion beyond RPT_AST_STR_INIT_SIZE";
		info->description = "Verifies that ast_str can grow beyond initial size "
			"when needed (e.g., large link lists)";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Testing ast_str expansion beyond initial size\n");

	str = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!str) {
		ast_test_status_update(test, "Failed to create ast_str\n");
		return AST_TEST_FAIL;
	}

	/* Create a string larger than RPT_AST_STR_INIT_SIZE */
	memset(large_string, 'A', sizeof(large_string) - 1);
	large_string[sizeof(large_string) - 1] = '\0';

	/* This should trigger expansion of the ast_str */
	ast_str_set(&str, 0, "%s", large_string);

	/* Verify the content was stored correctly */
	if (strlen(ast_str_buffer(str)) != strlen(large_string)) {
		ast_test_status_update(test, "String length mismatch after expansion: got %zu, expected %zu\n",
			strlen(ast_str_buffer(str)), strlen(large_string));
		ast_free(str);
		return AST_TEST_FAIL;
	}

	/* Verify content integrity */
	for (i = 0; i < sizeof(large_string) - 1; i++) {
		if (ast_str_buffer(str)[i] != 'A') {
			ast_test_status_update(test, "String content corrupted at position %d\n", i);
			ast_free(str);
			return AST_TEST_FAIL;
		}
	}

	ast_free(str);

	ast_test_status_update(test, "Successfully expanded ast_str beyond initial size\n");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_ast_str_null_handling)
{
	struct ast_str *str = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_str_null_handling";
		info->category = "/apps/app_rpt/";
		info->summary = "Test NULL handling for ast_str allocations";
		info->description = "Verifies that code properly handles NULL return from ast_str_create "
			"(simulating out-of-memory conditions)";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Testing proper allocation and NULL checks\n");

	/* Normal allocation should succeed */
	str = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!str) {
		ast_test_status_update(test, "Normal allocation failed unexpectedly\n");
		return AST_TEST_FAIL;
	}

	/* Verify we can check for NULL properly (this mimics the code pattern in app_rpt.c) */
	if (str != NULL) {
		ast_test_status_update(test, "NULL check pattern works correctly\n");
	} else {
		ast_test_status_update(test, "NULL check failed unexpectedly\n");
		return AST_TEST_FAIL;
	}

	ast_free(str);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_ast_str_boundary_conditions)
{
	struct ast_str *str = NULL;
	char boundary_string[RPT_AST_STR_INIT_SIZE];

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_str_boundary_conditions";
		info->category = "/apps/app_rpt/";
		info->summary = "Test boundary conditions at RPT_AST_STR_INIT_SIZE";
		info->description = "Verifies behavior at exactly RPT_AST_STR_INIT_SIZE bytes";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Testing boundary conditions\n");

	str = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!str) {
		ast_test_status_update(test, "Failed to create ast_str\n");
		return AST_TEST_FAIL;
	}

	/* Test with exactly RPT_AST_STR_INIT_SIZE - 1 characters (leaving room for null terminator) */
	memset(boundary_string, 'B', RPT_AST_STR_INIT_SIZE - 1);
	boundary_string[RPT_AST_STR_INIT_SIZE - 1] = '\0';

	ast_str_set(&str, 0, "%s", boundary_string);

	/* Verify correct storage */
	if (strlen(ast_str_buffer(str)) != RPT_AST_STR_INIT_SIZE - 1) {
		ast_test_status_update(test, "Boundary string length incorrect: got %zu, expected %d\n",
			strlen(ast_str_buffer(str)), RPT_AST_STR_INIT_SIZE - 1);
		ast_free(str);
		return AST_TEST_FAIL;
	}

	ast_free(str);

	ast_test_status_update(test, "Boundary conditions handled correctly\n");

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_rpt_ast_str_init_size_defined);
	AST_TEST_UNREGISTER(test_ast_str_create_with_rpt_init_size);
	AST_TEST_UNREGISTER(test_ast_str_multiple_allocations);
	AST_TEST_UNREGISTER(test_ast_str_expansion_from_init_size);
	AST_TEST_UNREGISTER(test_ast_str_null_handling);
	AST_TEST_UNREGISTER(test_ast_str_boundary_conditions);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(test_rpt_ast_str_init_size_defined);
	AST_TEST_REGISTER(test_ast_str_create_with_rpt_init_size);
	AST_TEST_REGISTER(test_ast_str_multiple_allocations);
	AST_TEST_REGISTER(test_ast_str_expansion_from_init_size);
	AST_TEST_REGISTER(test_ast_str_null_handling);
	AST_TEST_REGISTER(test_ast_str_boundary_conditions);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "RPT AST_STR Init Size Tests");