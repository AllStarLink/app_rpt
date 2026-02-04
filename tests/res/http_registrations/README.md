# HTTP Registrations Test Suite

This directory contains comprehensive tests for the `res_rpt_http_registrations` module.

## Overview

The `res_rpt_http_registrations` module provides DDNS-like functionality for AllStarLink/app_rpt nodes, allowing them to register periodically with a central server via HTTP(S). This test suite verifies:

- Registration request parsing and formatting
- HTTP communication with registration servers
- Registration state tracking and management
- Configuration file parsing and reload functionality
- Error handling and graceful degradation
- Memory management (including the fix for the DNS manager memory leak)

## Test Structure

### 1. Integration Tests (Asterisk Test Suite)

Located in the parent directory structure:
- `tests/res/http_registrations/` - Main test suite
- `tests/res/http_registrations_failure/` - Negative test cases

#### Main Integration Test (`test-config.yaml`)

Tests the complete registration workflow:

1. **Module Loading** - Verifies module loads successfully
2. **Initial Registration** - Tests first registration attempt
3. **Status Verification** - Validates `rpt show registrations` CLI command
4. **Configuration Reload** - Tests runtime config reload
5. **Module Unload** - Verifies clean shutdown and memory cleanup

**Test Components:**
- Mock HTTPS server simulating registration endpoint
- AMI event monitoring for state changes
- CLI command execution and validation
- Multiple registration scenarios

#### Failure Handling Test (`http_registrations_failure/`)

Tests error conditions and edge cases:

1. **Invalid Registration Formats**
   - Missing hostname separator
   - Empty username/hostname
   - Invalid port numbers
   - Malformed registration strings

2. **DNS Failures**
   - Non-existent hostnames
   - DNS lookup timeouts

3. **Graceful Degradation**
   - Module continues operating despite failures
   - Proper error logging
   - No crashes or memory leaks

### 2. Unit Tests (Standalone C)

Located in: `test_http_registrations_unit.c`

Provides low-level function testing without requiring Asterisk:

**Test Suites:**

1. **Registration String Parsing** (`test_parse_register_format`)
   - Valid format: `username:password@hostname:port`
   - Format without password: `username@hostname:port`
   - Format without port: `username:password@hostname`
   - Invalid formats detection

2. **JSON Request Data Format** (`test_build_request_data_format`)
   - Verifies proper JSON structure
   - Tests field presence and types
   - Validates nested object creation

3. **HTTP Response Parsing** (`test_http_response_parsing`)
   - Success response handling
   - Field extraction (ipaddr, port, refresh, data)
   - Registration success detection

4. **Registration State Management** (`test_registration_state_management`)
   - Initial state verification
   - State transitions (unregistered → registered)
   - Failed registration handling

5. **URL Construction** (`test_url_construction`)
   - HTTPS URL building with port
   - Default port handling
   - URL format validation

6. **Configuration Parsing** (`test_config_interval_parsing`)
   - Valid interval values
   - Invalid input handling
   - Default value application

7. **Memory Management** (`test_memory_management`)
   - Structure allocation with flexible array
   - String bounds checking
   - Buffer overflow prevention

8. **DNS Manager Integration** (`test_dns_manager_integration`)
   - Hostname validation
   - Port configuration
   - Address family handling

## Running the Tests

### Prerequisites

**For Integration Tests:**
- Asterisk Test Suite installed at `/usr/src/testsuite`
- Python 3 with twisted and starpy packages
- Asterisk with res_curl, chan_iax2, and res_dnsmgr modules
- OpenSSL for HTTPS support

**For Unit Tests:**
- GCC compiler
- GNU Make

### Running Integration Tests

#### Using PhreakNet Helper (Recommended):

```bash
cd /usr/src/testsuite
phreaknet runtest res/http_registrations
phreaknet runtest res/http_registrations_failure
```

#### Using Test Suite Directly:

```bash
cd /usr/src/testsuite
python3 runtests.py --test=tests/res/http_registrations
python3 runtests.py --test=tests/res/http_registrations_failure
```

### Running Unit Tests

```bash
cd tests/res/http_registrations
make test
```

Or compile and run manually:

```bash
gcc -Wall -Wextra -std=c99 -o test_http_registrations_unit test_http_registrations_unit.c
./test_http_registrations_unit
```

## Test Configuration Files

### Configuration for Main Test

Located in `configs/ast1/`:

- **rpt_http_registrations.conf** - Registration configuration with 10-second interval
- **iax.conf** - IAX2 bindport configuration
- **modules.conf** - Module loading configuration
- **logger.conf** - Debug logging setup
- **manager.conf** - AMI access for test control

### Configuration for Failure Test

Located in `http_registrations_failure/configs/ast1/`:

Contains intentionally invalid registration configurations to test error handling.

## Mock HTTP Server

The `mock_registration_server.py` script provides a test registration endpoint:

**Features:**
- Accepts HTTP POST requests with registration data
- Returns JSON responses matching expected format
- Tracks registration state
- Provides debug endpoint (GET /) showing current registrations
- Supports HTTPS with self-signed certificates
- Simulates various failure scenarios on different endpoints

**Endpoints:**
- `/` - Main registration endpoint (returns success)
- `/fail` - Returns HTTP 500 error
- `/unauthorized` - Returns HTTP 401 error
- `/notfound` - Returns HTTP 404 error

