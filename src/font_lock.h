#ifndef FONT_LOCK_H
#define FONT_LOCK_H

#include "syntax.h"

/* Multi-line state bits carried between lines. Bit layout matches the old
 * src/syntax.c so state round-trips unchanged through display.c. */
#define FL_STATE_NORMAL        0
#define FL_STATE_BLOCK_COMMENT 1
#define FL_STATE_STRING        2
#define FL_STATE_CODE_FENCE    4
#define FL_FENCE_LANG_SHIFT    3
#define FL_FENCE_LANG_MASK     0x38  /* bits 3-5 */

/* Reload compiled mode tables from Clojure's hammock.syntax-modes/export.
 * Called at startup after modes_load_edn, and on config-version bumps. */
void font_lock_reload(void);

/* Release all compiled tables. */
void font_lock_shutdown(void);

/* Install the mode table from an EDN string. Called by font_lock_reload after
 * fetching (hammock.syntax-modes/export) from SCI; also used by the test
 * harness to inject hand-built tables without SCI. Returns 0 on success,
 * -1 if the EDN cannot be parsed. On parse failure the previous table is
 * preserved. */
int font_lock_load_from_edn_string(const char *edn_text);

/* Highlight a line against a named mode (looked up by :name in the mode
 * table). Returns an empty LineHighlight if the name is unknown. Used by
 * syntax_highlight_line (which maps SyntaxLang -> name) and by the test
 * harness. */
LineHighlight font_lock_highlight_named(const char *mode_name,
                                        const char *line, int len,
                                        int state_in);

/* Highlight a line via the legacy SyntaxLang enum. Wraps
 * font_lock_highlight_named with a lang -> mode-name table. Returns an empty
 * LineHighlight if the language has no registered mode. */
LineHighlight font_lock_highlight_by_lang(SyntaxLang lang, const char *line,
                                          int len, int state_in);

#endif
