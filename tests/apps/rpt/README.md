# App RPT Tests

This directory contains tests for the app_rpt module.

## Test Overview

### Integration Tests (YAML-based)

These tests use the Asterisk Test Suite framework and are defined in YAML format.

#### 1. fast_connect_disconnect
Tests rapid connection and disconnection of nodes to ensure stability.
- Location: `fast_connect_disconnect/`
- Purpose: Verify that app_rpt handles rapid link establishment and teardown without crashes
- Related Issue: app_rpt issue #459

#### 2. ast_str_allocation
Tests the ast_str allocation functionality using RPT_AST_STR_INIT_SIZE constant.
- Location: `ast_str_allocation/`
- Purpose: Verify proper memory allocation and management after refactoring from magic numbers to RPT_AST_STR_INIT_SIZE
- Tests the following code paths:
  - Link list management (ast_str allocation for link lists)
  - Statistics posting (ast_str allocation for stats URLs and responses)
  - Key posting (ast_str allocation for key post messages)

### Unit Tests (C-based)

#### test_rpt_ast_str_init.c
Comprehensive unit tests for RPT_AST_STR_INIT_SIZE usage in ast_str allocations.

#### test_rpt_constants.c
Regression and validation tests for app_rpt constants including RPT_AST_STR_INIT_SIZE.

**Test Cases:**

1. **test_rpt_ast_str_init_size_defined**
   - Verifies RPT_AST_STR_INIT_SIZE is defined with expected value (500)
   - Ensures the constant is available throughout the codebase

2. **test_ast_str_create_with_rpt_init_size**
   - Tests basic ast_str creation with RPT_AST_STR_INIT_SIZE
   - Verifies allocation succeeds and string operations work correctly

3. **test_ast_str_multiple_allocations**
   - Tests multiple simultaneous ast_str allocations
   - Simulates real-world usage patterns from app_rpt:
     - do_link_post() pattern
     - do_key_post() pattern
     - statpost pattern
   - Ensures no interference between multiple allocations

4. **test_ast_str_expansion_from_init_size**
   - Tests ast_str expansion beyond RPT_AST_STR_INIT_SIZE
   - Verifies dynamic growth works correctly (e.g., for large link lists)
   - Tests content integrity after expansion

5. **test_ast_str_null_handling**
   - Tests proper NULL checking after ast_str_create()
   - Verifies code follows proper allocation failure handling patterns

6. **test_ast_str_boundary_conditions**
   - Tests behavior at exactly RPT_AST_STR_INIT_SIZE bytes
   - Verifies correct handling of boundary conditions

**Test Cases (test_rpt_constants.c):**

1. **test_rpt_ast_str_init_size_value**
   - Regression test to ensure RPT_AST_STR_INIT_SIZE maintains expected value (500)
   - Prevents accidental changes to the constant

2. **test_rpt_ast_str_init_size_sufficient**
   - Validates RPT_AST_STR_INIT_SIZE is large enough for typical use
   - Ensures it's not too small (causing frequent reallocations)
   - Ensures it's not too large (wasting memory)

3. **test_rpt_related_constants**
   - Verifies related constants (MAXNODES, MAXDTMF, MAXMACRO) are properly defined
   - Ensures consistency across the header file

4. **test_rpt_ast_str_init_size_consistency**
   - Tests that the constant value is consistent across different include paths
   - Verifies alignment properties

## Running the Tests

### Integration Tests

Integration tests are run using the Asterisk Test Suite:

```bash
# From the Asterisk Test Suite directory
./runtests.py --test=apps/rpt/fast_connect_disconnect
./runtests.py --test=apps/rpt/ast_str_allocation
```

### Unit Tests

Unit tests are built as Asterisk test modules and run using the Asterisk CLI:

```bash
# In Asterisk CLI
test show registered
test execute category /apps/app_rpt/
```

## Test Coverage

The tests cover the following changes from commit "use RPT_AST_STR_INIT_SIZE and eliminate magic numbers":

1. **apps/app_rpt/app_rpt.h:74**
   - Definition: `#define RPT_AST_STR_INIT_SIZE 500`
   - Tested by: test_rpt_ast_str_init_size_defined

2. **apps/app_rpt.c:1070**
   - Usage: `response_msg = ast_str_create(RPT_AST_STR_INIT_SIZE);`
   - Context: statpost_thread() - statistics posting
   - Tested by: test_ast_str_create_with_rpt_init_size, test_ast_str_multiple_allocations

3. **apps/app_rpt.c:1133**
   - Usage: `sp->stats_url = ast_str_create(RPT_AST_STR_INIT_SIZE);`
   - Context: statpost_send() - statistics URL allocation
   - Tested by: test_ast_str_multiple_allocations

4. **apps/app_rpt.c:3267**
   - Usage: `struct ast_str *lstr = ast_str_create(RPT_AST_STR_INIT_SIZE);`
   - Context: rpt_master() - link list management
   - Tested by: ast_str_allocation integration test, test_ast_str_expansion_from_init_size

5. **apps/app_rpt.c:3416**
   - Usage: `struct ast_str *str = ast_str_create(RPT_AST_STR_INIT_SIZE);`
   - Context: do_key_post() - key posting messages
   - Tested by: ast_str_allocation integration test, test_ast_str_multiple_allocations

6. **apps/app_rpt.c:3446**
   - Usage: `str = ast_str_create(RPT_AST_STR_INIT_SIZE);`
   - Context: do_link_post() - link posting messages
   - Tested by: ast_str_allocation integration test, test_ast_str_multiple_allocations

7. **apps/app_rpt.c:6825**
   - Usage: `l->linklist = ast_str_create(RPT_AST_STR_INIT_SIZE);`
   - Context: connect_link_remote() - link list allocation
   - Tested by: ast_str_allocation integration test

## Expected Results

All tests should pass, demonstrating that:
- The refactoring from magic numbers to RPT_AST_STR_INIT_SIZE is correct
- Memory allocation and deallocation work properly
- The constant value (500) is appropriate for typical use cases
- The code properly handles edge cases (NULL returns, boundary conditions, expansion)

## Future Enhancements

Potential additional tests:
1. Stress test with many simultaneous allocations
2. Performance comparison test (old vs new approach)
3. Memory leak detection test using valgrind
4. Thread safety test for concurrent allocations