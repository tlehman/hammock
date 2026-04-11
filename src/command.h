#ifndef COMMAND_H
#define COMMAND_H

#include <stdbool.h>

typedef void (*CommandFn)(void);

typedef enum {
    CMD_C_NATIVE,   /* Dispatch via C function pointer (zero latency) */
    CMD_CLOJURE,    /* Dispatch via SCI eval (effect vector return) */
} CommandDispatch;

typedef struct {
    const char *name;
    CommandFn fn;              /* C function pointer, NULL for CMD_CLOJURE */
    const char *docstring;     /* Human-readable documentation */
    const char *source;        /* "C" or "Clojure" */
    CommandDispatch dispatch;
} CommandEntry;

#define MAX_COMMANDS 256

extern CommandEntry command_table[MAX_COMMANDS];
extern int command_count;

void command_register(const char *name, CommandFn fn, const char *docstring);
void command_register_clojure(const char *name, const char *docstring);
CommandEntry *command_find(const char *name);
CommandFn command_lookup(const char *name);
void command_execute(const char *name);
void command_dispatch(const char *name, bool clj_available);

/* Register all built-in commands */
void commands_init(void);

/* Completion function type */
typedef int (*CompletionFn)(const char *input, const char **candidates, int max_candidates);

/* Minibuffer input with optional tab completion */
char *minibuffer_read(const char *prompt, CompletionFn complete);

/* Completion functions */
int complete_file_name(const char *input, const char **candidates, int max_cand);
int complete_buffer_name(const char *input, const char **candidates, int max_cand);
int complete_command_name(const char *input, const char **candidates, int max_cand);

/* Message to display in minibuffer */
extern char minibuf_message[256];
void message(const char *fmt, ...);

/* *Messages* buffer: append-only log of every call to message().
 * Set by main() after buffer subsystem is ready. NULL until then. */
struct Buffer;
extern struct Buffer *messages_buffer;
#define MESSAGES_CAP 1000

/* Global editor state */
extern bool editor_running;
extern bool need_redisplay;

#endif
