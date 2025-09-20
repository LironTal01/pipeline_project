/* tests/simple_test.c
 * טסט ידני פשוט לכל פלגאין — ממלאים inputs ו-expected, מפעילים, ומשווים.
 * הטסט מניח שהפלגאין הנבדק הוא השלב האחרון בשרשרת (אין attach), ולכן
 * plugin_common ידפיס ל-STDOUT את התוצרים שורה-שורה.
 *
 * קומפילציה ודוגמה:
 *   gcc -std=c17 -Wall -Wextra -O2 -pthread -I. \
 *     -o tests/simple_test \
 *     tests/simple_test.c \
 *     plugins/plugin_common.c plugins/sync/consumer_producer.c plugins/sync/monitor.c \
 *     plugins/uppercaser.c
 *   ./tests/simple_test
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     /* pipe, dup2, read, close */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../plugins/plugin_common.h"

/* אפשר לשנות את גודל התור אם צריך */
#ifndef QUEUE_SIZE
#define QUEUE_SIZE 8
#endif

/* ================== לכידה של STDOUT ================== */
typedef struct {
    int pipe_r;
    int saved_fd;
    int active;
} capture_t;

static int begin_capture_stdout(capture_t* cap) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    fflush(NULL);

    cap->saved_fd = dup(STDOUT_FILENO);
    if (cap->saved_fd < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
        close(pipefd[0]); close(pipefd[1]);
        close(cap->saved_fd);
        return -1;
    }
    close(pipefd[1]);         /* הכותב כעת הוא STDOUT */
    cap->pipe_r = pipefd[0];  /* נקרא מכאן את מה שנכתב */
    cap->active = 1;
    return 0;
}

static char* end_capture_stdout(capture_t* cap) {
    if (!cap->active) return strdup("");

    fflush(NULL);

    (void)dup2(cap->saved_fd, STDOUT_FILENO);
    close(cap->saved_fd);

    size_t sz = 0, capsz = 4096;
    char* buf = (char*)malloc(capsz);
    if (!buf) { close(cap->pipe_r); cap->active=0; return NULL; }

    for (;;) {
        char tmp[4096];
        ssize_t n = read(cap->pipe_r, tmp, sizeof(tmp));
        if (n <= 0) break;
        if (sz + (size_t)n + 1 > capsz) {
            capsz = (sz + (size_t)n + 1) * 2;
            char* nb = (char*)realloc(buf, capsz);
            if (!nb) { free(buf); close(cap->pipe_r); cap->active=0; return NULL; }
            buf = nb;
        }
        memcpy(buf + sz, tmp, (size_t)n);
        sz += (size_t)n;
    }
    close(cap->pipe_r);
    buf[sz] = '\0';
    cap->active = 0;
    return buf;
}

/* ================== כלי עזר ================== */

/* בניית מחרוזת צפויה מכל השורות עם '\n' ביניהן */
static char* join_lines_with_newlines(const char* const* lines, int n) {
    size_t total = 1; /* ל־'\0' */
    for (int i=0;i<n;++i) total += strlen(lines[i]) + 1; /* + '\n' לכל שורה */
    char* out = (char*)malloc(total);
    if (!out) return NULL;
    size_t pos = 0;
    for (int i=0;i<n;++i) {
        size_t len = strlen(lines[i]);
        memcpy(out + pos, lines[i], len);
        pos += len;
        out[pos++] = '\n';
    }
    out[pos] = '\0';
    return out;
}

/* הדפסת דלתא ידידותית כשיש הבדל */
static void print_diff(const char* expected_joined, const char* got) {
    fprintf(stderr, "\n--- EXPECTED ---\n%s", expected_joined);
    fprintf(stderr, "---   GOT    ---\n%s", got);
    /* השוואה שורה-שורה לציון היכן נכשל */
    int line = 1;
    const char *pe = expected_joined, *pg = got;
    while (*pe && *pg) {
        const char* le = strchr(pe, '\n');
        const char* lg = strchr(pg, '\n');
        size_t ne = le ? (size_t)(le - pe) : strlen(pe);
        size_t ng = lg ? (size_t)(lg - pg) : strlen(pg);
        if (ne != ng || strncmp(pe, pg, ne < ng ? ne : ng) != 0) {
            fprintf(stderr, "\n[DIFF] mismatch at line %d\n", line);
            fprintf(stderr, "exp: %.*s\n", (int)ne, pe);
            fprintf(stderr, "got: %.*s\n", (int)ng, pg);
            break;
        }
        line++;
        pe = le ? le + 1 : pe + ne;
        pg = lg ? lg + 1 : pg + ng;
    }
}

