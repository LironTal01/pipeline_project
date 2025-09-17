/* tests/test_plugin_common.c
 * Thorough unit tests for plugin_common.c using stub process/next functions.
 *
 * Build (from project root):
 *   gcc -std=c17 -Wall -Wextra -O2 -pthread -I. \
 *     -o tests/test_plugin_common \
 *     tests/test_plugin_common.c \
 *     plugins/plugin_common.c plugins/sync/consumer_producer.c plugins/sync/monitor.c
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../plugins/plugin_common.h"

/* ---------------- Mini test harness ---------------- */
static int g_runs=0, g_fails=0;

#define FAIL(fmt, ...) do { \
    fprintf(stderr, "    [FAIL] " fmt "\n", ##__VA_ARGS__); \
    g_fails++; \
    return -1; \
} while(0)

#define OK(expr) do { g_runs++; if(!(expr)) FAIL("Expected true: %s (%s:%d)", #expr, __FILE__, __LINE__); } while(0)
#define ASSERT_OK(err) do { g_runs++; const char* _e=(err); if(_e) FAIL("Expected OK (NULL) but got: %s", _e); } while(0)
#define ASSERT_ERR(err) do { g_runs++; const char* _e=(err); if(!_e) FAIL("Expected error (non-NULL) but got NULL"); } while(0)
#define STREQ(a,b) ((a)==(b) || ((a)&&(b)&&strcmp((a),(b))==0))

/* --------- stdout capture (only used when last plugin prints) --------- */
typedef struct { int pipe_r, saved_fd, active; } cap_t;

static int cap_begin_stdout(cap_t* c){
    int p[2]; if (pipe(p)!=0) return -1;
    fflush(NULL);
    c->saved_fd = dup(STDOUT_FILENO); if (c->saved_fd<0){ close(p[0]); close(p[1]); return -1; }
    if (dup2(p[1], STDOUT_FILENO)<0){ close(p[0]); close(p[1]); close(c->saved_fd); return -1; }
    close(p[1]); c->pipe_r = p[0]; c->active=1; return 0;
}
static char* cap_end_stdout(cap_t* c){
    if(!c->active) return strdup("");
    fflush(NULL);
    (void)dup2(c->saved_fd, STDOUT_FILENO); close(c->saved_fd);
    size_t sz=0, cap=4096; char* buf=(char*)malloc(cap); if(!buf){ close(c->pipe_r); c->active=0; return NULL; }
    for(;;){ char tmp[4096]; ssize_t n=read(c->pipe_r,tmp,sizeof(tmp)); if(n<=0) break;
        if(sz+(size_t)n+1>cap){ cap=(sz+(size_t)n+1)*2; char* nb=realloc(buf,cap); if(!nb){ free(buf); close(c->pipe_r); c->active=0; return NULL; } buf=nb; }
        memcpy(buf+sz,tmp,(size_t)n); sz+=(size_t)n;
    }
    close(c->pipe_r); buf[sz]='\0'; c->active=0; return buf;
}

/* ---------------------- Stubs & helpers ---------------------- */

/* A simple “process_function” that can:
   - uppercase (mode 'U')
   - pass-through (mode 'I')
   - drop a specific token (mode 'D', drop_if matches input => return NULL)
   - delay per char (mode 'T', nanosleep 5ms per char)
*/
typedef struct {
    char mode;           /* 'U','I','D','T' */
    const char* drop_if; /* used only when mode=='D' */
} proc_cfg_t;

static char* xdup(const char* s){ size_t n=strlen(s)+1; char* p=(char*)malloc(n); if(!p) return NULL; memcpy(p,s,n); return p; }

static const char* proc_fn(const char* in){
    static proc_cfg_t* cfg = NULL; /* set via set_proc_cfg() below */
    (void)cfg;
    /* The trick: we store cfg pointer in a static via a setter. */
    extern proc_cfg_t* get_proc_cfg(void); cfg = get_proc_cfg();

    if (cfg->mode=='D' && cfg->drop_if && STREQ(in, cfg->drop_if)) {
        return NULL; /* drop */
    }
    if (cfg->mode=='T') {
        struct timespec ts = {0, 5*1000*1000}; /* 5ms per char */
        for (const char* p=in; *p; ++p) nanosleep(&ts,NULL);
        return xdup(in);
    }
    if (cfg->mode=='U') {
        size_t n=strlen(in); char* out=(char*)malloc(n+1); if(!out) return NULL;
        for(size_t i=0;i<n;++i) out[i]=(char) ( (in[i]>='a'&&in[i]<='z') ? (in[i]-'a'+'A') : in[i] );
        out[n]='\0'; return out;
    }
    /* default pass-through */
    return xdup(in);
}
/* global accessors to avoid passing user-data through plugin_common */
static proc_cfg_t g_proc_cfg = { 'I', NULL };
proc_cfg_t* get_proc_cfg(void){ return &g_proc_cfg; }
static void set_proc_cfg(char mode, const char* drop_if){ g_proc_cfg.mode=mode; g_proc_cfg.drop_if=drop_if; }

