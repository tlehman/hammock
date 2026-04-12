#ifndef PERF_H
#define PERF_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    PERF_OFF = 0,
    PERF_BENCH,
    PERF_AMBIENT
} PerfMode;

/* Initialize the perf subsystem.
 *   PERF_OFF      - no-op; all probes short-circuit on one branch.
 *   PERF_BENCH    - collect samples into per-label arrays for aggregation.
 *   PERF_AMBIENT  - stream one EDN map per sample to out_path.
 * out_path is copied. For BENCH it is used later by perf_write_bench_edn;
 * for AMBIENT it is opened immediately and streamed to. */
void     perf_init(PerfMode mode, const char *out_path);

/* Flush pending ambient samples, close files, free per-label buffers. */
void     perf_shutdown(void);

/* Monotonic nanosecond clock. Safe to call even when perf is off. */
uint64_t perf_now_ns(void);

/* Record a single sample. One-branch no-op when PERF_OFF. */
void     perf_record(const char *label, uint64_t ns);

/* Current mode (used by callers that want to skip sample-prep work). */
PerfMode perf_get_mode(void);

/* BENCH mode: write the aggregate EDN map to out_path (NULL or "-" = stdout).
 * Emits {:version :host :mode :bench :timestamp :samples [{...}]}. */
void     perf_write_bench_edn(const char *out_path);

#endif /* PERF_H */
