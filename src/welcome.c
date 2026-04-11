#include "buffer.h"
#include "window.h"
#include "command.h"
#include "mode.h"
#include "welcome.h"
#include "hammock_logo_data.h"
#include <stdio.h>
#include <string.h>

extern bool need_redisplay;

static const char *WELCOME_TEXT =
    "Welcome to Hammock: A terminal text editor for deep thinking.\n"
    "Inspired by Rich Hickey's Hammock Driven Development.\n"
    "\n"
    "Minimal C kernel. Clojure scripting. Markdown-native.\n"
    "Uses SCI/GraalVM to get Clojure semantics without any\n"
    "runtime JVM dependencies.\n"
    "\n";

static const char *LINKS_TEXT =
    "\n"
    "## Get started\n"
    "\n"
    "- [Browse symbols](hammock://command/browse-symbols)\n"
    "- [Open *scratch* buffer](hammock://buffer/*scratch*)\n"
    "- [What's new](hammock://command/view-news)\n";

static Buffer *build_welcome_buffer(void) {
    Buffer *buf = buffer_find("*Hammock*");
    if (!buf) {
        buf = buffer_create("*Hammock*");
    } else {
        buf->read_only = false;
        buffer_delete_region(buf, 0, buffer_length(buf));
    }

    char header[64];
    int header_len = snprintf(header, sizeof(header),
                              "Hammock %s\n\n", HAMMOCK_VERSION);
    buffer_insert_string(buf, header, (size_t)header_len);
    buffer_insert_string(buf, WELCOME_TEXT, strlen(WELCOME_TEXT));
    buffer_insert_string(buf, (const char *)hammock_logo_txt,
                         (size_t)hammock_logo_txt_len);
    buffer_insert_string(buf, LINKS_TEXT, strlen(LINKS_TEXT));

    buf->point = 0;
    buf->modified = false;
    buf->read_only = true;
    buffer_set_mode(buf, MODE_MARKDOWN);

    return buf;
}

static void cmd_welcome(void) {
    Buffer *buf = build_welcome_buffer();
    current_window->buffer = buf;
    current_buffer = buf;
    need_redisplay = true;
}

Buffer *welcome_init(void) {
    command_register("welcome", cmd_welcome,
                     "Show the Hammock welcome buffer.");
    return build_welcome_buffer();
}
