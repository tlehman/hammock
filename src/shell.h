#ifndef SHELL_H
#define SHELL_H

#include "buffer.h"
#include <sys/types.h>

/* Shell process info */
typedef struct {
    pid_t pid;
    int master_fd;    /* PTY master file descriptor */
    Buffer *buffer;
} ShellProcess;

#define MAX_SHELLS 8
extern ShellProcess shells[MAX_SHELLS];
extern int shell_count;

/* Start an interactive shell in a new buffer */
void shell_start(void);

/* Run a single shell command, output to buffer */
void shell_command(const char *cmd);

/* Read pending output from all shell processes (non-blocking) */
void shell_read_all(void);

/* Send string to the shell process associated with buffer */
void shell_send(Buffer *buf, const char *str, size_t len);

/* Check if buffer has an associated shell */
ShellProcess *shell_for_buffer(Buffer *buf);

/* Register shell commands */
void shell_commands_init(void);

#endif
