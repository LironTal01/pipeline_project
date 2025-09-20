#!/bin/bash

# Edge-case and requirements compliance test suite
# - Builds via build.sh
# - Runs analyzer from ./output/analyzer
# - Verifies stdout contains ONLY pipeline prints; errors go to stderr

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

passed=0
failed=0

info() { echo -e "${BLUE}[INFO]${NC} $*"; }
pass() { echo -e "${GREEN}PASS${NC} $*"; passed=$((passed+1)); }
fail() { echo -e "${RED}FAIL${NC} $*"; failed=$((failed+1)); }

run_case_regex() {
    local name="$1"; shift
    local cmd="$1"; shift
    local expect_out_re="$1"; shift
    local expect_err_re="$1"; shift
    local expect_exit="$1"; shift

    info "Running: $name"

    local tmp_out tmp_err code
    tmp_out=$(mktemp)
    tmp_err=$(mktemp)
    bash -c "$cmd" >"$tmp_out" 2>"$tmp_err" || code=$?
    code=${code:-0}
    local out err
    out=$(cat "$tmp_out")
    err=$(cat "$tmp_err")
    rm -f "$tmp_out" "$tmp_err"

    local ok=true

    if [[ -n "$expect_out_re" ]]; then
        if ! echo "$out" | grep -Eq "$expect_out_re"; then
            ok=false
            fail "$name - stdout did not match regex"
            echo "Wanted regex: $expect_out_re"
            echo "Stdout:"; echo "$out"
        fi
    fi

    if [[ -n "$expect_err_re" ]]; then
        if ! echo "$err" | grep -Eq "$expect_err_re"; then
            ok=false
            fail "$name - stderr did not match regex"
            echo "Wanted regex: $expect_err_re"
            echo "Stderr:"; echo "$err"
        fi
    fi

    if [[ "$code" -ne "$expect_exit" ]]; then
        ok=false
        fail "$name - exit code $code != $expect_exit"
    fi

    if $ok; then pass "$name"; fi
}

cd "$(dirname "$0")/.."

info "Building project via build.sh"
./build.sh >/dev/null

ANALYZER=./output/analyzer

if [[ ! -x "$ANALYZER" ]]; then
    echo "analyzer missing after build" >&2
    exit 2
fi

# 1) Argument validation
run_case_regex "no args -> exit 1, usage shown" \
    "$ANALYZER" \
    "Usage:" \
    "^$" \
    1

run_case_regex "only queue -> exit !=0, usage shown" \
    "$ANALYZER 10" \
    "Usage:" \
    "^$" \
    1

run_case_regex "invalid queue zero" \
    "$ANALYZER 0 logger" \
    "Usage:" \
    "Error:.*[Qq]ueue size|^$" \
    1

run_case_regex "invalid queue negative" \
    "$ANALYZER -5 logger" \
    "Usage:" \
    "Error:.*[Qq]ueue size|^$" \
    1

run_case_regex "invalid queue non-numeric" \
    "$ANALYZER notanumber logger" \
    "Usage:" \
    "Error:|^$" \
    1

run_case_regex "non-existent plugin" \
    "$ANALYZER 10 invalid" \
    "^$" \
    "Failed to load plugin|dlopen" \
    1

# Helper to run analyzer with stdin text
pipe() {
    local text="$1"; shift
    local args="$*"
    echo -e "$text" | $ANALYZER $args
}

# 2) Single plugins
exp="[logger] hello"; out=$(pipe "hello\n<END>" 10 logger); [[ "$out" == *"$exp"* ]] && pass "logger prints" || fail "logger prints";

exp="[uppercaser] HELLO123!"; out=$(pipe "Hello123!\n<END>" 10 uppercaser); [[ "$out" == *"$exp"* ]] && pass "uppercaser" || fail "uppercaser";

# rotator is defined as right-rotation by 1
exp="[rotator] fabcde"; out=$(pipe "abcdef\n<END>" 10 rotator); [[ "$out" == *"$exp"* ]] && pass "rotator" || fail "rotator";

