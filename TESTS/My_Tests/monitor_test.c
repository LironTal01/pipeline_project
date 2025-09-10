// File: plugins/sync/monitor_test.c
// Build: gcc -O2 -Wall -Wextra -pthread -o output/monitor_test \
/*         plugins/sync/monitor.c plugins/sync/monitor_test.c */

// This test suite validates a manual-reset monitor implementation as specified in the assignment.
// It checks:
// 1) Signal-before-wait is remembered (no missed signal).
// 2) Manual-reset semantics: wait keeps succeeding after one signal until reset is called.
// 3) Multiple waiters: a single signal wakes all (because state stays signaled).
// 4) Reset returns the monitor to the blocking state.
// 5) Basic stress/toggle behavior under concurrency.

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "plugins/sync/monitor.h"   // <- your header per assignment

// --- tiny test harness ---
#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
        exit(1); \
    } \
} while (0)

#define PASS(msg) do { printf("[PASS] %s\n", msg); } while (0)

static long ms_since(const struct timespec* t0, const struct timespec* t1) {
    long sec  = (long)(t1->tv_sec - t0->tv_sec);
    long nsec = (long)(t1->tv_nsec - t0->tv_nsec);
    return sec * 1000 + nsec / 1000000;
}

// Helper: wait in a thread and record when it passed
typedef struct {
    monitor_t* mon;
    volatile int done;
} waiter_arg_t;

