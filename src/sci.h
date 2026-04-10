#ifndef SCI_H
#define SCI_H

#include <stdbool.h>
#include <stddef.h>

/* Initialize the embedded SCI interpreter (creates GraalVM isolate + SCI context) */
bool sci_init(void);

/* Shut down the SCI interpreter */
void sci_shutdown(void);

/* Evaluate a Clojure expression, returns result string (caller frees with free()) */
char *sci_eval(const char *expr);

/* Check if the SCI interpreter is initialized and ready */
bool sci_is_ready(void);

/* Load and evaluate a Clojure file, returns result string (caller frees) */
char *sci_load_file(const char *path);

/* Get the state version counter (incremented by atom watches) */
long long sci_get_state_version(void);

/* ---- Sexp utilities (moved from clojure.c, pure string parsing) ---- */

/* Find the sexp ending at or before the given position in a buffer.
 * Returns start position of the sexp, or (size_t)-1 if not found. */
size_t clojure_find_sexp_start(const char *text, size_t pos);

/* Extract sexp ending at pos from text */
char *clojure_extract_sexp(const char *text, size_t pos);

/* Check if character is a Clojure symbol constituent */
bool clojure_is_symbol_char(char ch);

#endif
