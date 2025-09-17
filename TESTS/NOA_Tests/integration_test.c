// tests/integration_test.c
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

/* API typedefs */
typedef const char* (*plugin_init_fn)(int);
typedef const char* (*plugin_fini_fn)(void);
typedef const char* (*plugin_place_work_fn)(const char*);
typedef void        (*plugin_attach_fn)(const char* (*)(const char*));
typedef const char* (*plugin_wait_finished_fn)(void);
typedef const char* (*plugin_get_name_fn)(void);

/* Loaded plugin wrapper */
typedef struct {
    void* so;
    plugin_init_fn           init;
    plugin_fini_fn           fini;
    plugin_place_work_fn     place;
    plugin_attach_fn         attach;
    plugin_wait_finished_fn  wait;
    plugin_get_name_fn       get_name;
    const char*              name;
} plug_t;

/* tiny strdup replacement (C17-friendly) */
static char* xdup(const char* s){
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (!p) return NULL;
    memcpy(p, s, n);
    return p;
}

/* stdout capture */
typedef struct { int pipe_r, saved_fd, active; } cap_t;
static int cap_begin_stdout(cap_t* c){
    int p[2]; if (pipe(p)!=0) return -1;
    fflush(NULL);
    c->saved_fd = dup(STDOUT_FILENO); if (c->saved_fd<0){ close(p[0]); close(p[1]); return -1; }
    if (dup2(p[1], STDOUT_FILENO)<0){ close(p[0]); close(p[1]); close(c->saved_fd); return -1; }
    close(p[1]); c->pipe_r=p[0]; c->active=1; return 0;
}
static char* cap_end_stdout(cap_t* c){
    if(!c->active){
        char* z = (char*)malloc(1);
        if (!z) return NULL;
        z[0] = '\0';
        return z;
    }
    fflush(NULL);
    (void)dup2(c->saved_fd, STDOUT_FILENO); close(c->saved_fd);
    size_t sz=0, cap=4096; char* buf=(char*)malloc(cap); if(!buf){ close(c->pipe_r); c->active=0; return NULL; }
    for(;;){
        char tmp[4096];
        ssize_t n=read(c->pipe_r,tmp,sizeof(tmp));
        if(n<=0) break;
        if(sz+(size_t)n+1>cap){
            cap=(sz+(size_t)n+1)*2;
            char* nb=(char*)realloc(buf,cap);
            if(!nb){ free(buf); close(c->pipe_r); c->active=0; return NULL; }
            buf=nb;
        }
        memcpy(buf+sz,tmp,(size_t)n);
        sz+=(size_t)n;
    }
    close(c->pipe_r); buf[sz]='\0'; c->active=0; return buf;
}

