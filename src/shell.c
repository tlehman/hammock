#include "shell.h"
#include "command.h"
#include "window.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <util.h>  /* macOS forkpty */
#include <poll.h>

ShellProcess shells[MAX_SHELLS];
int shell_count = 0;

void shell_start(void) {
    if (shell_count >= MAX_SHELLS) {
        message("Too many shell processes");
        return;
    }

    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);

    if (pid < 0) {
        message("Failed to create PTY");
        return;
    }

    if (pid == 0) {
        /* Child: start shell */
        const char *sh = getenv("SHELL");
        if (!sh) sh = "/bin/zsh";
        execlp(sh, sh, "--login", "-i", NULL);
        _exit(1);
    }

    /* Parent: set non-blocking */
    int flags = fcntl(master_fd, F_GETFL, 0);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

    /* Create shell buffer */
    char name[64];
    snprintf(name, sizeof(name), "*shell-%d*", shell_count);
    Buffer *buf = buffer_create(name);
    buf->major_mode = 6;  /* MODE_SHELL */

    shells[shell_count].pid = pid;
    shells[shell_count].master_fd = master_fd;
    shells[shell_count].buffer = buf;
    shell_count++;

    current_window->buffer = buf;
    current_buffer = buf;
    message("Shell started");
}

void shell_command(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        message("Failed to run command");
        return;
    }

    /* Collect output */
    size_t cap = 4096;
    size_t len = 0;
    char *output = hmalloc(cap);

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        size_t llen = strlen(line);
        if (len + llen >= cap) {
            cap *= 2;
            output = hrealloc(output, cap);
        }
        memcpy(output + len, line, llen);
        len += llen;
    }
    output[len] = '\0';
    pclose(fp);

    /* Trim trailing newline for minibuffer display */
    while (len > 0 && (output[len - 1] == '\n' || output[len - 1] == '\r'))
        output[--len] = '\0';

    if (len == 0) {
        message("(Shell command completed with no output)");
    } else if (strchr(output, '\n') == NULL && len < 200) {
        /* Single short line: show in minibuffer */
        message("%s", output);
    } else {
        /* Multi-line output: show in a buffer */
        Buffer *buf = buffer_find("*Shell Command Output*");
        if (!buf) {
            buf = buffer_create("*Shell Command Output*");
        } else {
            buf->read_only = false;
            buf->point = 0;
            while (buffer_length(buf) > 0)
                buffer_delete_forward(buf);
        }
        buffer_insert_string(buf, output, len);
        buf->point = 0;
        buf->modified = false;
        buf->read_only = true;

        Window *new_win = window_split_below(current_window);
        if (new_win) {
            new_win->buffer = buf;
            current_window = new_win;
            current_buffer = buf;
        } else {
            current_window->buffer = buf;
            current_buffer = buf;
        }
    }
    free(output);
}

void shell_read_all(void) {
    for (int i = 0; i < shell_count; i++) {
        if (shells[i].master_fd < 0) continue;

        char buf[4096];
        ssize_t n;
        while ((n = read(shells[i].master_fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';

            /* Strip ANSI escape sequences (basic) */
            Buffer *sbuf = shells[i].buffer;
            sbuf->point = buffer_length(sbuf);

            for (ssize_t j = 0; j < n; j++) {
                if (buf[j] == '\033') {
                    /* Skip escape sequence */
                    j++;
                    if (j < n && buf[j] == '[') {
                        j++;
                        while (j < n && buf[j] != 'm' && buf[j] != 'H' &&
                               buf[j] != 'J' && buf[j] != 'K' && buf[j] != 'A' &&
                               buf[j] != 'B' && buf[j] != 'C' && buf[j] != 'D')
                            j++;
                    }
                } else if (buf[j] == '\r') {
                    /* Carriage return: move to start of line */
                    continue;
                } else if (buf[j] == '\b') {
                    /* Backspace */
                    if (sbuf->point > 0)
                        buffer_delete_backward(sbuf);
                } else {
                    buffer_insert_char(sbuf, buf[j]);
                }
            }
            sbuf->modified = false;
            need_redisplay = true;
        }

        /* Check if process has exited */
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            int status;
            if (waitpid(shells[i].pid, &status, WNOHANG) > 0) {
                close(shells[i].master_fd);
                shells[i].master_fd = -1;
                Buffer *sbuf = shells[i].buffer;
                sbuf->point = buffer_length(sbuf);
                const char *msg = "\n[Process finished]\n";
                buffer_insert_string(sbuf, msg, strlen(msg));
                sbuf->modified = false;
            }
        }
    }
}

void shell_send(Buffer *buf, const char *str, size_t len) {
    ShellProcess *sp = shell_for_buffer(buf);
    if (sp && sp->master_fd >= 0) {
        write(sp->master_fd, str, len);
    }
}

ShellProcess *shell_for_buffer(Buffer *buf) {
    for (int i = 0; i < shell_count; i++) {
        if (shells[i].buffer == buf)
            return &shells[i];
    }
    return NULL;
}

/* Command wrappers */
static void cmd_shell(void) {
    shell_start();
}

static void cmd_shell_command(void) {
    char *cmd = minibuffer_read("Shell command: ", NULL);
    if (!cmd || cmd[0] == '\0') { message("Quit"); return; }
    shell_command(cmd);
}

void shell_commands_init(void) {
    command_register("shell", cmd_shell, "Start interactive shell");
    command_register("shell-command", cmd_shell_command, "Run shell command");
}
