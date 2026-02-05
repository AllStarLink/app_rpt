#!/bin/bash
#
# Test Runner Script for AllStarLink/app_rpt
#
# This script runs all available tests for the project

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test results tracking
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

# Function to run unit tests
run_unit_tests() {
    print_header "Running Unit Tests"

    cd tests/res/http_registrations

    if [ -f "test_http_registrations_unit.c" ]; then
        if command -v gcc &> /dev/null; then
            print_info "Compiling unit tests..."
            if gcc -Wall -Wextra -std=c99 -g -o test_http_registrations_unit test_http_registrations_unit.c 2>/dev/null; then
                print_success "Compilation successful"

                print_info "Running unit tests..."
                if ./test_http_registrations_unit; then
                    print_success "Unit tests PASSED"
                    PASSED_TESTS=$((PASSED_TESTS + 1))
                else
                    print_error "Unit tests FAILED"
                    FAILED_TESTS=$((FAILED_TESTS + 1))
                fi
                TOTAL_TESTS=$((TOTAL_TESTS + 1))

                # Cleanup
                rm -f test_http_registrations_unit
            else
                print_error "Compilation failed"
                FAILED_TESTS=$((FAILED_TESTS + 1))
                TOTAL_TESTS=$((TOTAL_TESTS + 1))
            fi
        else
            print_warning "gcc not found, skipping unit tests"
        fi
    else
        print_warning "Unit test file not found"
    fi

    cd - > /dev/null
}

# Function to validate test configurations
validate_test_configs() {
    print_header "Validating Test Configurations"

    if command -v python3 &> /dev/null; then
        # Validate YAML files
        for yaml_file in tests/res/http_registrations/test-config.yaml \
                         tests/res/http_registrations_failure/test-config.yaml; do
            if [ -f "$yaml_file" ]; then
                print_info "Validating $yaml_file..."
                if python3 -c "import yaml; yaml.safe_load(open('$yaml_file'))" 2>/dev/null; then
                    print_success "$(basename $yaml_file) is valid"
                    PASSED_TESTS=$((PASSED_TESTS + 1))
                else
                    print_error "$(basename $yaml_file) has syntax errors"
                    FAILED_TESTS=$((FAILED_TESTS + 1))
                fi
                TOTAL_TESTS=$((TOTAL_TESTS + 1))
            fi
        done

        # Validate Python mock server
        if [ -f "tests/res/http_registrations/mock_registration_server.py" ]; then
            print_info "Validating mock_registration_server.py..."
            if python3 -m py_compile tests/res/http_registrations/mock_registration_server.py 2>/dev/null; then
                print_success "Mock server syntax is valid"
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                print_error "Mock server has syntax errors"
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            TOTAL_TESTS=$((TOTAL_TESTS + 1))
        fi
    else
        print_warning "python3 not found, skipping validation"
    fi
}

# Function to check integration test availability
check_integration_tests() {
    print_header "Checking Integration Test Setup"

    if [ -d "/usr/src/testsuite" ]; then
        print_success "Asterisk Test Suite found at /usr/src/testsuite"

        # Check if tests are installed
        if [ -d "/usr/src/testsuite/tests/res/http_registrations" ]; then
            print_success "HTTP registration tests are installed"
        else
            print_warning "HTTP registration tests not installed in test suite"
            print_info "To install, run:"
            print_info "  cp -r tests/res /usr/src/testsuite/tests/"
        fi

        # Check if PhreakNet helper is available
        if command -v phreaknet &> /dev/null; then
            print_success "PhreakNet test runner available"
            print_info "To run integration tests:"
            print_info "  phreaknet runtest res/http_registrations"
            print_info "  phreaknet runtest res/http_registrations_failure"
        else
            print_warning "PhreakNet not found, use runtests.py directly"
            print_info "To run integration tests:"
            print_info "  cd /usr/src/testsuite"
            print_info "  python3 runtests.py --test=tests/res/http_registrations"
        fi
    else
        print_warning "Asterisk Test Suite not found at /usr/src/testsuite"
        print_info "Integration tests require the Asterisk Test Suite"
        print_info "Install from: https://github.com/asterisk/testsuite"
    fi
}

