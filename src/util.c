#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void *hmalloc(size_t size) {
    void *p = malloc(size);
    if (!p) {
        fprintf(stderr, "hammock: out of memory\n");
        abort();
    }
    return p;
}

void *hrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && size > 0) {
        fprintf(stderr, "hammock: out of memory\n");
        abort();
    }
    return p;
}

char *hstrdup(const char *s) {
    if (!s) return NULL;
    char *p = strdup(s);
    if (!p) {
        fprintf(stderr, "hammock: out of memory\n");
        abort();
    }
    return p;
}

char *hstrndup(const char *s, size_t n) {
    if (!s) return NULL;
    char *p = hmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* Kill ring */

void kill_ring_init(KillRing *kr) {
    memset(kr, 0, sizeof(*kr));
}

void kill_ring_push(KillRing *kr, const char *text) {
    if (!text) return;
    int idx = kr->head;
    free(kr->entries[idx]);
    kr->entries[idx] = hstrdup(text);
    kr->head = (kr->head + 1) % KILL_RING_SIZE;
    if (kr->count < KILL_RING_SIZE) kr->count++;
}

const char *kill_ring_top(KillRing *kr) {
    if (kr->count == 0) return NULL;
    int idx = (kr->head - 1 + KILL_RING_SIZE) % KILL_RING_SIZE;
    return kr->entries[idx];
}

void kill_ring_free(KillRing *kr) {
    for (int i = 0; i < KILL_RING_SIZE; i++) {
        free(kr->entries[i]);
        kr->entries[i] = NULL;
    }
    kr->count = 0;
    kr->head = 0;
}

/* Undo system */

void undo_init(UndoList *ul) {
    ul->head = NULL;
}

static void undo_push(UndoList *ul, UndoType type, size_t pos,
                       const char *text, size_t len) {
    UndoEntry *e = hmalloc(sizeof(UndoEntry));
    e->type = type;
    e->pos = pos;
    e->text = text ? hstrndup(text, len) : NULL;
    e->len = len;
    e->next = ul->head;
    ul->head = e;
}

void undo_record_insert(UndoList *ul, size_t pos, const char *text, size_t len) {
    undo_push(ul, UNDO_INSERT, pos, text, len);
}

void undo_record_delete(UndoList *ul, size_t pos, const char *text, size_t len) {
    undo_push(ul, UNDO_DELETE, pos, text, len);
}

void undo_add_boundary(UndoList *ul) {
    undo_push(ul, UNDO_BOUNDARY, 0, NULL, 0);
}

UndoEntry *undo_pop(UndoList *ul) {
    if (!ul->head) return NULL;
    UndoEntry *e = ul->head;
    ul->head = e->next;
    e->next = NULL;
    return e;
}

void undo_entry_free(UndoEntry *e) {
    if (e) {
        free(e->text);
        free(e);
    }
}

void undo_free(UndoList *ul) {
    UndoEntry *e = ul->head;
    while (e) {
        UndoEntry *next = e->next;
        undo_entry_free(e);
        e = next;
    }
    ul->head = NULL;
}

/* System clipboard */

typedef enum { CLIP_NONE, CLIP_MACOS, CLIP_WAYLAND, CLIP_X11 } ClipBackend;

static ClipBackend clip_backend = CLIP_NONE;
static bool clip_detected = false;

static bool command_exists(const char *cmd) {
    char buf[256];
    snprintf(buf, sizeof(buf), "which %s >/dev/null 2>&1", cmd);
    return system(buf) == 0;
}

static void clipboard_detect(void) {
    if (clip_detected) return;
    clip_detected = true;
    if (command_exists("pbcopy"))       clip_backend = CLIP_MACOS;
    else if (command_exists("wl-copy")) clip_backend = CLIP_WAYLAND;
    else if (command_exists("xclip"))   clip_backend = CLIP_X11;
}

void clipboard_copy(const char *text) {
    clipboard_detect();
    if (!text || clip_backend == CLIP_NONE) return;
    const char *cmd;
    switch (clip_backend) {
        case CLIP_MACOS:   cmd = "pbcopy"; break;
        case CLIP_WAYLAND: cmd = "wl-copy"; break;
        case CLIP_X11:     cmd = "xclip -selection clipboard"; break;
        default: return;
    }
    FILE *fp = popen(cmd, "w");
    if (fp) { fwrite(text, 1, strlen(text), fp); pclose(fp); }
}

char *clipboard_paste(void) {
    clipboard_detect();
    if (clip_backend == CLIP_NONE) return NULL;
    const char *cmd;
    switch (clip_backend) {
        case CLIP_MACOS:   cmd = "pbpaste"; break;
        case CLIP_WAYLAND: cmd = "wl-paste --no-newline 2>/dev/null"; break;
        case CLIP_X11:     cmd = "xclip -selection clipboard -o"; break;
        default: return NULL;
    }
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    size_t cap = 4096, len = 0;
    char *buf = hmalloc(cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (len + 1 >= cap) { cap *= 2; buf = hrealloc(buf, cap); }
    }
    pclose(fp);
    buf[len] = '\0';
    if (len == 0) { free(buf); return NULL; }
    return buf;
}

/* String utilities */

bool str_ends_with(const char *s, const char *suffix) {
    size_t slen = strlen(s);
    size_t suflen = strlen(suffix);
    if (suflen > slen) return false;
    return strcmp(s + slen - suflen, suffix) == 0;
}

char *path_join(const char *dir, const char *file) {
    size_t dlen = strlen(dir);
    size_t flen = strlen(file);
    char *p = hmalloc(dlen + 1 + flen + 1);
    memcpy(p, dir, dlen);
    if (dlen > 0 && dir[dlen - 1] != '/') {
        p[dlen] = '/';
        memcpy(p + dlen + 1, file, flen + 1);
    } else {
        memcpy(p + dlen, file, flen + 1);
    }
    return p;
}
