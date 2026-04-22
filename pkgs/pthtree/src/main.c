#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

/*
 * pthread_tree.c — LD_PRELOAD interposer that prints:
 *   • the pthread creation hierarchy
 *   • the shared object that holds each thread's entry point
 *   • per‑thread CPU time (CLOCK_THREAD_CPUTIME_ID)
 *     in seconds and as %% of total run‑time
 *
 * Build:
 *   gcc -shared -fPIC -o libpthtree.so pthread_tree.c -ldl -pthread
 *
 * Usage via wrapper (see pthtree script):
 *   ./pthtree [-v] your_command [args]
 *
 *   -v : show full paths instead of basenames.
 */

/* -----------------------------  helpers  --------------------------- */
static double ts_to_sec(const struct timespec *ts)
{
    return (double)ts->tv_sec + (double)ts->tv_nsec / 1e9;
}

/* -------------------------  internal data  ------------------------- */

typedef struct {
    pid_t  parent;
    pid_t  child;
    char  *lib;         /* strdup‑ed library name or path           */
    double cpu_sec;     /* ‑1 until recorded                        */
    int    recorded;    /* 0 → not yet, 1 → recorded                */
    double start;
} Edge;

static Edge  *edges     = NULL;
static size_t edge_len  = 0;
static size_t edge_cap  = 0;

static pthread_mutex_t edge_lock = PTHREAD_MUTEX_INITIALIZER;

static pid_t   root_tid       = -1;   /* main thread TID                     */
static double  root_cpu_sec   = -1.0; /* CPU time for root                   */
static int     root_recorded  = 0;
static int     verbose_paths  = 0;
static int     delay_dump_cfg = 0;
static int     delay_dump     = 0;

static struct timespec wall_start, wall_end;

/* ----------------------  edge / thread storage  -------------------- */

static void record_edge(pid_t parent, pid_t child, char *lib)
{
    double start;
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        start = ts_to_sec(&ts);
    } else {
        fprintf(stderr, "clock_gettime error\n");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&edge_lock);
    if (edge_len == edge_cap) {
        edge_cap = edge_cap ? edge_cap * 2 : 64;
        edges = realloc(edges, edge_cap * sizeof(Edge));
        if (!edges) {
            perror("realloc");
            _exit(1);
        }
    }
    edges[edge_len++] = (Edge){ parent, child, lib, -1.0, 0, start};
    pthread_mutex_unlock(&edge_lock);
}

static void store_cpu_time(pid_t tid, double sec)
{
    pthread_mutex_lock(&edge_lock);
    if (tid == root_tid) {
        if (!root_recorded) {
            root_cpu_sec = sec;
            root_recorded = 1;
        }
        pthread_mutex_unlock(&edge_lock);
        return;
    }

    for (size_t i = 0; i < edge_len; ++i) {
        if (edges[i].child == tid) {
            if (!edges[i].recorded) {
                edges[i].cpu_sec  = sec;
                edges[i].recorded = 1;
            }
            break;
        }
    }
    pthread_mutex_unlock(&edge_lock);
}

// needs taking the edge_lock!
static size_t count_unrecorded(void)
{
    size_t cnt = 0;
    for (size_t i = 0; i < edge_len; ++i) {
        if (!edges[i].recorded) {
            cnt++;
        }
    }
    return cnt;
}

static void cleanup(void)
{
    for (size_t i = 0; i < edge_len; ++i)
        free(edges[i].lib);
    free(edges);
}

/* ---------------------------  lookups  ----------------------------- */
static Edge *edge_by_child(pid_t tid)
{
    for (size_t i = 0; i < edge_len; ++i)
        if (edges[i].child == tid)
            return &edges[i];
    return NULL;
}

/* ---------------------------  printing  ---------------------------- */

/* child counters */
static size_t count_immediate(pid_t parent)
{
    size_t cnt = 0;
    for (size_t i = 0; i < edge_len; ++i)
        if (edges[i].parent == parent)
            ++cnt;
    return cnt;
}

static size_t count_descendants(pid_t parent)
{
    size_t total = 0;
    for (size_t i = 0; i < edge_len; ++i) {
        if (edges[i].parent == parent) {
            ++total;
            total += count_descendants(edges[i].child);
        }
    }
    return total;
}

static double total_wall_sec = 0.0;

static void print_subtree(pid_t parent, const char *prefix, size_t lvl)
{
    size_t nchild = count_immediate(parent);
    if (nchild == 0)
        return;

    size_t seen = 0;
    for (size_t i = 0; i < edge_len; ++i) {
        if (edges[i].parent != parent)
            continue;
        ++seen;
        int last = (seen == nchild);

        char rtime[256];
        if (edges[i].recorded) {
            double cpu  = edges[i].cpu_sec;
            double total_pth_sec = ts_to_sec(&wall_end) - edges[i].start;
            double perc   = total_wall_sec > 0 ? (cpu * 100.0 / total_wall_sec) : 0.0;
            double percth = total_pth_sec  > 0 ? (cpu * 100.0 / total_pth_sec)  : 0.0;
            snprintf(rtime, sizeof(rtime), "%.2fs (%.2f%%, %.2f%%)", cpu, percth, perc);
        } else {
            snprintf(rtime, sizeof(rtime), "? (?%%, ?%%)");
        }

        size_t child_cnt = count_immediate(edges[i].child);
        size_t desc_cnt  = count_descendants(edges[i].child);

        fprintf(stderr, "%s%s── %d (%s) %zulvl %s [%zu/%zu]\n",
                prefix, last ? "└" : "├", (int)edges[i].child,
                edges[i].lib ? edges[i].lib : "unknown",
                lvl, rtime, child_cnt, desc_cnt);

        char next_pref[256];
        snprintf(next_pref, sizeof(next_pref), "%s%s   ", prefix, last ? " " : "│");
        print_subtree(edges[i].child, next_pref, lvl+1);
    }
}

