#!/bin/bash

# Test script for Modular Pipeline System
# This script builds the project and runs comprehensive tests

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Print functions
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
}

# Test counter
TESTS_PASSED=0
TESTS_FAILED=0

# Function to run a test
run_test() {
    local test_name="$1"
    local command="$2"
    local expected="$3"
    
    print_test "Running: $test_name"
    
    local actual
    if actual=$(eval "$command" 2>&1); then
        if [ "$actual" = "$expected" ]; then
            print_status "PASS: $test_name"
            ((TESTS_PASSED++))
        else
            print_error "FAIL: $test_name (Expected: '$expected', Got: '$actual')"
            ((TESTS_FAILED++))
        fi
    else
        print_error "FAIL: $test_name (Command failed)"
        ((TESTS_FAILED++))
    fi
}

# Function to run a test that should fail
run_test_fail() {
    local test_name="$1"
    local command="$2"
    
    print_test "Running: $test_name"
    
    if eval "$command" 2>/dev/null; then
        print_error "FAIL: $test_name (Expected failure, but succeeded)"
        ((TESTS_FAILED++))
    else
        print_status "PASS: $test_name"
        ((TESTS_PASSED++))
    fi
}

# Build the project
print_status "Building project..."
./build.sh

# Test 1: Basic uppercaser + logger pipeline
print_status "Running Test 1: Basic uppercaser + logger pipeline"
run_test "uppercaser + logger" \
    "echo 'hello<END>' | ./output/analyzer 10 uppercaser logger | grep '\[logger\]'" \
    "[logger] HELLO"

# Test 2: Single logger plugin
print_status "Running Test 2: Single logger plugin"
run_test "single logger" \
    "echo 'test message<END>' | ./output/analyzer 5 logger | grep '\[logger\]'" \
    "[logger] test message"

# Test 3: Rotator plugin
print_status "Running Test 3: Rotator plugin"
run_test "rotator" \
    "echo 'hello<END>' | ./output/analyzer 5 rotator | grep '\[rotator\]'" \
    "[rotator] uryyb"

# Test 4: Flipper plugin
print_status "Running Test 4: Flipper plugin"
run_test "flipper" \
    "echo 'Hello World<END>' | ./output/analyzer 5 flipper | grep '\[flipper\]'" \
    "[flipper] hELLO wORLD"

# Test 5: Expander plugin
print_status "Running Test 5: Expander plugin"
run_test "expander" \
    "echo 'hi<END>' | ./output/analyzer 5 expander | grep '\[expander\]'" \
    "[expander] hhiiii"

# Test 6: Typewriter plugin
print_status "Running Test 6: Typewriter plugin"
run_test "typewriter" \
    "echo 'test<END>' | ./output/analyzer 5 typewriter | grep '\[typewriter\]'" \
    "[typewriter] test"

# Test 7: Complex pipeline (uppercaser -> flipper -> logger)
print_status "Running Test 7: Complex pipeline"
run_test "complex pipeline" \
    "echo 'hello<END>' | ./output/analyzer 5 uppercaser flipper logger | grep '\[logger\]'" \
    "[logger] hello"

# Test 8: Empty input
print_status "Running Test 8: Empty input"
run_test "empty input" \
    "echo '<END>' | ./output/analyzer 5 logger | grep '\[logger\]' || echo 'no output'" \
    "no output"

# Test 9: Invalid queue size (should fail)
print_status "Running Test 9: Invalid queue size"
run_test_fail "invalid queue size" \
    "./output/analyzer 0 logger"

# Test 10: Missing plugin (should fail)
print_status "Running Test 10: Missing plugin"
run_test_fail "missing plugin" \
    "./output/analyzer 5 nonexistent"

# Test 11: No plugins (should work but just echo input)
print_status "Running Test 11: No plugins"
run_test "no plugins" \
    "echo 'hello<END>' | ./output/analyzer 5" \
    "hello"

# Test 12: Multiple lines input
print_status "Running Test 12: Multiple lines input"
run_test "multiple lines" \
    "printf 'line1\nline2\n<END>\n' | ./output/analyzer 5 logger | grep '\[logger\]' | wc -l" \
    "2"

# Test 13: Long string
print_status "Running Test 13: Long string"
long_string="$(printf 'a%.0s' {1..100})"
run_test "long string" \
    "echo '${long_string}<END>' | ./output/analyzer 5 logger | grep '\[logger\]' | cut -d' ' -f2- | wc -c" \
    "101"

# Test 14: Special characters
print_status "Running Test 14: Special characters"
run_test "special characters" \
    "echo 'Hello, World! 123<END>' | ./output/analyzer 5 logger | grep '\[logger\]'" \
    "[logger] Hello, World! 123"

# Test 15: All plugins in sequence
print_status "Running Test 15: All plugins in sequence"
run_test "all plugins" \
    "echo 'hello<END>' | ./output/analyzer 5 logger uppercaser rotator flipper expander typewriter | grep '\[typewriter\]'" \
    "[typewriter] HHEELLLLOO"

# Print test results
print_status "Test Results:"
print_status "Tests passed: $TESTS_PASSED"
if [ $TESTS_FAILED -gt 0 ]; then
    print_error "Tests failed: $TESTS_FAILED"
    exit 1
else
    print_status "All tests passed!"
fi

print_status "Testing completed successfully!"
