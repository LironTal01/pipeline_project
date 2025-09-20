/* consumer_producer_test.c (fixed, portable, warnings-free)
 *
 * Comprehensive tests for consumer_producer.{h,c} + monitor.{h,c}
 * Covers:
 *  - Init argument validation
 *  - Basic FIFO semantics (single producer/consumer)
 *  - Blocking behavior: consumer waits on empty, producer waits on full
 *  - Finish behavior: consumers return NULL when empty+finished; producers rejected after finish
 *  - Stress test: multiple producers/consumers
 *  - Long string payloads
 *  - Get on uninitialized queue
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#ifdef _WIN32
  #include <sys/timeb.h>
  #include <windows.h>
#else
  #include <sys/time.h>
#endif

#include "consumer_producer.h"  /* depends on monitor.h */

/* ---------- Minimal test harness ---------- */
static int g_tests_run = 0;
static int g_tests_failed = 0;

#define FAIL_MSG(fmt, ...) do { \
    fprintf(stderr, "    [FAIL] " fmt "\n", ##__VA_ARGS__); \
    g_tests_failed++; \
} while(0)

#define ASSERT_TRUE(expr) do { \
    g_tests_run++; \
    if(!(expr)) { FAIL_MSG("ASSERT_TRUE failed: %s at %s:%d", #expr, __FILE__, __LINE__); return -1; } \
} while(0)

#define ASSERT_FALSE(expr) do { \
    g_tests_run++; \
    if((expr)) { FAIL_MSG("ASSERT_FALSE failed: %s at %s:%d", #expr, __FILE__, __LINE__); return -1; } \
} while(0)

#define ASSERT_EQ_INT(a,b) do { \
    g_tests_run++; \
    if((a)!=(b)) { FAIL_MSG("ASSERT_EQ_INT failed: %s=%d, %s=%d at %s:%d", #a,(int)(a), #b,(int)(b), __FILE__, __LINE__); return -1; } \
} while(0)

/* Compare strings safely (NULL-aware) */
static int str_eq_nullable(const char* a, const char* b) {
    if (a == b) return 1;   /* covers a==b==NULL */
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

#define ASSERT_STREQ(a,b) do {                                           \
    g_tests_run++;                                                       \
    if (!str_eq_nullable((a),(b))) {                                     \
        FAIL_MSG("ASSERT_STREQ failed:\n  A: \"%s\"\n  B: \"%s\"",       \
                 (a)?(a):"(null)", (b)?(b):"(null)");                    \
        return -1;                                                       \
    }                                                                    \
} while(0)

/* For functions that return const char* error (NULL on success) */
#define ASSERT_OK(err) do {                  \
    g_tests_run++;                           \
    if ((err) != NULL) {                     \
        FAIL_MSG("ASSERT_OK failed: %s",     \
                 (err));                     \
        return -1;                           \
    }                                        \
} while(0)

/* Sleep helper (milliseconds) */
static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Monotonic timestamp in milliseconds (portable-ish) */
static long long now_ms(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((long long)ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
#elif defined(_WIN32)
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (long long)(counter.QuadPart * 1000LL / freq.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
#endif
}

/* ---------- Thread payloads ---------- */

typedef struct {
    consumer_producer_t* q;
    const char** msgs;
    int n;
    const char* last_err; /* NULL if all puts succeeded */
    long long t_end_ms;   /* time when thread completed */
} producer_args_t;

typedef struct {
    consumer_producer_t* q;
    char** out;     /* array of size n (or more) to store received strings */
    int max_out;    /* capacity of 'out' array */
    int count;      /* how many actually received (non-NULL) */
    int stop_on_null;  /* if 1: stop when get() returns NULL (finished+empty) */
    long long t_end_ms;
} consumer_args_t;

static void* producer_thread(void* arg) {
    producer_args_t* pa = (producer_args_t*)arg;
    pa->last_err = NULL;
    for (int i = 0; i < pa->n; i++) {
        const char* err = consumer_producer_put(pa->q, pa->msgs[i]);
        if (err != NULL) { pa->last_err = err; break; }
    }
    pa->t_end_ms = now_ms();
    return NULL;
}

static void* consumer_thread(void* arg) {
    consumer_args_t* ca = (consumer_args_t*)arg;
    ca->count = 0;
    for (;;) {
        char* s = consumer_producer_get(ca->q);
        if (s == NULL) {
            if (ca->stop_on_null) break; /* finished + empty */
            break;
        }
        if (ca->count < ca->max_out) {
            ca->out[ca->count] = s;
        } else {
            /* shouldn't happen in controlled tests; free to avoid leak */
            free(s);
        }
        ca->count++;
        if (!ca->stop_on_null && ca->count >= ca->max_out) break;
    }
    ca->t_end_ms = now_ms();
    return NULL;
}

/* ---------- Tests ---------- */

/* 1) Init argument validation */
static int test_init_invalid(void) {
    consumer_producer_t q;
    memset(&q, 0, sizeof(q));

    const char* err;
    err = consumer_producer_init(NULL, 4);
    ASSERT_STREQ(err, "queue pointer is NULL");

    err = consumer_producer_init(&q, 0);
    ASSERT_STREQ(err, "capacity must be > 0");

    return 0;
}

/* 2) Basic FIFO single producer/consumer */
static int test_basic_fifo(void) {
    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, 4);
    ASSERT_OK(err);

    ASSERT_OK(consumer_producer_put(&q, "A"));
    ASSERT_OK(consumer_producer_put(&q, "B"));
    ASSERT_OK(consumer_producer_put(&q, "C"));

    char* s1 = consumer_producer_get(&q);
    char* s2 = consumer_producer_get(&q);
    char* s3 = consumer_producer_get(&q);

    ASSERT_STREQ(s1, "A");
    ASSERT_STREQ(s2, "B");
    ASSERT_STREQ(s3, "C");

    free(s1); free(s2); free(s3);

    consumer_producer_signal_finished(&q);
    char* s4 = consumer_producer_get(&q);
    ASSERT_TRUE(s4 == NULL);

    consumer_producer_destroy(&q);
    return 0;
}

/* 3) Consumer blocks on empty until producer puts */
static int test_blocking_consumer_on_empty(void) {
    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, 2);
    ASSERT_OK(err);

    long long t0 = now_ms();

    pthread_t ct;
    char* out[1] = {0};
    consumer_args_t ca = { .q=&q, .out=out, .max_out=1, .stop_on_null=0, .count=0, .t_end_ms=0 };
    pthread_create(&ct, NULL, consumer_thread, &ca);

    /* Let consumer block for a bit */
    sleep_ms(200);

    /* Now produce one item */
    ASSERT_OK(consumer_producer_put(&q, "X"));

    pthread_join(ct, NULL);

    long long elapsed = ca.t_end_ms - t0;
    ASSERT_TRUE(elapsed >= 150); /* Should have been blocked at least ~150ms */

    ASSERT_EQ_INT(ca.count, 1);
    ASSERT_STREQ(out[0], "X");
    free(out[0]);

    consumer_producer_signal_finished(&q);
    consumer_producer_destroy(&q);
    return 0;
}

/* 4) Producer blocks on full until consumer gets */
static int test_blocking_producer_on_full(void) {
    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, 1);
    ASSERT_OK(err);

    /* Fill the single-slot queue */
    ASSERT_OK(consumer_producer_put(&q, "A"));

    long long t0 = now_ms();

    pthread_t pt;
    const char* msgs[] = {"B"};
    producer_args_t pa = { .q=&q, .msgs=msgs, .n=1, .last_err=NULL, .t_end_ms=0 };
    pthread_create(&pt, NULL, producer_thread, &pa);

    /* Let producer block for a bit */
    sleep_ms(200);

    /* Now consume one, allowing the blocked producer to proceed */
    char* got = consumer_producer_get(&q);
    ASSERT_STREQ(got, "A");
    free(got);

    pthread_join(pt, NULL);

    long long elapsed = pa.t_end_ms - t0;
    ASSERT_TRUE(elapsed >= 150); /* Should have been blocked at least ~150ms */
    ASSERT_TRUE(pa.last_err == NULL); /* put should eventually succeed */

    consumer_producer_signal_finished(&q);
    consumer_producer_destroy(&q);
    return 0;
}

/* 5) Finish behavior: consumers unblocked (NULL), producers rejected */
static int test_finish_behavior(void) {
    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, 2);
    ASSERT_OK(err);

    /* Start a consumer that waits on empty and stops on NULL */
    pthread_t ct;
    char* out[4] = {0};
    consumer_args_t ca = { .q=&q, .out=out, .max_out=4, .stop_on_null=1, .count=0, .t_end_ms=0 };
    pthread_create(&ct, NULL, consumer_thread, &ca);

    /* Give it time to block, then finish */
    sleep_ms(150);
    consumer_producer_signal_finished(&q);

    pthread_join(ct, NULL);
    ASSERT_EQ_INT(ca.count, 0);       /* Nothing was produced */

    /* After finish, a producer should be rejected */
    const char* perr = consumer_producer_put(&q, "X");
    ASSERT_TRUE(perr != NULL);

    consumer_producer_destroy(&q);
    return 0;
}

