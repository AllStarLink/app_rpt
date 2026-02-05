# Quick Start Guide - HTTP Registrations Tests

This guide will help you quickly run the HTTP registration tests.

## Prerequisites

Choose your testing approach:

### Option 1: Unit Tests (Standalone)
- **Requirements:** GCC compiler only
- **Time:** < 1 minute
- **Scope:** Function-level testing

### Option 2: Integration Tests (Full)
- **Requirements:** Asterisk Test Suite, Python 3, Twisted, StarPy
- **Time:** 1-2 minutes per test
- **Scope:** Complete module testing

### Option 3: Validation Only
- **Requirements:** Python 3, YAML module
- **Time:** < 10 seconds
- **Scope:** Syntax checking

## Quick Start: Validation Only

Fastest way to verify tests are correctly formatted:

```bash
cd /home/jailuser/git
./run_tests.sh --unit-only
```

**Expected Output:**
```
✓ test-config.yaml is valid
✓ test-config.yaml is valid
✓ Mock server syntax is valid

ALL TESTS PASSED ✓
```

## Quick Start: Unit Tests

Run standalone C unit tests (no Asterisk required):

```bash
cd tests/res/http_registrations

# Compile and run
gcc -Wall -Wextra -std=c99 -o test test_http_registrations_unit.c
./test

# Or use Makefile
make test
```

**Expected Output:**
```
========================================
  HTTP Registrations Unit Test Suite
========================================

=== Test: Registration String Parsing ===
  [PASS] Basic format parsing (user@host)
  [PASS] Username parsed correctly
  ...

========================================
  Test Results Summary
========================================
  Passed: 22
  Failed: 0
  Total:  22
========================================

RESULT: SUCCESS
```

## Quick Start: Integration Tests

Run full Asterisk integration tests:

### Step 1: Install Test Suite (if needed)

```bash
# Clone test suite
git clone https://github.com/asterisk/testsuite /usr/src/testsuite

# Install dependencies
cd /usr/src/testsuite
pip3 install -r requirements.txt
```

### Step 2: Copy Tests

```bash
cd /home/jailuser/git
cp -r tests/res /usr/src/testsuite/tests/
```

### Step 3: Run Tests

```bash
cd /usr/src/testsuite

# Using PhreakNet (if available)
phreaknet runtest res/http_registrations
phreaknet runtest res/http_registrations_failure

# OR using runtests.py directly
python3 runtests.py --test=tests/res/http_registrations
python3 runtests.py --test=tests/res/http_registrations_failure
```

## Understanding Test Results

### Unit Test Results

```
[PASS] Test description    ← Individual test passed
[FAIL] Test description    ← Individual test failed (investigate)
```

### Integration Test Results

```
Test: http_registrations
  Status: PASSED           ← Test succeeded
  Duration: 28.5s          ← Test execution time
```

## Troubleshooting

### Problem: GCC not found

**Solution:** Install build tools
```bash
# Ubuntu/Debian
apt-get install build-essential

# RHEL/CentOS
yum install gcc make
```

### Problem: Python modules missing

**Solution:** Install required modules
```bash
pip3 install pyyaml twisted starpy
```

### Problem: Test suite not found

**Solution:** Install Asterisk Test Suite
```bash
git clone https://github.com/asterisk/testsuite /usr/src/testsuite
cd /usr/src/testsuite
pip3 install -r requirements.txt
```

### Problem: Unit tests fail to compile

**Error:** `undefined reference to...`

**Solution:** Unit tests are self-contained and should not need external libraries. If you see linking errors, ensure you're compiling only `test_http_registrations_unit.c` without additional flags.

### Problem: Integration tests timeout

**Possible Causes:**
1. Asterisk not installed
2. Required modules not built
3. Network issues (mock server)

**Solution:** Check logs in test suite output directory

## Manual Testing

If automated tests don't work, you can manually test the module:

### Step 1: Start Mock Server

```bash
cd tests/res/http_registrations
python3 mock_registration_server.py 8443
```