/* Downstream “next_place_work” sink that records what it got */
typedef struct {
    char** lines; int cap, count; int saw_end; int fail_on_index; /* if >=0: return error on that call */
} sink_t;
static sink_t g_sink = {0};

static void sink_reset(int cap, int fail_on_index){
    if(g_sink.lines){ for(int i=0;i<g_sink.count;++i) free(g_sink.lines[i]); free(g_sink.lines); }
    g_sink.lines = (char**)calloc((size_t)cap, sizeof(char*));
    g_sink.cap = cap; g_sink.count=0; g_sink.saw_end=0; g_sink.fail_on_index=fail_on_index;
}
static void sink_free(void){ sink_reset(0,-1); }

static const char* sink_next(const char* s){
    if (STREQ(s,"<END>")) { g_sink.saw_end=1; return NULL; }
    if (g_sink.count < g_sink.cap) g_sink.lines[g_sink.count++] = xdup(s);
    /* simulate an error from downstream on a specific call index */
    if (g_sink.fail_on_index>=0 && g_sink.count-1 == g_sink.fail_on_index) return "downstream error";
    return NULL;
}

/* Utility to join sink lines with \n for compare */
static char* sink_join(void){
    size_t total=1; for(int i=0;i<g_sink.count;++i) total += strlen(g_sink.lines[i])+1;
    char* s=(char*)malloc(total); if(!s) return NULL; size_t pos=0;
    for(int i=0;i<g_sink.count;++i){ size_t n=strlen(g_sink.lines[i]); memcpy(s+pos,g_sink.lines[i],n); pos+=n; s[pos++]='\n'; }
    s[pos]='\0'; return s;
}

/* --------------------------- Tests --------------------------- */

/* 1) As last stage (no attach): items should print to stdout; <END> not printed */
static int test_print_as_last(void){
    cap_t cap={0}; OK(cap_begin_stdout(&cap)==0);

    set_proc_cfg('U', NULL); /* uppercase */
    ASSERT_OK(common_plugin_init(proc_fn, "p1", 8));
    ASSERT_OK(plugin_place_work("ab"));
    ASSERT_OK(plugin_place_work("He llo"));
    ASSERT_OK(plugin_place_work("<END>"));
    ASSERT_OK(plugin_wait_finished());
    ASSERT_OK(plugin_fini());

    char* out = cap_end_stdout(&cap); OK(out!=NULL);
    /* expected stdout */
    const char* exp = "AB\nHE LLO\n";
    if (!STREQ(out, exp)) { fprintf(stderr,"got:\n%s---\nexp:\n%s---\n", out, exp); free(out); FAIL("stdout mismatch"); }
    free(out);
    return 0;
}

/* 2) With attach(): nothing printed; forwarded lines recorded; <END> forwarded */
static int test_forward_with_attach(void){
    sink_reset(16, -1);
    set_proc_cfg('I', NULL); /* pass-through */
    ASSERT_OK(common_plugin_init(proc_fn, "p2", 8));
    plugin_attach(sink_next);

    ASSERT_OK(plugin_place_work("a"));
    ASSERT_OK(plugin_place_work("b"));
    ASSERT_OK(plugin_place_work("<END>"));
    ASSERT_OK(plugin_wait_finished());
    ASSERT_OK(plugin_fini());

    OK(g_sink.saw_end == 1);
    OK(g_sink.count == 2);
    OK(STREQ(g_sink.lines[0],"a") && STREQ(g_sink.lines[1],"b"));
    sink_free();
    return 0;
}

/* 3) process_function returns NULL for "DROP" -> item is dropped */
static int test_drop_item(void){
    sink_reset(8, -1);
    set_proc_cfg('D', "DROP");
    ASSERT_OK(common_plugin_init(proc_fn, "p3", 8));
    plugin_attach(sink_next);

    ASSERT_OK(plugin_place_work("X"));
    ASSERT_OK(plugin_place_work("DROP"));
    ASSERT_OK(plugin_place_work("Y"));
    ASSERT_OK(plugin_place_work("<END>"));
    ASSERT_OK(plugin_wait_finished());
    ASSERT_OK(plugin_fini());

    OK(g_sink.count == 2);
    OK(STREQ(g_sink.lines[0],"X") && STREQ(g_sink.lines[1],"Y"));
    OK(g_sink.saw_end == 1);
    sink_free();
    return 0;
}

/* 4) Double init must fail the second time */
static int test_double_init_fails(void){
    set_proc_cfg('I', NULL);
    ASSERT_OK(common_plugin_init(proc_fn, "p4", 4));
    const char* e2 = common_plugin_init(proc_fn, "p4", 4);
    ASSERT_ERR(e2);
    ASSERT_OK(plugin_place_work("<END>"));
    ASSERT_OK(plugin_wait_finished());
    ASSERT_OK(plugin_fini());
    return 0;
}

