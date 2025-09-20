#!/bin/bash

# Test script for Analyzer
# This script builds the project and runs comprehensive tests   

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Timeout configuration (can be overridden via env)
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-15}"

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
    
    local actual code=0
    actual=$(timeout "$TIMEOUT_SECONDS" bash -lc "$command" 2>&1) || code=$?
    if [ $code -eq 124 ]; then
        print_error "FAIL: $test_name (Timed out after ${TIMEOUT_SECONDS}s)"
        TESTS_FAILED=$((TESTS_FAILED+1))
        return 0
    fi
    if [ $code -ne 0 ]; then
        print_error "FAIL: $test_name (Command failed with exit $code)"
        TESTS_FAILED=$((TESTS_FAILED+1))
        return 0
    fi
    if [ "$actual" = "$expected" ]; then
        print_status "PASS: $test_name"
        TESTS_PASSED=$((TESTS_PASSED+1))
    else
        print_error "FAIL: $test_name (Expected: '$expected', Got: '$actual')"
        TESTS_FAILED=$((TESTS_FAILED+1))
    fi
    return 0
}

# Function to run a test that should fail
run_test_fail() {
    local test_name="$1"
    local command="$2"
    
    print_test "Running: $test_name"
    
    local code=0
    timeout "$TIMEOUT_SECONDS" bash -lc "$command" >/dev/null 2>&1 || code=$?
    if [ $code -eq 124 ]; then
        print_error "FAIL: $test_name (Timed out after ${TIMEOUT_SECONDS}s)"
        TESTS_FAILED=$((TESTS_FAILED+1))
        return 0
    elif [ $code -eq 0 ]; then
        print_error "FAIL: $test_name (Expected failure, but succeeded)"
        TESTS_FAILED=$((TESTS_FAILED+1))
        return 0
    else
        print_status "PASS: $test_name (command failed as expected)"
        TESTS_PASSED=$((TESTS_PASSED+1))
        return 0
    fi
}

# Build the project
print_status "Building project..."
if ! timeout 60 ./build.sh; then
    print_error "Build timed out or failed"
    exit 1
fi

# Test 1: Basic uppercaser + logger pipeline
print_status "Running Test 1: Basic uppercaser + logger pipeline"
run_test "uppercaser + logger" \
    "printf 'hello\n<END>\n' | ./output/analyzer 10 uppercaser logger | grep '\[logger\]'" \
    "[logger] HELLO"

# Test 2: Single logger plugin
print_status "Running Test 2: Single logger plugin"
run_test "single logger" \
    "printf 'test message\n<END>\n' | ./output/analyzer 5 logger | grep '\[logger\]'" \
    "[logger] test message"

# Test 3: Rotator plugin (right rotate by 1)
print_status "Running Test 3: Rotator plugin"
run_test "rotator" \
    "printf 'abcdef\n<END>\n' | ./output/analyzer 5 rotator | grep '\[rotator\]'" \
    "[rotator] fabcde"

# Test 4: Flipper plugin (reverse string)
print_status "Running Test 4: Flipper plugin"
run_test "flipper" \
    "printf 'hello\n<END>\n' | ./output/analyzer 5 flipper | grep '\[flipper\]'" \
    "[flipper] olleh"

# Test 5: Expander plugin (space between characters)
print_status "Running Test 5: Expander plugin"
run_test "expander" \
    "printf 'abc\n<END>\n' | ./output/analyzer 5 expander | grep '\[expander\]'" \
    "[expander] a b c"

# Test 6: Typewriter plugin
print_status "Running Test 6: Typewriter plugin"
run_test "typewriter" \
    "printf 'test\n<END>\n' | ./output/analyzer 5 typewriter | grep '\[typewriter\]'" \
    "[typewriter] test"

# Test 7: Complex pipeline (uppercaser -> flipper -> logger)
print_status "Running Test 7: Complex pipeline"
run_test "complex pipeline" \
    "printf 'hello\n<END>\n' | ./output/analyzer 5 uppercaser flipper logger | grep '\[logger\]'" \
    "[logger] OLLEH"

# Test 8: Empty input
print_status "Running Test 8: Empty input"
run_test "empty input" \
    "printf '\n<END>\n' | ./output/analyzer 5 logger | grep '^\[logger\] '" \
    "[logger] "

# Test 9: Invalid queue size (should fail)
print_status "Running Test 9: Invalid queue size"
run_test_fail "invalid queue size" \
    "./output/analyzer 0 logger"

# Test 10: Missing plugin (should fail)
print_status "Running Test 10: Missing plugin"
run_test_fail "missing plugin" \
    "./output/analyzer 5 invalid"

# Test 11: No plugins (should fail with usage)
print_status "Running Test 11: No plugins"
run_test_fail "no plugins" \
    "./output/analyzer 5"

# Test 12: Multiple lines input
print_status "Running Test 12: Multiple lines input"
run_test "multiple lines" \
    "printf 'line1\nline2\n<END>\n' | ./output/analyzer 5 logger | grep '\[logger\]' | wc -l" \
    "2"

# Test 13: Long string
print_status "Running Test 13: Long string"
long_string="$(printf 'a%.0s' {1..100})"
run_test "long string" \
    "printf '%s\n<END>\n' \"$long_string\" | ./output/analyzer 5 logger | grep '\[logger\]' | cut -d' ' -f2- | wc -c" \
    "101"

# Test 14: Special characters
print_status "Running Test 14: Special characters"
run_test "special characters" \
    "printf 'Hello, World! 123\n<END>\n' | ./output/analyzer 5 logger | grep '\[logger\]'" \
    "[logger] Hello, World! 123"

# Test 15: All plugins in sequence
print_status "Running Test 15: All plugins in sequence"
run_test "all plugins" \
    "printf 'hello\n<END>\n' | ./output/analyzer 15 uppercaser rotator flipper expander logger | grep '\[logger\]'" \
    "[logger] L L E H O"

# Test 16: Double flipper restores
print_status "Running Test 16: Double flipper restores"
run_test "double flipper restores" \
    "printf 'hello\n<END>\n' | ./output/analyzer 10 flipper flipper logger | grep '\[logger\]'" \
    "[logger] hello"

# Test 17: Duplicate uppercaser
print_status "Running Test 17: Duplicate uppercaser"
run_test "duplicate uppercaser" \
    "printf 'hello\n<END>\n' | ./output/analyzer 10 uppercaser uppercaser logger | grep '\[logger\]'" \
    "[logger] HELLO"

# Test 18: Rotator 4 times returns original
print_status "Running Test 18: Rotator 4 times returns original"
run_test "rotator x4 returns original" \
    "printf 'abcd\n<END>\n' | ./output/analyzer 10 rotator rotator rotator rotator logger | grep '\[logger\]'" \
    "[logger] abcd"

# Results
print_status "Test Results:"
print_status "Tests passed: $TESTS_PASSED"
if [ $TESTS_FAILED -gt 0 ]; then
    print_error "Tests failed: $TESTS_FAILED"
    exit 1
else
    print_status "All tests passed!"
fi

print_status "Testing completed successfully!"