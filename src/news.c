#include "buffer.h"
#include "window.h"
#include "command.h"
#include "mode.h"
#include "news.h"
#include "news_data.h"

/* xxd -i NEWS.md generates:
 *   unsigned char NEWS_md[] = { ... };
 *   unsigned int NEWS_md_len = ...;
 */

static void cmd_view_news(void) {
    Buffer *buf = buffer_find("*NEWS*");
    if (!buf) {
        buf = buffer_create("*NEWS*");
    } else {
        buf->read_only = false;
        buffer_delete_region(buf, 0, buffer_length(buf));
    }

    buffer_insert_string(buf, (const char *)NEWS_md, (size_t)NEWS_md_len);
    buf->point = 0;
    buf->modified = false;
    buf->read_only = true;
    buffer_set_mode(buf, "Markdown");

    current_window->buffer = buf;
    current_buffer = buf;
    need_redisplay = true;
}

void news_init(void) {
    command_register("view-news", cmd_view_news,
                     "Display Hammock release notes.");
}
