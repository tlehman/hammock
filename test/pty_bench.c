/* Hammock perf: PTY-driven external benchmark.
 *
 * Spawns ./hammock under a pseudo-terminal, feeds it a scripted sequence
 * of keystrokes, and measures response time by the "idle window" method:
 * after each `measure` action, write the keystrokes, then read output
 * until no bytes have arrived for PTY_IDLE_MS milliseconds, and record
 * the wall-clock delta. Writes one EDN sample-aggregate file into
 * perf/runs/ matching the shape produced by src/perf.c.
 *
 * Build via `make pty-bench`. Run via `make perf-pty` or directly:
 *   ./build/pty_bench perf/scripts/pty-smoke.txt
 *
 * The script format is line-oriented. Blank lines and `#` comments are
 * skipped. Directives:
 *
 *   wait-for "pattern"
 *     Wait until `pattern` appears in the cumulative output buffer or
 *     until the global timeout fires. Not measured.
 *
 *   send "key-tokens"
 *     Write a sequence of space-separated key tokens to the child.
 *     Tokens: `C-<x>` (control char), `RET` (0x0d), `TAB` (0x09),
 *     `ESC` (0x1b), `SPC` (0x20), or any other token written
 *     literally byte-by-byte. Not measured.
 *
 *   measure "label" send "key-tokens" [times N]
 *     Same as `send`, but times the exchange. With `times N` the
 *     tokens are repeated N times inside a single measurement. Records
 *     a sample under `label`.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#  include <util.h>
#elif defined(__linux__)
#  include <pty.h>
#else
#  include <pty.h>
#endif

#define PTY_IDLE_MS      5
#define PTY_TIMEOUT_SEC  10
#define MAX_SAMPLES      128

typedef struct {
    char    label[64];
    uint64_t ns;
} Sample;

static Sample samples[MAX_SAMPLES];
static size_t sample_count = 0;

static char    scrollback[1 << 16];
static size_t  scrollback_len = 0;

static int  child_fd  = -1;
static pid_t child_pid = -1;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void append_scrollback(const char *buf, size_t len) {
    if (scrollback_len + len >= sizeof(scrollback)) {
        /* Shift back half to keep recent output around. */
        size_t keep = sizeof(scrollback) / 2;
        memmove(scrollback, scrollback + scrollback_len - keep, keep);
        scrollback_len = keep;
    }
    memcpy(scrollback + scrollback_len, buf, len);
    scrollback_len += len;
    scrollback[scrollback_len] = '\0';
}

/* Read available bytes from child into scrollback. Returns bytes read
 * during this call (0 if nothing was available). */
static int drain_child(int timeout_ms) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(child_fd, &rfds);
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };
    int r = select(child_fd + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0) return 0;
    char buf[4096];
    ssize_t n = read(child_fd, buf, sizeof(buf));
    if (n <= 0) return 0;
    append_scrollback(buf, (size_t)n);
    return (int)n;
}

static int wait_for_pattern(const char *pat) {
    size_t pat_len = strlen(pat);
    uint64_t deadline = now_ns() + (uint64_t)PTY_TIMEOUT_SEC * 1000000000ULL;
    while (now_ns() < deadline) {
        drain_child(50);
        if (scrollback_len >= pat_len &&
            memmem(scrollback, scrollback_len, pat, pat_len) != NULL)
            return 1;
    }
    fprintf(stderr, "pty_bench: timed out waiting for %s\n", pat);
    return 0;
}

/* Translate a single token into one or more bytes and write them. */
static void write_token(const char *tok) {
    size_t tl = strlen(tok);
    if (tl == 3 && tok[0] == 'C' && tok[1] == '-') {
        char c = tok[2];
        if (c >= 'a' && c <= 'z') c -= 'a' - 1;
        else if (c >= 'A' && c <= 'Z') c -= 'A' - 1;
        else if (c == ' ') c = 0;
        write(child_fd, &c, 1);
        return;
    }
    if (strcmp(tok, "RET") == 0) { write(child_fd, "\r", 1); return; }
    if (strcmp(tok, "TAB") == 0) { write(child_fd, "\t", 1); return; }
    if (strcmp(tok, "ESC") == 0) { write(child_fd, "\x1b", 1); return; }
    if (strcmp(tok, "SPC") == 0) { write(child_fd, " ", 1); return; }
    /* Literal token: write bytes. */
    write(child_fd, tok, tl);
}