/* 6) Stress test: multiple producers/consumers */
static int test_stress_multi(void) {
    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, 32);
    ASSERT_OK(err);

    enum { P = 4, C = 4, N_PER_P = 200 }; /* total items = 800 */

    /* Prepare messages for each producer */
    const char** msgbuf[P];
    for (int p = 0; p < P; ++p) {
        msgbuf[p] = (const char**)calloc(N_PER_P, sizeof(char*));
        if (!msgbuf[p]) { FAIL_MSG("alloc failed"); return -1; }
        for (int i = 0; i < N_PER_P; ++i) {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "p%d:%d", p, i);
            char* s = (char*)malloc(strlen(tmp)+1);
            if (!s) { FAIL_MSG("alloc failed"); return -1; }
            strcpy(s, tmp);
            msgbuf[p][i] = s; /* queue duplicates; original freed below */
        }
    }

    /* Launch producers */
    pthread_t pt[P];
    producer_args_t pa[P];
    for (int p = 0; p < P; ++p) {
        pa[p].q = &q;
        pa[p].msgs = msgbuf[p];
        pa[p].n = N_PER_P;
        pa[p].last_err = NULL;
        pa[p].t_end_ms = 0;
        pthread_create(&pt[p], NULL, producer_thread, &pa[p]);
    }

    /* Launch consumers (collect until finished) */
    pthread_t ct[C];
    consumer_args_t ca[C];
    for (int c = 0; c < C; ++c) {
        ca[c].q = &q;
        ca[c].max_out = P * N_PER_P; /* more than enough */
        ca[c].out = (char**)calloc((size_t)ca[c].max_out, sizeof(char*));
        ca[c].count = 0;
        ca[c].stop_on_null = 1;
        ca[c].t_end_ms = 0;
        pthread_create(&ct[c], NULL, consumer_thread, &ca[c]);
    }

    /* Wait for producers to finish, then signal finished */
    for (int p = 0; p < P; ++p) pthread_join(pt[p], NULL);
    consumer_producer_signal_finished(&q);

    /* Join consumers */
    int total = 0;
    for (int c = 0; c < C; ++c) {
        pthread_join(ct[c], NULL);
        total += ca[c].count;
    }

    /* All produced items must be consumed exactly once */
    ASSERT_EQ_INT(total, P * N_PER_P);

    /* Free per-consumer received strings */
    for (int c = 0; c < C; ++c) {
        for (int i = 0; i < ca[c].count; ++i) free(ca[c].out[i]);
        free(ca[c].out);
    }

    /* Free producer message buffers (originals) */
    for (int p = 0; p < P; ++p) {
        for (int i = 0; i < N_PER_P; ++i) free((void*)msgbuf[p][i]);
        free(msgbuf[p]);
    }

    consumer_producer_destroy(&q);
    return 0;
}