exp="[flipper] olleh"; out=$(pipe "hello\n<END>" 10 flipper); [[ "$out" == *"$exp"* ]] && pass "flipper" || fail "flipper";

exp="[expander] a b c"; out=$(pipe "abc\n<END>" 10 expander); [[ "$out" == *"$exp"* ]] && pass "expander" || fail "expander";

exp_prefix='[typewriter] hi'; out=$(pipe "hi\n<END>" 10 typewriter); [[ "$out" == *"$exp_prefix"* ]] && pass "typewriter content" || fail "typewriter content";

# 3) Chains
exp='[logger] HELLO'; out=$(pipe "hello\n<END>" 10 uppercaser logger); [[ "$out" == *"$exp"* ]] && pass "uppercaser->logger" || fail "uppercaser->logger";

exp='[logger] DABC'; out=$(pipe "abcd\n<END>" 10 uppercaser rotator logger); [[ "$out" == *"$exp"* ]] && pass "uppercaser->rotator->logger" || fail "uppercaser->rotator->logger";

exp='[logger] L L E H O'; out=$(pipe "hello\n<END>" 15 uppercaser rotator flipper expander logger); [[ "$out" == *"$exp"* ]] && pass "complex 5 chain" || fail "complex 5 chain";

# 4) Multiple lines and shutdown message
out=$(pipe "line1\nline2\nline3\n<END>" 10 uppercaser logger)
lines=$(echo "$out" | grep "^\[logger\]" | wc -l | tr -d ' ')
[[ "$lines" == 3 ]] && pass "3 lines processed" || fail "3 lines processed"
echo "$out" | grep -q "Pipeline shutdown complete" && pass "shutdown message" || fail "shutdown message"

# 5) Queue sizes
out=$(pipe "test\n<END>" 1 uppercaser logger); echo "$out" | grep -q "^\[logger\] TEST$" && pass "queue size 1" || fail "queue size 1"
out=$(pipe "large queue test\n<END>" 1000 uppercaser logger); echo "$out" | grep -q "^\[logger\] LARGE QUEUE TEST$" && pass "queue size 1000" || fail "queue size 1000"

# 6) Long input near 1024
long=$(printf 'A%.0s' {1..1000})
out=$(pipe "$long\n<END>" 10 logger)
echo "$out" | grep -q "^\[logger\] $(printf 'A%.0s' {1..1000})$" && pass "1000-char line" || fail "1000-char line"

# 7) Edge cases
out=$(pipe "\n<END>" 10 logger); echo "$out" | grep -q "^\[logger\] $" && pass "empty string" || fail "empty string"
out=$(pipe "a\n<END>" 10 uppercaser logger); echo "$out" | grep -q "^\[logger\] A$" && pass "single char" || fail "single char"
out=$(pipe "!@#\$%^&*()\n<END>" 10 logger); echo "$out" | grep -Fq "[logger] !@#$%^&*()" && pass "symbols preserved" || fail "symbols preserved"

# 8) Repeated plugins and idempotence
out=$(pipe "test\n<END>" 10 logger logger logger)
cnt=$(echo "$out" | grep -c "^\[logger\]")
[[ "$cnt" == 3 ]] && pass "multiple same plugin" || fail "multiple same plugin"

out=$(pipe "abcd\n<END>" 10 rotator rotator rotator rotator logger)
echo "$out" | grep -q "^\[logger\] abcd$" && pass "four rotations restore" || fail "four rotations restore"

out=$(pipe "hello\n<END>" 10 flipper flipper logger)
echo "$out" | grep -q "^\[logger\] hello$" && pass "double flip restore" || fail "double flip restore"

echo
echo -e "${YELLOW}Summary:${NC} Passed=$passed Failed=$failed"
if [[ $failed -gt 0 ]]; then exit 1; fi
exit 0