/* load a plugin .so */
static int load_plugin(const char* path, int qsize, plug_t* out) {
    memset(out, 0, sizeof(*out));
    out->so = dlopen(path, RTLD_NOW);
    if (!out->so) { fprintf(stderr, "dlopen(%s) failed: %s\n", path, dlerror()); return -1; }

    #define GETSYM(field, sym) do{ \
        out->field = (sym##_fn)dlsym(out->so, #sym); \
        if(!out->field){ fprintf(stderr,"dlsym(%s) %s\n", path, #sym); return -1; } \
    }while(0)

    GETSYM(init,              plugin_init);
    GETSYM(fini,              plugin_fini);
    GETSYM(place,             plugin_place_work);
    GETSYM(attach,            plugin_attach);
    GETSYM(wait,              plugin_wait_finished);
    GETSYM(get_name,          plugin_get_name);
    #undef GETSYM

    const char* err = out->init(qsize);
    if (err){ fprintf(stderr,"plugin_init(%s) -> %s\n", path, err); return -1; }
    out->name = out->get_name();
    fprintf(stderr, "[INFO] loaded %s as \"%s\"\n", path, out->name ? out->name : "(null)");
    return 0;
}

static void unload_plugin(plug_t* p){
    if (!p->so) return;
    (void)p->fini();
    dlclose(p->so);
    memset(p,0,sizeof(*p));
}

/* expected transform per plugin name */
static char* apply_one(const char* plugin_name, const char* in){
    if (!plugin_name) return xdup(in);

    if (strcmp(plugin_name, "uppercaser")==0){
        size_t n=strlen(in); char* out=(char*)malloc(n+1); if(!out) return NULL;
        for(size_t i=0;i<n;++i) { out[i]=(char)toupper((unsigned char)in[i]); }
        out[n]='\0'; return out;
    }
    if (strcmp(plugin_name, "rotator")==0){
        size_t n=strlen(in); char* out=(char*)malloc(n+1); if(!out) return NULL;
        if(n==0){ out[0]='\0'; return out; }
        out[0]=in[n-1]; if(n>1) memcpy(out+1,in,n-1); out[n]='\0'; return out;
    }
    if (strcmp(plugin_name, "flipper")==0){
        size_t n=strlen(in); char* out=(char*)malloc(n+1); if(!out) return NULL;
        for(size_t i=0;i<n;++i) { out[i]=in[n-1-i]; }
        out[n]='\0'; return out;
    }
    if (strcmp(plugin_name, "expander")==0){
        size_t n=strlen(in);
        if(n==0){ char* out=(char*)malloc(1); if(!out) return NULL; out[0]='\0'; return out; }
        char* out=(char*)malloc(2*n); if(!out) return NULL;
        for(size_t i=0;i<n;++i){ out[i*2]=in[i]; if(i+1<n) out[i*2+1]=' '; }
        out[2*n-1]='\0'; return out;
    }
    if (strcmp(plugin_name, "logger")==0)     return xdup(in); /* logs to stderr only */
    if (strcmp(plugin_name, "typewriter")==0) return xdup(in); /* throttles only */

    return xdup(in);
}

static char* expected_through_chain(char** names, int n, const char* in){
    char* cur = xdup(in);
    for (int i=0;i<n;++i){
        char* next = apply_one(names[i], cur);
        free(cur);
        cur = next;
        if (!cur) return NULL;
    }
    return cur;
}

int main(int argc, char** argv){
    if (argc < 2){
        fprintf(stderr, "usage: %s <plugin1.so> [plugin2.so ...]\n", argv[0]);
        fprintf(stderr, "example:\n  %s build/uppercaser.so build/expander.so build/typewriter.so\n", argv[0]);
        return 2;
    }

    int n = argc - 1;
    plug_t* plugs = (plug_t*)calloc((size_t)n, sizeof(plug_t));
    if (!plugs) return 2;

    /* load all */
    for (int i=0;i<n;++i){
        if (load_plugin(argv[i+1], /*queue_size*/8, &plugs[i]) != 0){
            for (int j=0;j<i; ++j) unload_plugin(&plugs[j]);
            free(plugs);
            return 2;
        }
    }

    /* chain: plugs[i] -> plugs[i+1] */
    for (int i=0;i<n-1;++i){
        plugs[i].attach(plugs[i+1].place);
    }

    /* names for expected */
    char** names = (char**)calloc((size_t)n, sizeof(char*));
    if (!names){ for(int i=0;i<n;++i) unload_plugin(&plugs[i]); free(plugs); return 2; }
    for (int i=0;i<n;++i) names[i] = (char*)(plugs[i].name ? plugs[i].name : "");

    /* inputs */
    const char* inputs[] = { "", "ab", "Hello", "Hello World!", "xyz123" };
    const int   M = (int)(sizeof(inputs)/sizeof(inputs[0]));

    /* expected (joined with '\n') */
    size_t exp_cap = 1024, exp_len=0;
    char* expected = (char*)malloc(exp_cap); if(!expected){ fprintf(stderr,"oom\n"); return 2; }
    expected[0]='\0';
    for (int i=0;i<M;++i){
        char* t = expected_through_chain(names, n, inputs[i]);
        size_t tl = strlen(t);
        if (exp_len + tl + 2 > exp_cap){ exp_cap = (exp_len+tl+2)*2; expected = (char*)realloc(expected, exp_cap); }
        memcpy(expected+exp_len, t, tl); exp_len += tl;
        expected[exp_len++] = '\n'; expected[exp_len] = '\0';
        free(t);
    }

    /* run and capture stdout of the last plugin */
    cap_t cap = {0}; if (cap_begin_stdout(&cap)!=0){ fprintf(stderr,"capture failed\n"); return 2; }

    for (int i=0;i<M;++i){
        const char* err = plugs[0].place(inputs[i]);
        if (err) { fprintf(stderr,"place_work error: %s\n", err); }
    }
    (void)plugs[0].place("<END>");

    for (int i=0;i<n;++i) (void)plugs[i].wait();
    for (int i=0;i<n;++i) unload_plugin(&plugs[i]);

    char* got = cap_end_stdout(&cap);
    if (!got){ fprintf(stderr,"failed to read captured output\n"); free(expected); free(names); free(plugs); return 2; }

    /* print final output ALWAYS */
    printf("=== FINAL OUTPUT ===\n%s", got);

    /* compare & report */
    if (strcmp(got, expected) != 0){
        fprintf(stderr, "[FAIL] output mismatch\n--- EXPECTED ---\n%s---   GOT   ---\n%s", expected, got);
        free(got); free(expected); free(names); free(plugs);
        return 1;
    }

    printf("[PASS] pipeline ok for %d inputs through %d plugins\n", M, n);
    free(got); free(expected); free(names); free(plugs);
    return 0;
}
