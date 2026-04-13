#ifndef HAMMOCK_PAREN_H
#define HAMMOCK_PAREN_H

#include <stddef.h>
#include <sys/types.h>
#include "buffer.h"

#define PAREN_FLASH_MS 1000

/* If buf is in Clojure mode and the byte at closer_pos is )/]/},
 * find the matching opener (string/comment/char-literal aware).
 * On match: if visible, set a 1s display flash; else echo the match line
 * in the minibuffer. On no-match: silent. */
void paren_flash_check(Buffer *buf, size_t closer_pos);

/* Pure scanner exposed for unit testing. Returns opener offset in s, or -1. */
ssize_t paren_scan_string(const char *s, size_t len, size_t closer_pos);

#endif
