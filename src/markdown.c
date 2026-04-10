#include "markdown.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>

void markdown_link_free(MarkdownLink *link) {
    if (link) {
        free(link->text);
        free(link->target);
        link->text = NULL;
        link->target = NULL;
    }
}

MarkdownLink markdown_link_at_point(Buffer *buf) {
    MarkdownLink link = {.type = LINK_NONE};

    char *line_text = NULL;
    int line_num, col;
    buffer_point_to_line_col(buf, buf->point, &line_num, &col);
    line_text = buffer_line_text(buf, line_num);
    if (!line_text) return link;

    int len = (int)strlen(line_text);

    /* Search for [[bidir]] links */
    for (int i = 0; i < len - 3; i++) {
        if (line_text[i] == '[' && line_text[i + 1] == '[') {
            int start = i;
            int j = i + 2;
            while (j < len - 1 && !(line_text[j] == ']' && line_text[j + 1] == ']'))
                j++;
            if (j < len - 1) {
                int end = j + 2;
                if (col >= start && col < end) {
                    link.type = LINK_BIDIR;
                    link.start = start;
                    link.end = end;
                    link.target = hstrndup(line_text + i + 2, (size_t)(j - i - 2));
                    link.text = hstrdup(link.target);
                    free(line_text);
                    return link;
                }
            }
        }
    }

    /* Search for [text](url) links */
    for (int i = 0; i < len; i++) {
        if (line_text[i] == '[' && (i == 0 || line_text[i - 1] != '[')) {
            int text_start = i + 1;
            int j = text_start;
            while (j < len && line_text[j] != ']') j++;
            if (j >= len || j + 1 >= len || line_text[j + 1] != '(') continue;

            int url_start = j + 2;
            int k = url_start;
            while (k < len && line_text[k] != ')') k++;
            if (k >= len) continue;

            int start = i;
            int end = k + 1;
            if (col >= start && col < end) {
                link.type = LINK_MARKDOWN;
                link.start = start;
                link.end = end;
                link.text = hstrndup(line_text + text_start, (size_t)(j - text_start));
                link.target = hstrndup(line_text + url_start, (size_t)(k - url_start));
                free(line_text);
                return link;
            }
        }
    }

    free(line_text);
    return link;
}

/* Markdown commands moved to clj/commands.clj */