/* 7) Long string payload (~1024 bytes) */
static int test_long_string(void) {
    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, 2);
    ASSERT_OK(err);

    /* build 1023 'A's + '\0' (total 1024) */
    char* big = (char*)malloc(1024);
    ASSERT_TRUE(big != NULL);
    memset(big, 'A', 1023);
    big[1023] = '\0';

    ASSERT_OK(consumer_producer_put(&q, big));
    free(big); /* queue took ownership via strdup */

    char* got = consumer_producer_get(&q);
    ASSERT_TRUE(got != NULL);
    ASSERT_EQ_INT((int)strlen(got), 1023);
    for (int i = 0; i < 10; ++i) ASSERT_TRUE(got[i] == 'A'); /* spot-check */
    free(got);

    consumer_producer_signal_finished(&q);
    consumer_producer_destroy(&q);
    return 0;
}

/* 8) Get on uninitialized queue */
static int test_get_uninitialized(void) {
    consumer_producer_t q;
    memset(&q, 0, sizeof(q));
    char* s = consumer_producer_get(&q);
    ASSERT_TRUE(s == NULL);
    return 0;
}

/* 9) Put after finished -> rejected */
static int test_put_after_finished(void) {
    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, 2);
    ASSERT_OK(err);

    consumer_producer_signal_finished(&q);

    const char* perr = consumer_producer_put(&q, "data");
    ASSERT_TRUE(perr != NULL);

    consumer_producer_destroy(&q);
    return 0;
}

/* ---------- Runner ---------- */

static int run_test(const char* name, int (*fn)(void)) {
    printf("[ RUN  ] %s\n", name);
    int rc = fn();
    if (rc == 0) {
        printf("[ PASS ] %s\n", name);
    } else {
        printf("[ FAIL ] %s (rc=%d)\n", name, rc);
    }
    return rc;
}

int main(void) {
    int rc = 0;

    rc |= run_test("init_invalid",              test_init_invalid);
    rc |= run_test("basic_fifo",                test_basic_fifo);
    rc |= run_test("blocking_consumer_on_empty",test_blocking_consumer_on_empty);
    rc |= run_test("blocking_producer_on_full", test_blocking_producer_on_full);
    rc |= run_test("finish_behavior",           test_finish_behavior);
    rc |= run_test("stress_multi",              test_stress_multi);
    rc |= run_test("long_string",               test_long_string);
    rc |= run_test("get_uninitialized",         test_get_uninitialized);
    rc |= run_test("put_after_finished",        test_put_after_finished);

    printf("\n=== SUMMARY ===\n");
    printf("Assertions run: %d\n", g_tests_run);
    printf("Tests failed:   %d\n", g_tests_failed);
    printf("Exit code:      %d\n", rc != 0 ? 1 : 0);
    return rc != 0 ? 1 : 0;
}
