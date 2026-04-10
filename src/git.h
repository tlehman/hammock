#ifndef GIT_H
#define GIT_H

#include "buffer.h"

/* Git file status */
typedef struct {
    char index_status;   /* status in index (staged) */
    char work_status;    /* status in working tree */
    char path[512];
} GitFileStatus;

#define MAX_GIT_FILES 512

typedef struct {
    GitFileStatus files[MAX_GIT_FILES];
    int count;
    char branch[128];
} GitStatus;

/* Parse git status output */
GitStatus git_parse_status(void);

/* Format git status into buffer content */
char *git_format_status(GitStatus *status);

/* Git operations */
void git_stage_file(const char *path);
void git_unstage_file(const char *path);
void git_commit(const char *message);
char *git_diff_file(const char *path, bool cached);
char *git_log(int count);

/* Get the file path at the current line in git-status buffer */
char *git_file_at_point(Buffer *buf);

#endif
