#ifndef MARKDOWN_H
#define MARKDOWN_H

#include "buffer.h"

/* Link types found in markdown */
typedef enum {
    LINK_NONE,
    LINK_MARKDOWN,    /* [text](url) */
    LINK_BIDIR,       /* [[target]] */
} LinkType;

typedef struct {
    LinkType type;
    int start;          /* start column in line */
    int end;            /* end column in line */
    char *text;         /* display text */
    char *target;       /* URL or target name */
} MarkdownLink;

/* Find link at position in buffer */
MarkdownLink markdown_link_at_point(Buffer *buf);

/* Free a link struct */
void markdown_link_free(MarkdownLink *link);

#endif