/* Write a space-separated key string (e.g. "C-x C-f foo.txt RET"). */
static void send_keys(const char *keys) {
    char *copy = strdup(keys);
    char *save = NULL;
    for (char *t = strtok_r(copy, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
        write_token(t);
    }
    free(copy);
}

static uint64_t measure_keys(const char *keys, int repeat) {
    uint64_t t0 = now_ns();
    for (int i = 0; i < repeat; i++) send_keys(keys);
    /* Drain until idle window. */
    while (drain_child(PTY_IDLE_MS) > 0) { /* keep draining */ }
    return now_ns() - t0;
}

static void record_sample(const char *label, uint64_t ns) {
    if (sample_count >= MAX_SAMPLES) return;
    snprintf(samples[sample_count].label, sizeof(samples[0].label),
             "%s", label);
    samples[sample_count].ns = ns;
    sample_count++;
}

/* Minimal quoted-string extractor. Copies contents between the first
 * and second '"' into out. Returns pointer just past the closing quote,
 * or NULL on failure. */
static const char *read_quoted(const char *s, char *out, size_t out_sz) {
    while (*s && *s != '"') s++;
    if (*s != '"') return NULL;
    s++;
    size_t i = 0;
    while (*s && *s != '"') {
        if (i + 1 < out_sz) out[i++] = *s;
        s++;
    }
    out[i] = '\0';
    if (*s == '"') s++;
    return s;
}

static void process_line(char *line) {
    /* Trim leading whitespace */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || *line == '\0' || *line == '\n') return;

    if (strncmp(line, "wait-for", 8) == 0) {
        char pat[256] = {0};
        read_quoted(line + 8, pat, sizeof(pat));
        wait_for_pattern(pat);
    } else if (strncmp(line, "send", 4) == 0 &&
               strncmp(line, "send", 4) == 0 &&
               (line[4] == ' ' || line[4] == '\t')) {
        char keys[512] = {0};
        read_quoted(line + 4, keys, sizeof(keys));
        send_keys(keys);
    } else if (strncmp(line, "measure", 7) == 0) {
        char label[64] = {0};
        char keys[512] = {0};
        int  repeat = 1;
        const char *p = read_quoted(line + 7, label, sizeof(label));
        if (!p) return;
        /* Expect: send "keys" [times N] */
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "send", 4) != 0) return;
        p += 4;
        p = read_quoted(p, keys, sizeof(keys));
        if (!p) return;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "times", 5) == 0) {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            repeat = atoi(p);
            if (repeat < 1) repeat = 1;
        }
        uint64_t ns = measure_keys(keys, repeat);
        record_sample(label, ns);
        fprintf(stderr, "  measured %-20s (%d reps)  %8.3f ms\n",
                label, repeat, ns / 1e6);
    }
}

static void write_edn_output(const char *out_path) {
    FILE *fp = fopen(out_path, "w");
    if (!fp) {
        fprintf(stderr, "pty_bench: cannot write %s\n", out_path);
        return;
    }
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
    char host[128] = "unknown";
    gethostname(host, sizeof(host));
    host[sizeof(host)-1] = '\0';
    for (char *p = host; *p; p++) if (*p == '.') { *p = '\0'; break; }

    fprintf(fp, "{:version \"pty\"\n");
    fprintf(fp, " :host \"%s\"\n", host);
    fprintf(fp, " :mode :pty\n");
    fprintf(fp, " :timestamp \"%s\"\n", ts);
    fprintf(fp, " :samples [\n");
    for (size_t i = 0; i < sample_count; i++) {
        fprintf(fp,
                "  {:label \"%s\" :n 1 :min-ns %llu :p50-ns %llu"
                " :p90-ns %llu :p99-ns %llu :max-ns %llu :mean-ns %llu}\n",
                samples[i].label,
                (unsigned long long)samples[i].ns,
                (unsigned long long)samples[i].ns,
                (unsigned long long)samples[i].ns,
                (unsigned long long)samples[i].ns,
                (unsigned long long)samples[i].ns,
                (unsigned long long)samples[i].ns);
    }
    fprintf(fp, " ]}\n");
    fclose(fp);
    fprintf(stderr, "pty_bench: wrote %s\n", out_path);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s script.txt [hammock-path]\n", argv[0]);
        return 2;
    }
    const char *script_path  = argv[1];
    const char *hammock_path = (argc >= 3) ? argv[2] : "./hammock";

    /* Fork under a pty. */
    struct winsize ws = { .ws_row = 24, .ws_col = 80, .ws_xpixel = 0, .ws_ypixel = 0 };
    struct termios tios;
    memset(&tios, 0, sizeof(tios));
    tios.c_iflag  = ICRNL | IXON;
    tios.c_oflag  = OPOST | ONLCR;
    tios.c_cflag  = CS8 | CREAD | CLOCAL;
    tios.c_lflag  = 0;
    cfsetispeed(&tios, B38400);
    cfsetospeed(&tios, B38400);

    child_pid = forkpty(&child_fd, NULL, &tios, &ws);
    if (child_pid < 0) {
        perror("forkpty");
        return 1;
    }
    if (child_pid == 0) {
        /* Child */
        setenv("TERM", "xterm-256color", 1);
        setenv("LANG", "en_US.UTF-8", 1);
        /* Unset HAMMOCK_PERF so the child's own ambient logging
         * doesn't pollute the measurement. */
        unsetenv("HAMMOCK_PERF");
        execl(hammock_path, hammock_path, (char *)NULL);
        perror("execl");
        _exit(127);
    }

    /* Parent: process script. */
    FILE *sp = fopen(script_path, "r");
    if (!sp) {
        fprintf(stderr, "pty_bench: cannot open %s\n", script_path);
        kill(child_pid, SIGKILL);
        return 1;
    }
    char line[1024];
    while (fgets(line, sizeof(line), sp)) {
        process_line(line);
    }
    fclose(sp);

    /* Let the child shut down cleanly if it hasn't already. */
    int status = 0;
    for (int i = 0; i < 50; i++) {
        pid_t w = waitpid(child_pid, &status, WNOHANG);
        if (w == child_pid) break;
        struct timespec ts = { 0, 10000000 };
        nanosleep(&ts, NULL);
    }
    kill(child_pid, SIGTERM);
    close(child_fd);

    /* Write output EDN. */
    mkdir("perf", 0755);
    mkdir("perf/runs", 0755);
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H-%M-%SZ", &tm);
    char host[128] = "unknown";
    gethostname(host, sizeof(host));
    host[sizeof(host)-1] = '\0';
    for (char *p = host; *p; p++) if (*p == '.') { *p = '\0'; break; }
    char out_path[512];
    snprintf(out_path, sizeof(out_path),
             "perf/runs/%s-%s-pty.edn", ts, host);
    write_edn_output(out_path);
    return 0;
}
