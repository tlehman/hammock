#include "git.h"
#include "command.h"
#include "window.h"
#include "mode.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Run a git command and return output (caller frees) */
static char *git_run(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    size_t cap = 4096;
    size_t len = 0;
    char *buf = hmalloc(cap);

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        size_t llen = strlen(line);
        if (len + llen >= cap) {
            cap *= 2;
            buf = hrealloc(buf, cap);
        }
        memcpy(buf + len, line, llen);
        len += llen;
    }
    buf[len] = '\0';
    pclose(fp);
    return buf;
}

GitStatus git_parse_status(void) {
    GitStatus status = {.count = 0};

    /* Get branch */
    char *branch = git_run("git branch --show-current 2>/dev/null");
    if (branch) {
        /* Trim newline */
        size_t blen = strlen(branch);
        if (blen > 0 && branch[blen - 1] == '\n') branch[blen - 1] = '\0';
        strncpy(status.branch, branch, sizeof(status.branch) - 1);
        free(branch);
    }

    /* Get status */
    char *output = git_run("git status --porcelain=v1 2>/dev/null");
    if (!output) return status;

    char *line = output;
    while (*line && status.count < MAX_GIT_FILES) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        if (strlen(line) >= 4) {
            status.files[status.count].index_status = line[0];
            status.files[status.count].work_status = line[1];
            strncpy(status.files[status.count].path, line + 3,
                    sizeof(status.files[status.count].path) - 1);
            status.count++;
        }

        if (eol)
            line = eol + 1;
        else
            break;
    }

    free(output);
    return status;
}

char *git_format_status(GitStatus *status) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = hmalloc(cap);

    #define APPEND_FMT(...) do { \
        int n = snprintf(buf + len, cap - len, __VA_ARGS__); \
        if (n < 0) break; \
        if (len + (size_t)n >= cap) { cap *= 2; buf = hrealloc(buf, cap); \
            snprintf(buf + len, cap - len, __VA_ARGS__); } \
        len += (size_t)n; \
    } while(0)

    APPEND_FMT("Head:     %s\n\n", status->branch[0] ? status->branch : "(detached)");

    /* Staged changes */
    bool has_staged = false;
    for (int i = 0; i < status->count; i++) {
        if (status->files[i].index_status != ' ' && status->files[i].index_status != '?') {
            if (!has_staged) {
                APPEND_FMT("Staged changes:\n");
                has_staged = true;
            }
            char st = status->files[i].index_status;
            const char *desc = "modified";
            if (st == 'A') desc = "new file";
            else if (st == 'D') desc = "deleted";
            else if (st == 'R') desc = "renamed";
            APPEND_FMT("  %s:   %s\n", desc, status->files[i].path);
        }
    }
    if (has_staged) APPEND_FMT("\n");

    /* Unstaged changes */
    bool has_unstaged = false;
    for (int i = 0; i < status->count; i++) {
        if (status->files[i].work_status != ' ' && status->files[i].work_status != '?') {
            if (!has_unstaged) {
                APPEND_FMT("Unstaged changes:\n");
                has_unstaged = true;
            }
            char st = status->files[i].work_status;
            const char *desc = "modified";
            if (st == 'D') desc = "deleted";
            APPEND_FMT("  %s:   %s\n", desc, status->files[i].path);
        }
    }
    if (has_unstaged) APPEND_FMT("\n");

    /* Untracked files */
    bool has_untracked = false;
    for (int i = 0; i < status->count; i++) {
        if (status->files[i].index_status == '?' && status->files[i].work_status == '?') {
            if (!has_untracked) {
                APPEND_FMT("Untracked files:\n");
                has_untracked = true;
            }
            APPEND_FMT("  %s\n", status->files[i].path);
        }
    }

    if (!has_staged && !has_unstaged && !has_untracked) {
        APPEND_FMT("Nothing to commit, working tree clean\n");
    }

    #undef APPEND_FMT
    buf[len] = '\0';
    return buf;
}

void git_stage_file(const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git add '%s' 2>&1", path);
    char *output = git_run(cmd);
    if (output && output[0]) {
        message("git add: %s", output);
    }
    free(output);
}

void git_unstage_file(const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git restore --staged '%s' 2>&1", path);
    char *output = git_run(cmd);
    if (output && output[0]) {
        message("git restore: %s", output);
    }
    free(output);
}

void git_commit(const char *msg) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git commit -m '%s' 2>&1", msg);
    char *output = git_run(cmd);
    if (output) {
        message("%s", output);
    }
    free(output);
}

char *git_diff_file(const char *path, bool cached) {
    char cmd[1024];
    const char *flag = cached ? " --cached" : "";
    if (path) {
        snprintf(cmd, sizeof(cmd), "git diff%s '%s' 2>/dev/null", flag, path);
    } else {
        snprintf(cmd, sizeof(cmd), "git diff%s 2>/dev/null", flag);
    }
    return git_run(cmd);
}

char *git_log(int count) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "git log --oneline -%d 2>/dev/null", count);
    return git_run(cmd);
}

char *git_file_at_point(Buffer *buf) {
    int line_num, col;
    buffer_point_to_line_col(buf, buf->point, &line_num, &col);
    char *line_text = buffer_line_text(buf, line_num);
    if (!line_text) return NULL;

    /* Parse file path from status line format "  desc:   path" */
    char *colon = strchr(line_text, ':');
    if (colon) {
        char *path = colon + 1;
        while (*path == ' ') path++;
        /* Trim trailing whitespace */
        char *end = path + strlen(path) - 1;
        while (end > path && (*end == ' ' || *end == '\n')) *end-- = '\0';
        char *result = hstrdup(path);
        free(line_text);
        return result;
    }

    /* Try bare filename (untracked section) */
    char *path = line_text;
    while (*path == ' ') path++;
    if (*path && *path != '\n') {
        char *end = path + strlen(path) - 1;
        while (end > path && (*end == ' ' || *end == '\n')) *end-- = '\0';
        char *result = hstrdup(path);
        free(line_text);
        return result;
    }

    free(line_text);
    return NULL;
}

/* Git commands moved to clj/commands.clj */
