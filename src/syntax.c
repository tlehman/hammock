/* All syntax highlighting logic has moved to font_lock.c.
 * syntax_highlight_line and syntax_detect are defined there.
 * This translation unit is kept (rather than the file deleted) so the
 * Makefile wildcard glob for src does not have to change. */
#include "syntax.h"