static void* waiter_fn(void* arg) {
    waiter_arg_t* wa = (waiter_arg_t*)arg;
    int rc = monitor_wait(wa->mon);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] monitor_wait returned %d\n", rc);
        exit(1);
    }
    __atomic_store_n(&wa->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

// Busy-wait (short) for condition with timeout in ms
static void spin_until(volatile int* flag, int timeout_ms) {
    const int step_us = 1000; // 1ms
    int waited = 0;
    while (!__atomic_load_n(flag, __ATOMIC_SEQ_CST) && waited < timeout_ms) {
        usleep(step_us);
        waited += 1;
    }
}

// --- TESTS ---

static void test_signal_before_wait_is_remembered(void) {
    monitor_t m;
    ASSERT_TRUE(monitor_init(&m) == 0, "monitor_init");
    // Signal first
    monitor_signal(&m);

    // Start waiter that should return immediately (no real wait)
    waiter_arg_t wa = { .mon = &m, .done = 0 };
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_t th;
    ASSERT_TRUE(pthread_create(&th, NULL, waiter_fn, &wa) == 0, "pthread_create");

    // It should pass very quickly (< 50ms on a normal machine)
    spin_until(&wa.done, 200); // 200ms timeout to avoid flakiness
    clock_gettime(CLOCK_MONOTONIC, &t1);

    ASSERT_TRUE(wa.done == 1, "waiter did not pass after signal-before-wait");
    long dt = ms_since(&t0, &t1);
    ASSERT_TRUE(dt < 50, "waiter should return immediately when signaled state is set");

    pthread_join(th, NULL);
    monitor_destroy(&m);
    PASS("signal-before-wait is remembered");
}

static void test_manual_reset_semantics(void) {
    monitor_t m;
    ASSERT_TRUE(monitor_init(&m) == 0, "monitor_init");

    // Put monitor into signaled state
    monitor_signal(&m);

    // Two sequential waits in the same thread should both return immediately
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ASSERT_TRUE(monitor_wait(&m) == 0, "wait #1 on signaled monitor");
    ASSERT_TRUE(monitor_wait(&m) == 0, "wait #2 on signaled monitor");
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ASSERT_TRUE(ms_since(&t0, &t1) < 50, "manual-reset: repeated waits should not block");

    // Reset — next wait should block until we signal again
    monitor_reset(&m);

    // Start a waiter that should block now
    waiter_arg_t wa = { .mon = &m, .done = 0 };
    pthread_t th;
    ASSERT_TRUE(pthread_create(&th, NULL, waiter_fn, &wa) == 0, "pthread_create");
    // Give it a moment to block
    usleep(20000); // 20ms

    ASSERT_TRUE(wa.done == 0, "waiter should be blocked after reset");

    // Now signal and verify it passes
    monitor_signal(&m);
    spin_until(&wa.done, 200);
    ASSERT_TRUE(wa.done == 1, "waiter should pass after re-signal");

    pthread_join(th, NULL);
    monitor_destroy(&m);
    PASS("manual reset semantics");
}

static void test_multiple_waiters_single_signal_wakes_all(void) {
    monitor_t m;
    ASSERT_TRUE(monitor_init(&m) == 0, "monitor_init");

    const int N = 8;
    pthread_t th[N];
    waiter_arg_t wa[N];
    for (int i = 0; i < N; ++i) {
        wa[i].mon = &m;
        wa[i].done = 0;
        ASSERT_TRUE(pthread_create(&th[i], NULL, waiter_fn, &wa[i]) == 0, "pthread_create");
    }

    // Let them block
    usleep(30000);

    // Single signal sets state => all waiters should pass (manual-reset)
    monitor_signal(&m);

    // Wait up to 500ms for everyone
    int all_done = 0;
    for (int tries = 0; tries < 500; ++tries) {
        all_done = 1;
        for (int i = 0; i < N; ++i) {
            if (!__atomic_load_n(&wa[i].done, __ATOMIC_SEQ_CST)) { all_done = 0; break; }
        }
        if (all_done) break;
        usleep(1000);
    }
    ASSERT_TRUE(all_done, "not all waiters passed after single signal in manual-reset mode");

    for (int i = 0; i < N; ++i) pthread_join(th[i], NULL);
    monitor_destroy(&m);
    PASS("multiple waiters + single signal (manual-reset)");
}

static void test_reset_blocks_again(void) {
    monitor_t m;
    ASSERT_TRUE(monitor_init(&m) == 0, "monitor_init");
    monitor_signal(&m);

    // Confirm immediate wait
    ASSERT_TRUE(monitor_wait(&m) == 0, "immediate wait on signaled");

    // Reset and verify a timed observation of blocking (no timeout API in spec, so we infer by time)
    monitor_reset(&m);

    waiter_arg_t wa = { .mon = &m, .done = 0 };
    pthread_t th;
    ASSERT_TRUE(pthread_create(&th, NULL, waiter_fn, &wa) == 0, "pthread_create");
    // After 50ms it should still be blocked
    usleep(50000);
    ASSERT_TRUE(wa.done == 0, "after reset, waiter should still be blocked");

    monitor_signal(&m);
    spin_until(&wa.done, 200);
    ASSERT_TRUE(wa.done == 1, "after re-signal, waiter should pass");

    pthread_join(th, NULL);
    monitor_destroy(&m);
    PASS("reset returns monitor to blocking state");
}

static void test_basic_stress_toggle(void) {
    monitor_t m;
    ASSERT_TRUE(monitor_init(&m) == 0, "monitor_init");

    enum { LOOPS = 200 };
    volatile int passed = 0;

    // Worker toggles waits repeatedly; main toggles signal/reset
    waiter_arg_t wa = { .mon = &m, .done = 0 };
    pthread_t th;
    ASSERT_TRUE(pthread_create(&th, NULL, waiter_fn, &wa) == 0, "pthread_create");

    for (int i = 0; i < LOOPS; ++i) {
        // Ensure waiter is blocked
        if (__atomic_load_n(&wa.done, __ATOMIC_SEQ_CST)) {
            // Restart waiter for another wait cycle
            wa.done = 0;
            ASSERT_TRUE(pthread_create(&th, NULL, waiter_fn, &wa) == 0, "pthread_create(loop)");
        }
        // Brief pause, then signal and immediately reset after it passes
        usleep(2000);
        monitor_signal(&m);
        spin_until(&wa.done, 100);
        ASSERT_TRUE(wa.done == 1, "stress: waiter did not pass after signal");
        __atomic_fetch_add(&passed, 1, __ATOMIC_SEQ_CST);
        monitor_reset(&m);
        // Start next round: create a fresh waiter thread
        if (i < LOOPS - 1) {
            wa.done = 0;
            ASSERT_TRUE(pthread_create(&th, NULL, waiter_fn, &wa) == 0, "pthread_create(next)");
        }
    }

    // Final join on last waiter thread (only if it's running)
    if (wa.done == 0) {
        // make sure it can finish
        monitor_signal(&m);
        pthread_join(th, NULL);
    }

    ASSERT_TRUE(passed == LOOPS, "stress: not all cycles passed");
    monitor_destroy(&m);
    PASS("basic stress/toggle");
}

int main(void) {
    printf("=== monitor_test (manual-reset monitor) ===\n");

    test_signal_before_wait_is_remembered();
    test_manual_reset_semantics();
    test_multiple_waiters_single_signal_wakes_all();
    test_reset_blocks_again();
    test_basic_stress_toggle();

    printf("[OK] All monitor tests passed.\n");
    return 0;
}