**Running the Mock Server:**

```bash
python3 mock_registration_server.py [port]
```

Default port: 8443

## Expected Test Results

### Integration Test Success Criteria

1. Module loads without errors
2. Registration requests sent to mock server
3. `rpt show registrations` displays correct status
4. Module reload works without crashes
5. Module unload completes cleanly
6. No memory leaks detected

### Unit Test Success Criteria

All test assertions pass:
- Registration string parsing (6 tests)
- JSON format validation (1 test)
- Response parsing (2 tests)
- State management (3 tests)
- URL construction (2 tests)
- Config parsing (3 tests)
- Memory management (2 tests)
- DNS integration (2 tests)

**Expected Output:**
```
========================================
  Test Results Summary
========================================
  Passed: 21+
  Failed: 0
  Total:  21+
========================================

RESULT: SUCCESS
```

## Test Coverage

### Functions Tested

**Directly:**
- `parse_register()` - Registration string parsing
- `append_register()` - Registry entry creation
- `build_request_data()` - JSON request formatting
- `register_to_json()` - Node data JSON conversion
- `http_register()` - HTTP registration execution
- `curl_post()` - CURL wrapper function
- `load_config()` - Configuration parsing
- `cleanup_registrations()` - Memory cleanup
- `handle_show_registrations()` - CLI command handler

**Indirectly:**
- `do_refresh()` - Periodic registration thread
- `load_module()` - Module initialization
- `unload_module()` - Module cleanup
- `reload_module()` - Configuration reload
- `get_bindport()` - IAX2 port detection

### Edge Cases Covered

1. **Input Validation:**
   - Empty strings
   - Missing required fields
   - Invalid port numbers
   - Malformed registration strings

2. **Network Errors:**
   - DNS lookup failures
   - Connection timeouts
   - HTTP error responses (4xx, 5xx)

3. **Memory Management:**
   - Proper allocation with flexible array members
   - String buffer bounds checking
   - DNS manager entry cleanup (memory leak fix)
   - Registration list cleanup on unload

4. **Concurrency:**
   - Module reload during registration
   - Multiple simultaneous registrations
   - Thread cleanup on unload

## Troubleshooting

### Integration Tests Fail to Start

**Problem:** Test suite cannot find module
**Solution:** Ensure module is built and installed:
```bash
cd /home/jailuser/git
./rpt_install.sh
```

### Unit Tests Compilation Errors

**Problem:** Missing headers or functions
**Solution:** Unit tests are standalone and don't require Asterisk headers. Verify GCC is installed.

### Mock Server Connection Refused

**Problem:** HTTPS connection fails
**Solution:**
1. Check if mock server is running: `netstat -an | grep 8443`
2. Verify self-signed certificate was generated in `/tmp/`
3. Try HTTP mode by modifying test configuration

### Registration Not Showing in CLI

**Problem:** `rpt show registrations` shows no entries
**Solution:**
1. Check module loaded: `module show like res_rpt_http_registrations`
2. Verify config file exists and is valid
3. Check IAX2 bindport is set in iax.conf
4. Enable debug logging: `core set debug 5`

## Memory Leak Testing

The main commit addressed a memory leak related to DNS manager entries. To verify the fix:

### Using Valgrind (if available):

```bash
valgrind --leak-check=full --show-leak-kinds=all asterisk -cvvvg
# Load module, let it run, unload module, check for leaks
```

### Using Asterisk Memory Debugging:

1. Compile Asterisk with `MALLOC_DEBUG` enabled
2. Run tests
3. Check memory summary: `memory show summary`

### Expected Behavior:

- No memory leaks on module unload
- DNS manager entries properly freed in `cleanup_registrations()`
- Thread cleanup completes without hanging

## Adding New Tests

### Adding Integration Test:

1. Create new directory: `tests/res/new_test_name/`
2. Create `test-config.yaml` following existing patterns
3. Add configuration files in `configs/ast1/`
4. Update `tests/res/tests.yaml` to include new test
5. Run test to verify

### Adding Unit Test:

1. Add new test function in `test_http_registrations_unit.c`
2. Follow naming convention: `test_<functionality_name>()`
3. Use `TEST_ASSERT()` macro for assertions
4. Call function from `main()`
5. Run `make test` to verify

## References

- **Module Source:** `res/res_rpt_http_registrations.c`
- **Config Sample:** `configs/samples/rpt_http_registrations.conf.sample`
- **Asterisk Test Suite:** https://wiki.asterisk.org/wiki/display/AST/Asterisk+Test+Suite+Documentation
- **AllStarLink/app_rpt:** https://github.com/AllStarLink/app_rpt

## CI/CD Integration

Tests are automatically run via GitHub Actions:

See: `.github/workflows/main.yml`

```yaml
- name: Build app_rpt
  run: ./rpt_install.sh
- name: Run tests
  run: phreaknet runtest res/http_registrations
```

## Contributing

When modifying `res_rpt_http_registrations.c`:

1. Run existing tests to ensure no regressions
2. Add new tests for new functionality
3. Verify memory management with valgrind
4. Update this README if test coverage changes
5. Ensure all tests pass before submitting PR

## License

Tests are released under the same license as the Asterisk project (GPL v2).