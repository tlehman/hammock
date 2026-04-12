#include "perf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef HAMMOCK_VERSION
#define HAMMOCK_VERSION "0.0.0"
#endif

#define PERF_MAX_LABELS    64
#define PERF_INITIAL_CAP   1024
#define PERF_AMBIENT_CAP   50000

typedef struct {
    char    *label;
    uint64_t *samples;
    size_t    count;
    size_t    capacity;
} PerfLabel;

static PerfMode  g_mode = PERF_OFF;
static char     *g_out_path = NULL;
static FILE     *g_ambient_fp = NULL;
static PerfLabel g_labels[PERF_MAX_LABELS];
static size_t    g_label_count = 0;

/* Lifetime sample count, capped at PERF_AMBIENT_CAP. */
static size_t    g_ambient_total = 0;

uint64_t perf_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

PerfMode perf_get_mode(void) { return g_mode; }

void perf_init(PerfMode mode, const char *out_path) {
    g_mode = mode;
    if (out_path) g_out_path = strdup(out_path);
    if (mode == PERF_AMBIENT && g_out_path) {
        g_ambient_fp = fopen(g_out_path, "w");
        if (!g_ambient_fp) {
            fprintf(stderr, "perf: cannot open %s for writing\n", g_out_path);
            g_mode = PERF_OFF;
            return;
        }
        /* Header line so the file is a valid EDN-lines stream. */
        time_t now = time(NULL);
        struct tm tm;
        gmtime_r(&now, &tm);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
        char host[256] = "unknown";
        gethostname(host, sizeof(host));
        host[sizeof(host)-1] = '\0';
        /* Unbuffered so every sample hits disk immediately, which
         * matters because SIGTERM and other non-graceful exits skip
         * atexit handlers. Log volume is low (one line per dispatched
         * command), so the overhead is acceptable. */
        setvbuf(g_ambient_fp, NULL, _IONBF, 0);
        fprintf(g_ambient_fp,
                ";; hammock perf ambient log v1\n"
                ";; {:version \"%s\" :host \"%s\" :mode :ambient :timestamp \"%s\"}\n",
                HAMMOCK_VERSION, host, ts);
    }
}

void perf_shutdown(void) {
    if (g_mode == PERF_AMBIENT) {
        if (g_ambient_fp) {
            fflush(g_ambient_fp);
            fclose(g_ambient_fp);
            g_ambient_fp = NULL;
        }
    }
    for (size_t i = 0; i < g_label_count; i++) {
        free(g_labels[i].label);
        free(g_labels[i].samples);
        g_labels[i].label = NULL;
        g_labels[i].samples = NULL;
        g_labels[i].count = 0;
        g_labels[i].capacity = 0;
    }
    g_label_count = 0;
    free(g_out_path);
    g_out_path = NULL;
    g_mode = PERF_OFF;
}

static PerfLabel *get_or_create_label(const char *label) {
    for (size_t i = 0; i < g_label_count; i++) {
        if (strcmp(g_labels[i].label, label) == 0)
            return &g_labels[i];
    }
    if (g_label_count >= PERF_MAX_LABELS) return NULL;
    PerfLabel *l = &g_labels[g_label_count++];
    l->label = strdup(label);
    l->capacity = PERF_INITIAL_CAP;
    l->samples = malloc(l->capacity * sizeof(uint64_t));
    l->count = 0;
    if (!l->samples) {
        free(l->label);
        g_label_count--;
        return NULL;
    }
    return l;
}

static void ambient_append_line(const char *label, uint64_t ns) {
    if (!g_ambient_fp) return;
    if (g_ambient_total >= PERF_AMBIENT_CAP) return;  /* simplified cap: drop new
                                                         samples past the limit. */
    fprintf(g_ambient_fp, "{:label \"%s\" :ns %llu}\n",
            label, (unsigned long long)ns);
    g_ambient_total++;
}

void perf_record(const char *label, uint64_t ns) {
    if (g_mode == PERF_OFF) return;
    if (g_mode == PERF_AMBIENT) {
        ambient_append_line(label, ns);
        return;
    }
    /* PERF_BENCH */
    PerfLabel *l = get_or_create_label(label);
    if (!l) return;
    if (l->count >= l->capacity) {
        size_t new_cap = l->capacity * 2;
        uint64_t *new_samples = realloc(l->samples, new_cap * sizeof(uint64_t));
        if (!new_samples) return;
        l->samples = new_samples;
        l->capacity = new_cap;
    }
    l->samples[l->count++] = ns;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static uint64_t pct(const uint64_t *sorted, size_t n, double p) {
    if (n == 0) return 0;
    size_t idx = (size_t)(p * (double)(n - 1));
    if (idx >= n) idx = n - 1;
    return sorted[idx];
}

void perf_write_bench_edn(const char *out_path) {
    FILE *fp = stdout;
    int  close_fp = 0;
    if (out_path && out_path[0] != '-' && out_path[0] != '\0') {
        fp = fopen(out_path, "w");
        if (!fp) {
            fprintf(stderr, "perf: cannot open %s for writing\n", out_path);
            return;
        }
        close_fp = 1;
    }

    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
    char host[256] = "unknown";
    gethostname(host, sizeof(host));
    host[sizeof(host)-1] = '\0';

    fprintf(fp, "{:version \"%s\"\n", HAMMOCK_VERSION);
    fprintf(fp, " :host \"%s\"\n", host);
    fprintf(fp, " :mode :bench\n");
    fprintf(fp, " :timestamp \"%s\"\n", ts);
    fprintf(fp, " :samples [\n");
    for (size_t i = 0; i < g_label_count; i++) {
        PerfLabel *l = &g_labels[i];
        if (l->count == 0) continue;
        qsort(l->samples, l->count, sizeof(uint64_t), cmp_u64);
        uint64_t mn  = l->samples[0];
        uint64_t mx  = l->samples[l->count - 1];
        uint64_t p50 = pct(l->samples, l->count, 0.50);
        uint64_t p90 = pct(l->samples, l->count, 0.90);
        uint64_t p99 = pct(l->samples, l->count, 0.99);
        uint64_t sum = 0;
        for (size_t j = 0; j < l->count; j++) sum += l->samples[j];
        uint64_t mean = sum / l->count;
        fprintf(fp,
                "  {:label \"%s\" :n %zu :min-ns %llu :p50-ns %llu"
                " :p90-ns %llu :p99-ns %llu :max-ns %llu :mean-ns %llu}\n",
                l->label, l->count,
                (unsigned long long)mn, (unsigned long long)p50,
                (unsigned long long)p90, (unsigned long long)p99,
                (unsigned long long)mx, (unsigned long long)mean);
    }
    fprintf(fp, " ]}\n");
    if (close_fp) fclose(fp);
}
