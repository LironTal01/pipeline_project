#!/bin/bash

# Comprehensive Pipeline Test Suite
# Tests all plugins and edge cases

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

# Print functions
print_status() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((TESTS_PASSED++))
}

print_error() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((TESTS_FAILED++))
}

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# Test function
run_test() {
    local test_name="$1"
    local expected="$2"
    local command="$3"
    
    print_info "Running: $test_name"
    
    local actual=$(eval "$command" 2>/dev/null | grep -E '\[.*\]' | grep -v '<END>' | tail -1)
    
    if [ "$actual" == "$expected" ]; then
        print_status "$test_name: PASS"
    else
        print_error "$test_name: FAIL (Expected '$expected', got '$actual')"
        return 1
    fi
}

# Test function for multiple outputs
run_test_multiple() {
    local test_name="$1"
    local expected_lines="$2"
    local command="$3"
    
    print_info "Running: $test_name"
    
    local actual=$(eval "$command" 2>/dev/null | grep -E '\[.*\]')
    local expected_count=$(echo "$expected_lines" | wc -l)
    local actual_count=$(echo "$actual" | wc -l)
    
    if [ "$actual_count" -eq "$expected_count" ]; then
        print_status "$test_name: PASS ($actual_count lines)"
    else
        print_error "$test_name: FAIL (Expected $expected_count lines, got $actual_count)"
        return 1
    fi
}

echo "=========================================="
echo "    PIPELINE SYSTEM COMPREHENSIVE TESTS"
echo "=========================================="

# Test 1: Single plugin tests
echo
echo "=== SINGLE PLUGIN TESTS ==="

run_test "Logger only" "[logger] hello world" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 logger'

run_test "Uppercaser only" "[uppercaser] HELLO WORLD" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 uppercaser'

run_test "Rotator only" "[rotator] uryyb jbeyq" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 rotator'

run_test "Flipper only" "[flipper] hELLO wORLD" \
    'echo -e "Hello World\n<END>" | ./output/analyzer 5 flipper'

run_test "Expander only" "[expander] hhheeellllllooo wwwooorrrlllddd" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 expander'

run_test "Typewriter only" "[typewriter] hello world" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 typewriter'

# Test 2: Two plugin chains
echo
echo "=== TWO PLUGIN CHAINS ==="

run_test "Logger + Uppercaser" "[uppercaser] HELLO WORLD" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 logger uppercaser'

run_test "Uppercaser + Rotator" "[rotator] URYYB JBEYQ" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 uppercaser rotator'

run_test "Rotator + Flipper" "[flipper] URYYB JBEYQ" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 rotator flipper'

run_test "Flipper + Expander" "[expander] hhhEEELLLLLLOOO wwwOOORRRLLLDDD" \
    'echo -e "Hello World\n<END>" | ./output/analyzer 5 flipper expander'

run_test "Expander + Typewriter" "[typewriter] hheelllloo  wwoorrlldd" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 expander typewriter'

# Test 3: Three plugin chains
echo
echo "=== THREE PLUGIN CHAINS ==="

run_test "Logger + Uppercaser + Rotator" "[rotator] URYYB JBEYQ" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 logger uppercaser rotator'

run_test "Uppercaser + Rotator + Flipper" "[flipper] uryyb jbeyq" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 uppercaser rotator flipper'

run_test "Rotator + Flipper + Expander" "[expander] UURRRRYYYBBB  JJJBBBEYYYQQQ" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 rotator flipper expander'

# Test 4: All plugins chain
echo
echo "=== ALL PLUGINS CHAIN ==="

run_test "All plugins" "[typewriter] UURRRRYYYBBB  JJJBBBEYYYQQQ" \
    'echo -e "hello world\n<END>" | ./output/analyzer 5 logger uppercaser rotator flipper expander typewriter'

# Test 5: Edge cases
echo
echo "=== EDGE CASES ==="

run_test "Empty string" "[logger] " \
    'echo -e "\n<END>" | ./output/analyzer 5 logger'

run_test "Single character" "[uppercaser] A" \
    'echo -e "a\n<END>" | ./output/analyzer 5 uppercaser'

run_test "Numbers only" "[rotator] 123" \
    'echo -e "123\n<END>" | ./output/analyzer 5 rotator'

run_test "Special characters" "[flipper] !@#$%^&*()" \
    'echo -e "!@#$%^&*()\n<END>" | ./output/analyzer 5 flipper'

run_test "Mixed case" "[expander] HeLlO WoRlD" \
    'echo -e "HeLlO WoRlD\n<END>" | ./output/analyzer 5 expander'

# Test 6: Long input
echo
echo "=== LONG INPUT TESTS ==="

long_text="This is a very long text that should test the system's ability to handle large inputs without any issues or memory problems"
run_test "Long text" "[typewriter] $long_text" \
    "echo -e \"$long_text\n<END>\" | ./output/analyzer 5 logger uppercaser rotator flipper expander typewriter"

# Test 7: Multiple lines
echo
echo "=== MULTIPLE LINES TESTS ==="

run_test_multiple "Multiple lines" "2" \
    'echo -e "line1\nline2\n<END>" | ./output/analyzer 5 logger'

# Test 8: Queue size variations
echo
echo "=== QUEUE SIZE TESTS ==="

run_test "Queue size 1" "[uppercaser] HELLO" \
    'echo -e "hello\n<END>" | ./output/analyzer 1 logger uppercaser'

run_test "Queue size 10" "[uppercaser] HELLO" \
    'echo -e "hello\n<END>" | ./output/analyzer 10 logger uppercaser'

run_test "Queue size 100" "[uppercaser] HELLO" \
    'echo -e "hello\n<END>" | ./output/analyzer 100 logger uppercaser'

# Test 9: Error handling
echo
echo "=== ERROR HANDLING TESTS ==="

print_info "Testing invalid plugin name..."
if echo -e "hello\n<END>" | ./output/analyzer 5 invalid_plugin 2>/dev/null; then
    print_error "Invalid plugin should fail"
else
    print_status "Invalid plugin correctly fails"
fi

print_info "Testing no plugins..."
if echo -e "hello\n<END>" | ./output/analyzer 5 2>/dev/null; then
    print_error "No plugins should fail"
else
    print_status "No plugins correctly fails"
fi

# Test 10: Memory stress test
echo
echo "=== MEMORY STRESS TEST ==="

print_info "Running memory stress test..."
stress_text="A"
for i in {1..10}; do
    stress_text="${stress_text}${stress_text}"
done

run_test "Memory stress" "[typewriter] $stress_text" \
    "echo -e \"$stress_text\n<END>\" | ./output/analyzer 5 logger uppercaser rotator flipper expander typewriter"

# Summary
echo
echo "=========================================="
echo "              TEST SUMMARY"
echo "=========================================="
echo -e "Tests Passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Tests Failed: ${RED}$TESTS_FAILED${NC}"
echo "Total Tests: $((TESTS_PASSED + TESTS_FAILED))"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}🎉 ALL TESTS PASSED! 🎉${NC}"
    exit 0
else
    echo -e "${RED}❌ SOME TESTS FAILED! ❌${NC}"
    exit 1
fi