/* ================== כאן את עורכת: קלט/פלט ================== */

/* הכניסי כאן את הקלטים שאת רוצה לבדוק (שורה = פריט עבודה אחד) */
static const char* inputs[] = {
    "",            /* דוגמה: מחרוזת ריקה */
    "h",           /* תו אחד */
    "ab",
    "Hello",
    "Hello World!",
    "xyz123"
};

/* הכניסי כאן את הפלט הצפוי מכל קלט, לפי הפלגאין שאת בודקת */
static const char* expected[] = {
    /* לדוגמה עבור uppercaser: */
    "",            /* דוגמה: מחרוזת ריקה */
    "h",           /* תו אחד */
    "ab",
    "Hello",
    "Hello World!",
    "xyz123"
    /* הערה: כל שורה ב-expected צריכה להיות בדיוק תוצאת העיבוד של השורה המתאימה ב-inputs */
};

static const int N_CASES = (int)(sizeof(inputs)/sizeof(inputs[0]));

/* ================== הטסט עצמו ================== */

int main(void) {
    if ((int)(sizeof(expected)/sizeof(expected[0])) != N_CASES) {
        fprintf(stderr, "expected[] and inputs[] length mismatch\n");
        return 2;
    }

    /* לכידה של STDOUT בזמן הריצה */
    capture_t cap = {0};
    if (begin_capture_stdout(&cap) != 0) {
        fprintf(stderr, "failed to capture stdout\n");
        return 2;
    }

    /* אתחול הפלגאין הנבדק (אין שלב הבא, אז plugin_common ידפיס STDOUT) */
    const char* err = plugin_init(QUEUE_SIZE);
    if (err) {
        (void)end_capture_stdout(&cap);
        fprintf(stderr, "plugin_init failed: %s\n", err);
        return 2;
    }

    /* הזרמת כל הקלטים */
    for (int i=0;i<N_CASES;++i) {
        err = plugin_place_work(inputs[i]);
        if (err) {
            (void)end_capture_stdout(&cap);
            fprintf(stderr, "plugin_place_work failed on index %d: %s\n", i, err);
            return 2;
        }
    }

    /* שליחת END לחסימה מסודרת וסיום */
    err = plugin_place_work("<END>");
    if (err) {
        (void)end_capture_stdout(&cap);
        fprintf(stderr, "plugin_place_work(<END>) failed: %s\n", err);
        return 2;
    }

    /* להמתין לסיום ולנקות */
    err = plugin_wait_finished();
    if (err) {
        (void)end_capture_stdout(&cap);
        fprintf(stderr, "plugin_wait_finished failed: %s\n", err);
        return 2;
    }
    err = plugin_fini();
    if (err) {
        (void)end_capture_stdout(&cap);
        fprintf(stderr, "plugin_fini failed: %s\n", err);
        return 2;
    }

    /* קריאת הפלט שנלכד */
    char* got = end_capture_stdout(&cap);
    if (!got) {
        fprintf(stderr, "failed to read captured stdout\n");
        return 2;
    }

    /* בניית ה-expected (כל שורה + '\n') */
    char* exp_joined = join_lines_with_newlines(expected, N_CASES);
    if (!exp_joined) {
        free(got);
        fprintf(stderr, "failed to build expected string\n");
        return 2;
    }

    /* השוואה */
    if (strcmp(got, exp_joined) != 0) {
        fprintf(stderr, "[FAIL] output mismatch\n");
        print_diff(exp_joined, got);
        free(exp_joined);
        free(got);
        return 1;
    }

    /* הצלחה */
    printf("[PASS] all %d cases matched expected output\n", N_CASES);
    free(exp_joined);
    free(got);
    return 0;
}
