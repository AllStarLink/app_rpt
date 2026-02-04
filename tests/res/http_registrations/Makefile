# Makefile for HTTP Registrations Tests
#
# This Makefile compiles and runs unit tests for res_rpt_http_registrations

CC=gcc
CFLAGS=-Wall -Wextra -std=c99 -g
LDFLAGS=

# Test executable
TEST_BINARY=test_http_registrations_unit

# Source files
TEST_SRC=test_http_registrations_unit.c

.PHONY: all test clean

all: $(TEST_BINARY)

$(TEST_BINARY): $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: $(TEST_BINARY)
	@echo "Running unit tests..."
	@./$(TEST_BINARY)

clean:
	rm -f $(TEST_BINARY)

help:
	@echo "Available targets:"
	@echo "  all    - Build the test binary (default)"
	@echo "  test   - Build and run the unit tests"
	@echo "  clean  - Remove built files"
	@echo "  help   - Show this help message"