/* 5) place_work before init -> error */
static int test_place_before_init_errors(void){
    const char* e = plugin_place_work("x");
    ASSERT_ERR(e);
    return 0;
}

/* 6) wait then fini (double join safety) */
static int test_wait_then_fini_ok(void){
    set_proc_cfg('I', NULL);
    ASSERT_OK(common_plugin_init(proc_fn, "p6", 2));
    ASSERT_OK(plugin_place_work("<END>"));
    ASSERT_OK(plugin_wait_finished());
    ASSERT_OK(plugin_fini()); /* should be fine even after wait */
    return 0;
}

/* 7) fini without sending <END>: common should drain and shutdown gracefully */
static int test_fini_without_end(void){
    sink_reset(8, -1);
    set_proc_cfg('U', NULL);
    ASSERT_OK(common_plugin_init(proc_fn, "p7", 8));
    plugin_attach(sink_next);

    ASSERT_OK(plugin_place_work("ab"));
    ASSERT_OK(plugin_place_work("cd"));

    /* Directly finalize without <END> */
    ASSERT_OK(plugin_fini());

    /* All items should have been processed & forwarded before shutdown */
    OK(g_sink.count == 2);
    OK(STREQ(g_sink.lines[0],"AB") && STREQ(g_sink.lines[1],"CD"));
    sink_free();
    return 0;
}

/* 8) plugin_get_name returns configured name after init */
static int test_get_name(void){
    set_proc_cfg('I', NULL);
    ASSERT_OK(common_plugin_init(proc_fn, "my_plugin", 2));
    OK(STREQ(plugin_get_name(), "my_plugin"));
    ASSERT_OK(plugin_place_work("<END>"));
    ASSERT_OK(plugin_wait_finished());
    ASSERT_OK(plugin_fini());
    return 0;
}

/* 9) Downstream returns an error for a specific item -> should not crash */
static int test_downstream_error_logged_and_continue(void){
    sink_reset(8, /*fail_on_index=*/1);
    set_proc_cfg('I', NULL);
    ASSERT_OK(common_plugin_init(proc_fn, "p9", 8));
    plugin_attach(sink_next);

    ASSERT_OK(plugin_place_work("a"));  /* ok */
    ASSERT_OK(plugin_place_work("b"));  /* sink will return "downstream error" */
    ASSERT_OK(plugin_place_work("c"));  /* ok */
    ASSERT_OK(plugin_place_work("<END>"));
    ASSERT_OK(plugin_wait_finished());
    ASSERT_OK(plugin_fini());

    /* We still recorded all three items; error was just logged by plugin_common */
    OK(g_sink.count == 3);
    OK(STREQ(g_sink.lines[0],"a") && STREQ(g_sink.lines[1],"b") && STREQ(g_sink.lines[2],"c"));
    OK(g_sink.saw_end==1);
    sink_free();
    return 0;
}

/* 10) Many items (stress-ish) */
static int test_many_items(void){
    sink_reset(2000, -1);
    set_proc_cfg('I', NULL);
    ASSERT_OK(common_plugin_init(proc_fn, "p10", 64));
    plugin_attach(sink_next);

    const int N=1000;
    char buf[32];
    for(int i=0;i<N;++i){ snprintf(buf,sizeof(buf),"msg%d",i); ASSERT_OK(plugin_place_work(buf)); }
    ASSERT_OK(plugin_place_work("<END>"));
    ASSERT_OK(plugin_wait_finished());
    ASSERT_OK(plugin_fini());

    OK(g_sink.count == N);
    OK(STREQ(g_sink.lines[0],"msg0"));
    OK(STREQ(g_sink.lines[N-1],"msg999"));
    sink_free();
    return 0;
}

/* ------------------------ Runner ------------------------ */
static int run(const char* name, int(*fn)(void)){
    printf("[ RUN  ] %s\n", name);
    int rc = fn();
    if (rc==0) printf("[ PASS ] %s\n", name);
    else       printf("[ FAIL ] %s (rc=%d)\n", name, rc);
    return rc;
}

int main(void){
    int rc=0;
    rc |= run("print_as_last",                    test_print_as_last);
    rc |= run("forward_with_attach",              test_forward_with_attach);
    rc |= run("drop_item",                        test_drop_item);
    rc |= run("double_init_fails",                test_double_init_fails);
    rc |= run("place_before_init_errors",         test_place_before_init_errors);
    rc |= run("wait_then_fini_ok",                test_wait_then_fini_ok);
    rc |= run("fini_without_end",                 test_fini_without_end);
    rc |= run("get_name",                         test_get_name);
    rc |= run("downstream_error_logged_continue", test_downstream_error_logged_and_continue);
    rc |= run("many_items",                       test_many_items);

    printf("\n=== SUMMARY ===\n");
    printf("Assertions run: %d\n", g_runs);
    printf("Tests failed:   %d\n", g_fails);
    printf("Exit code:      %d\n", rc ? 1 : 0);
    return rc ? 1 : 0;
}
