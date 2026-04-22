/*
 * loadwrap.c
 *
 *  * Fork‑exec the command given on the command line.
 *  * Sample the kernel's 1‑minute load average (sysinfo.loads[0]) once a
 *    second together with the uptime (sysinfo.uptime). Both values come
 *    from a single sysinfo() call so the snapshot is atomic.
 *  * Ignore the first 60 s of runtime: samples are stored only after the
 *    first minute has elapsed since the wrapper started.
 *  * If the wrapped command terminates before 60 s, we exit immediately and
 *    report -1 as the mean load.
 *  * Samples are kept in a pre‑faulted buffer sized for 30 min
 *    (1 sample/s -> 1 800 entries). After the child terminates (or the
 *    buffer fills) the samples are written to "loadwrap.out".
 *  * The program now prints mean, standard deviation, and 25th/50th/75th
 *    percentiles of the collected load values.
 *  * If the child exits with a non‑zero status or by signal, this wrapper
 *    returns the same exit status (128+SIG for signals).
 *
 *  Build :  gcc -O2 -Wall -Wextra -o loadwrap loadwrap.c -lm
 *  Usage :  ./loadwrap <command> [arg ...]
 *
 *  Output file:  loadwrap.out  - one line per sample:
 *      "<uptime‑seconds> <load>\n"
 *      (created only if at least one sample was stored)
 */

#define _POSIX_C_SOURCE 200809L
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#define SAMPLING_INTERVAL_S    1  /* 1 s */
#define MAX_MINUTES           30
#define MAX_SAMPLES           (MAX_MINUTES * 60)  /* 1800 */

struct sample {
    unsigned long ts;   /* uptime - seconds since boot */
    double        load; /* 1‑min load avg */
};

static struct sample buffer[MAX_SAMPLES];
static unsigned long start_time;

/* Prefault the buffer so no major page faults occur during timing */
static void prefault_buffer(void)
{
    size_t pagesize = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char *p = (unsigned char *)buffer;
    for (size_t i = 0; i < sizeof(buffer); i += pagesize)
        p[i] = 0;
}

/* Collect one sample - returns 0 on success, -1 on error; fills *uptime */
//static int collect_sample(unsigned long *uptime, double *load)
//{
//    struct sysinfo si;
//    if (sysinfo(&si) == -1)
//        return -1;
//
//    *uptime = (unsigned long)si.uptime;
//    *load   = si.loads[0] / 65536.0;
//    return 0;
//}

static int read_procs_running(int *out)
{
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1;
    char line[256];
    int found = -1;
    while (fgets(line, sizeof line, fp)) {
        if (strncmp(line, "procs_running", 13) == 0) {
            found = (int)strtol(line + 13, NULL, 10);
            break;
        }
    }
    fclose(fp);
    if (found < 0) return -1;
    *out = found;
    return 0;
}

static unsigned long get_seconds(void)
{
	return (unsigned long) time(NULL);
}

/* Collect one sample – returns 0 on success, -1 on error */
static void collect_sample(unsigned long *uptime, double *load)
{
    int running = 0;
    *uptime = get_seconds() - start_time;
    if (read_procs_running(&running) == -1) {
       perror("read_procs_running:");
       exit(1);
    }
    *load = (double)running;
}

static void write_file(const char *fname, size_t nsamples)
{
    FILE *fp = fopen(fname, "w");
    if (!fp) {
        perror("fopen");
        return;
    }
    for (size_t i = 0; i < nsamples; ++i)
        fprintf(fp, "%lu %.0f\n", buffer[i].ts, buffer[i].load);
    fclose(fp);
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

/* Linear interpolation percentile */
static double percentile(const double *arr, size_t n, double p)
{
    if (n == 0) return NAN;
    double pos = p * (double)(n - 1);
    size_t lo = (size_t)pos;
    size_t hi = (lo + 1 < n) ? lo + 1 : lo;
    double frac = pos - (double)lo;
    return arr[lo] * (1.0 - frac) + arr[hi] * frac;
}

static void pr_summary(
    size_t idx,
    double mean, double stddev,
    double q25, double q50, double q75, double q90,
    const char *outfile
) {
    printf("samples: %zu\n", idx);
    printf("mean:    %.3f\n", mean);
    printf("std:     %.3f\n", stddev);
    printf("25th:    %.3f\n", q25);
    printf("median:  %.3f\n", q50);
    printf("75th:    %.3f\n", q75);
    printf("90th:    %.3f\n", q90);
    printf("data:    %s\n", outfile);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args ...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    prefault_buffer();

    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (child == 0) {
        /* Child - replace with user command */
        execvp(argv[1], &argv[1]);
        perror("execvp");
        _exit(127);            /* only reached on exec error */
    }

    size_t idx = 0;
    int child_status = 0;
    bool child_done  = false;

    struct timespec req = { .tv_sec = SAMPLING_INTERVAL_S, .tv_nsec = 0};

    start_time = get_seconds();

    while (1) {
        /* Check if child has terminated */
        pid_t r = waitpid(child, &child_status, WNOHANG);
        if (r == -1) {
            perror("waitpid");
            kill(child, SIGKILL);
            child_done = true;
        } else if (r > 0) {
            child_done = true;          /* normal termination */
        }

        /* Gather one snapshot every second */
        unsigned long uptime_now;
        double load_now;
        collect_sample(&uptime_now, &load_now);

        if (idx < MAX_SAMPLES) {
            buffer[idx].ts   = uptime_now;
            buffer[idx].load = load_now;
            ++idx;
        }

        if (child_done)
            break;                      /* exit the sampling loop */

        nanosleep(&req, NULL);          /* maintain cadence */
    }

    /* Handle too-short runs (< 60 s) */
    if (idx == 0) {
        pr_summary(0, 0, 0, 0, 0, 0, 0, "");
        /* propagate child error if any, else success */
        if (WIFEXITED(child_status) && WEXITSTATUS(child_status) != 0)
            return WEXITSTATUS(child_status);
        if (WIFSIGNALED(child_status))
            return 128 + WTERMSIG(child_status);
        return 0;
    }

    /* Write samples to disk */
    const char *outfile = "loadwrap.out";
    write_file(outfile, idx);

    /* Allocate array of loads for statistics */
    double *loads = malloc(idx * sizeof(double));
    if (!loads) {
        perror("malloc");
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < idx; ++i)
        loads[i] = buffer[i].load;

    /* Calculate mean, variance */
    double sum = 0.0, sumsq = 0.0;
    for (size_t i = 0; i < idx; ++i) {
        sum   += loads[i];
        sumsq += loads[i] * loads[i];
    }
    double mean = sum / idx;
    double variance = (sumsq / idx) - (mean * mean);
    double stddev = (variance > 0.0) ? sqrt(variance) : 0.0;

    /* Quantiles: sort then interpolate */
    qsort(loads, idx, sizeof(double), cmp_double);
    double q25 = percentile(loads, idx, 0.25);
    double q50 = percentile(loads, idx, 0.50); /* median */
    double q75 = percentile(loads, idx, 0.75);
    double q90 = percentile(loads, idx, 0.90);

    pr_summary(idx, mean, stddev, q25, q50, q75, q90, outfile);

    free(loads);

    /* Propagate child status if it indicates an error */
    if (WIFEXITED(child_status)) {
        int ec = WEXITSTATUS(child_status);
        if (ec != 0)
            return ec;
    } else if (WIFSIGNALED(child_status)) {
        return 128 + WTERMSIG(child_status);
    }

    return 0;   /* child succeeded */
}