static void dump_tree(void)
{
    size_t root_child = count_immediate(root_tid);
    size_t root_desc  = count_descendants(root_tid);
    double root_pcpu = total_wall_sec > 0 ? (root_cpu_sec * 100.0 / total_wall_sec) : 0.0;
    size_t lvl = 0;

    fprintf(stderr, "%d (main) %zulvl %.2fs (%.2f%%, %.2f%%) [%zu/%zu]\n", (int)root_tid, lvl,
            root_cpu_sec, root_pcpu, root_pcpu, root_child, root_desc);
    print_subtree(root_tid, "", lvl+1);
}

/* ---------------------  original function ptrs  -------------------- */

typedef int  (*orig_pthread_create_f)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
typedef void (*orig_pthread_exit_f)(void *);

static orig_pthread_create_f real_pthread_create = NULL;
static orig_pthread_exit_f   real_pthread_exit   = NULL;

/* ------------------  utils: library name lookup  ------------------- */
static char *lib_from_addr(void *addr)
{
    Dl_info info;
    if (dladdr(addr, &info) && info.dli_fname) {
        if (verbose_paths)
            return strdup(info.dli_fname);
        const char *base = strrchr(info.dli_fname, '/');
        return strdup(base ? base + 1 : info.dli_fname);
    }
    return strdup("unknown");
}

/* -----------------------  thread trampoline  ----------------------- */
struct start_info {
    void *(*user_start)(void *);
    void *user_arg;
    pid_t parent_tid;
    char *lib;
};

static void *start_trampoline(void *arg)
{
    struct start_info *info = (struct start_info *)arg;
    pid_t self_tid = (pid_t)syscall(SYS_gettid);

    /* first, remember the hierarchy */
    record_edge(info->parent_tid, self_tid, info->lib);

    /* run user's start routine */
    void *(*usr)(void *) = info->user_start;
    void *usr_arg        = info->user_arg;
    free(info); /* free container; 'lib' now owned by `record_edge` */

    void *ret = usr(usr_arg);

    /* on normal return, capture CPU time */
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
        store_cpu_time(self_tid, ts_to_sec(&ts));

    pthread_mutex_lock(&edge_lock);
    if (delay_dump && !count_unrecorded()) {
        // we are the last thread, time for a dump!
        dump_tree();
        cleanup();
    }
    pthread_mutex_unlock(&edge_lock);

    return ret;
}

/* ----------------------  interposed symbol: pthread_create  -------- */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
    if (!real_pthread_create) {
        real_pthread_create = (orig_pthread_create_f)dlsym(RTLD_NEXT, "pthread_create");
        if (!real_pthread_create) {
            fprintf(stderr, "[pthtree] failed to resolve original pthread_create\n");
            _exit(1);
        }
    }

    struct start_info *info = malloc(sizeof(*info));
    if (!info) return ENOMEM;
    info->user_start = start_routine;
    info->user_arg   = arg;
    info->parent_tid = (pid_t)syscall(SYS_gettid);
    info->lib        = lib_from_addr((void *)start_routine);

    return real_pthread_create(thread, attr, start_trampoline, info);
}

/* ----------------------  interposed symbol: pthread_exit ----------- */
void pthread_exit(void *retval)
{
    if (!real_pthread_exit) {
        real_pthread_exit = (orig_pthread_exit_f)dlsym(RTLD_NEXT, "pthread_exit");
        if (!real_pthread_exit) {
            fprintf(stderr, "[pthtree] failed to resolve original pthread_exit\n");
            _exit(1);
        }
    }

    /* record CPU time before true exit */
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
        pid_t tid = (pid_t)syscall(SYS_gettid);
        store_cpu_time(tid, ts_to_sec(&ts));
    }

    real_pthread_exit(retval);
    __builtin_unreachable();
}

/* ------------------------  lifecycle hooks  ------------------------ */
__attribute__((constructor))
static void pthtree_ctor(void)
{
    /* root info */
    root_tid = (pid_t)syscall(SYS_gettid);

    /* verbose? */
    const char *v = getenv("PTHTREE_VERBOSE");
    if (v && *v && strcmp(v, "0") != 0)
        verbose_paths = 1;

    /* delay dump */
    v = getenv("PTHTREE_DELAY");
    if (v && *v && strcmp(v, "0") != 0)
        delay_dump_cfg = 1;

    /* wall clock start */
    clock_gettime(CLOCK_REALTIME, &wall_start);
}

__attribute__((destructor))
static void pthtree_dtor(void)
{
    /* wall clock end and total */
    clock_gettime(CLOCK_REALTIME, &wall_end);
    total_wall_sec = ts_to_sec(&wall_end) - ts_to_sec(&wall_start);

    /* root CPU if not done yet */
    if (!root_recorded) {
        struct timespec ts;
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
            root_cpu_sec   = ts_to_sec(&ts);
            root_recorded  = 1;
        }
    }

    pthread_mutex_lock(&edge_lock);
    // If some threads did not exited yet, expect another destructor to finish
    // them before plotting the tree.
    if (delay_dump_cfg && count_unrecorded())
        delay_dump = 1;

    if (!delay_dump) {
        dump_tree();
        cleanup();
    }
    pthread_mutex_unlock(&edge_lock);
}

