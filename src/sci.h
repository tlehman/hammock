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

/* Result of an interruptible eval. */
typedef struct {
    char *result;     /* eval result string (caller frees), NULL if cancelled */
    bool cancelled;   /* true if user pressed C-g during evaluation */
} SciEvalResult;

/* Evaluate a Clojure expression in a forked child process so the user can
 * cancel it with C-g. The parent polls both the result pipe and stdin; on C-g
 * (byte 7 or ESC) the child is killed with SIGKILL. The parent's SCI isolate
 * is untouched by the child, so editor state survives cancellation.
 *
 * While this function blocks, the caller must not expect any other input or
 * display updates to happen — it owns stdin and draws its own minibuffer. */
SciEvalResult sci_eval_interruptible(const char *expr);

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