# Function to run integration tests (if test suite is available)
run_integration_tests() {
    print_header "Running Integration Tests"

    if [ -d "/usr/src/testsuite" ]; then
        cd /usr/src/testsuite

        # Ensure tests are copied
        if [ ! -d "tests/res/http_registrations" ]; then
            print_info "Installing tests into test suite..."
            cp -r /home/jailuser/git/tests/res tests/
        fi

        if command -v phreaknet &> /dev/null; then
            # Use PhreakNet
            print_info "Running http_registrations test..."
            if phreaknet runtest res/http_registrations 2>&1 | tee /tmp/test_output.log; then
                print_success "http_registrations test PASSED"
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                print_error "http_registrations test FAILED"
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            TOTAL_TESTS=$((TOTAL_TESTS + 1))

            print_info "Running http_registrations_failure test..."
            if phreaknet runtest res/http_registrations_failure 2>&1 | tee -a /tmp/test_output.log; then
                print_success "http_registrations_failure test PASSED"
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                print_error "http_registrations_failure test FAILED"
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            TOTAL_TESTS=$((TOTAL_TESTS + 1))
        elif command -v python3 &> /dev/null && [ -f "runtests.py" ]; then
            # Use runtests.py directly
            print_info "Running http_registrations test..."
            if python3 runtests.py --test=tests/res/http_registrations 2>&1 | tee /tmp/test_output.log; then
                print_success "http_registrations test PASSED"
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                print_error "http_registrations test FAILED"
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            TOTAL_TESTS=$((TOTAL_TESTS + 1))

            print_info "Running http_registrations_failure test..."
            if python3 runtests.py --test=tests/res/http_registrations_failure 2>&1 | tee -a /tmp/test_output.log; then
                print_success "http_registrations_failure test PASSED"
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                print_error "http_registrations_failure test FAILED"
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            TOTAL_TESTS=$((TOTAL_TESTS + 1))
        else
            print_warning "Cannot run integration tests (no test runner found)"
        fi

        cd - > /dev/null
    else
        print_warning "Skipping integration tests (test suite not installed)"
    fi
}

# Function to print summary
print_summary() {
    print_header "Test Results Summary"

    echo ""
    echo -e "  Total Tests:   ${TOTAL_TESTS}"
    echo -e "  ${GREEN}Passed:        ${PASSED_TESTS}${NC}"
    echo -e "  ${RED}Failed:        ${FAILED_TESTS}${NC}"
    echo ""

    if [ $FAILED_TESTS -eq 0 ] && [ $TOTAL_TESTS -gt 0 ]; then
        echo -e "${GREEN}========================================${NC}"
        echo -e "${GREEN}  ALL TESTS PASSED ✓${NC}"
        echo -e "${GREEN}========================================${NC}"
        return 0
    elif [ $TOTAL_TESTS -eq 0 ]; then
        echo -e "${YELLOW}========================================${NC}"
        echo -e "${YELLOW}  NO TESTS WERE RUN${NC}"
        echo -e "${YELLOW}========================================${NC}"
        return 1
    else
        echo -e "${RED}========================================${NC}"
        echo -e "${RED}  SOME TESTS FAILED ✗${NC}"
        echo -e "${RED}========================================${NC}"
        return 1
    fi
}

# Main execution
main() {
    echo ""
    print_header "AllStarLink/app_rpt Test Runner"
    echo ""

    # Parse command line arguments
    RUN_UNIT=1
    RUN_INTEGRATION=0
    RUN_VALIDATION=1

    while [[ $# -gt 0 ]]; do
        case $1 in
            --unit-only)
                RUN_UNIT=1
                RUN_INTEGRATION=0
                RUN_VALIDATION=1
                shift
                ;;
            --integration-only)
                RUN_UNIT=0
                RUN_INTEGRATION=1
                RUN_VALIDATION=0
                shift
                ;;
            --all)
                RUN_UNIT=1
                RUN_INTEGRATION=1
                RUN_VALIDATION=1
                shift
                ;;
            --help)
                echo "Usage: $0 [OPTIONS]"
                echo ""
                echo "Options:"
                echo "  --unit-only          Run only unit tests"
                echo "  --integration-only   Run only integration tests"
                echo "  --all                Run all tests (default: unit + validation)"
                echo "  --help               Show this help message"
                echo ""
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                echo "Use --help for usage information"
                exit 1
                ;;
        esac
    done

    # Run tests based on flags
    if [ $RUN_VALIDATION -eq 1 ]; then
        validate_test_configs
        echo ""
    fi

    if [ $RUN_UNIT -eq 1 ]; then
        run_unit_tests
        echo ""
    fi

    if [ $RUN_INTEGRATION -eq 1 ]; then
        run_integration_tests
        echo ""
    else
        check_integration_tests
        echo ""
    fi

    # Print summary
    print_summary
    return $?
}

# Run main
main "$@"
exit $?