### Step 2: Start Asterisk

```bash
# Copy test config
cp configs/ast1/rpt_http_registrations.conf /etc/asterisk/

# Start Asterisk
asterisk -cvvv
```

### Step 3: Load Module

```
*CLI> module load res_rpt_http_registrations.so
```

### Step 4: Check Status

```
*CLI> rpt show registrations
```

**Expected Output:**
```
Host                  Username    Perceived         Refresh  State
localhost:8443        testnode1   127.0.0.1:4569    60       Registered
localhost:8443        testnode2   127.0.0.1:4569    60       Registered
2 HTTP registrations.
```

### Step 5: Reload Test

```
*CLI> module reload res_rpt_http_registrations.so
*CLI> rpt show registrations
```

### Step 6: Unload Test

```
*CLI> module unload res_rpt_http_registrations.so
```

Check for clean shutdown (no warnings/errors).

## What Each Test Does

### http_registrations Test
1. Starts mock HTTPS server
2. Loads res_rpt_http_registrations module
3. Waits for initial registration
4. Verifies registration status via CLI
5. Reloads configuration
6. Unloads module cleanly

### http_registrations_failure Test
1. Uses invalid configuration
2. Tests error handling
3. Verifies graceful degradation
4. Ensures no crashes

### Unit Tests
- Parsing registration strings
- Building JSON requests
- Parsing HTTP responses
- Managing registration state
- Constructing URLs
- Memory management
- Configuration parsing

## Next Steps

After tests pass:

1. **Review Coverage:** See `README.md` for detailed coverage analysis
2. **Add Tests:** Follow patterns in existing tests to add more
3. **CI/CD Integration:** Use `run_tests.sh` in your CI pipeline
4. **Memory Testing:** Run with Valgrind for leak detection

## Getting Help

- **Test Documentation:** See `README.md` in this directory
- **Test Summary:** See `/home/jailuser/git/TEST_SUMMARY.md`
- **File Listing:** See `/home/jailuser/git/TESTS_CREATED.md`
- **Module Source:** `res/res_rpt_http_registrations.c`

## Test Runner Options

The `run_tests.sh` script in the root directory provides easy test execution:

```bash
./run_tests.sh              # Validation + unit tests (default)
./run_tests.sh --unit-only  # Only unit tests
./run_tests.sh --all        # All tests including integration
./run_tests.sh --help       # Show all options
```

## Minimum Viable Test

If you can only run one test to verify functionality:

```bash
cd tests/res/http_registrations
python3 -c "import yaml; yaml.safe_load(open('test-config.yaml'))"
echo $?  # Should print: 0
```

This validates that at minimum, the test configuration is syntactically correct.

## Time Estimates

| Test Type | Duration | Requirements |
|-----------|----------|-------------|
| Validation | < 10 sec | Python 3 |
| Unit Tests | < 1 min | GCC |
| Integration (single) | 20-30 sec | Test Suite + Asterisk |
| Integration (both) | 40-60 sec | Test Suite + Asterisk |
| Full Suite | 1-2 min | All above |

## Success Criteria

Tests are successful when:

- ✅ All unit test assertions pass (22+)
- ✅ Integration tests complete without timeout
- ✅ No memory leaks detected
- ✅ Module loads/unloads cleanly
- ✅ CLI shows correct registration status

## Common Issues

### SSL/TLS Errors

Mock server generates self-signed certificates. If you see SSL errors:
- The test is configured to handle this
- Or modify `curl_post()` to use `CURLOPT_SSL_VERIFYPEER = 0` for testing

### Port Already in Use

If port 8443 is in use:
- Change port in `test-config.yaml`
- Update `rpt_http_registrations.conf` accordingly

### Asterisk Won't Start

Check for:
- Conflicting Asterisk instances: `killall asterisk`
- Permission issues: Run test suite as appropriate user
- Missing modules: Ensure res_curl, chan_iax2 are built

---

**Need more help?** See the full README.md in this directory.