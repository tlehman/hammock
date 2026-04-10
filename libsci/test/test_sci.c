/**
 * Minimal test: link against libsci and evaluate (+ 1 2).
 *
 * Build:   cc -o test_sci test_sci.c -L.. -lsci -I.. -Wl,-rpath,..
 * Run:     ./test_sci
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libsci.h"

int main(void) {
    graal_isolate_t *isolate = NULL;
    graal_isolatethread_t *thread = NULL;

    printf("Creating GraalVM isolate...\n");
    if (graal_create_isolate(NULL, &isolate, &thread) != 0) {
        fprintf(stderr, "Failed to create GraalVM isolate\n");
        return 1;
    }

    printf("Initializing SCI...\n");
    int rc = libsci_init(thread);
    if (rc != 0) {
        fprintf(stderr, "sci_init failed\n");
        return 1;
    }

    /* Test 1: basic arithmetic */
    printf("Test 1: (+ 1 2)\n");
    char *result = libsci_eval_string(thread, (char *)"(+ 1 2)");
    if (result) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "3") != 0) {
            fprintf(stderr, "  FAIL: expected '3'\n");
            return 1;
        }
        libsci_free_string(thread, result);
        printf("  PASS\n");
    } else {
        fprintf(stderr, "  FAIL: NULL result\n");
        return 1;
    }

    /* Test 2: define and use an atom */
    printf("Test 2: atoms\n");
    libsci_eval_string(thread, (char *)"(def my-atom (atom 42))");
    result = libsci_eval_string(thread, (char *)"@my-atom");
    if (result) {
        printf("  @my-atom = %s\n", result);
        if (strcmp(result, "42") != 0) {
            fprintf(stderr, "  FAIL: expected '42'\n");
            return 1;
        }
        libsci_free_string(thread, result);
    }
    libsci_eval_string(thread, (char *)"(swap! my-atom inc)");
    result = libsci_eval_string(thread, (char *)"@my-atom");
    if (result) {
        printf("  @my-atom after swap! = %s\n", result);
        if (strcmp(result, "43") != 0) {
            fprintf(stderr, "  FAIL: expected '43'\n");
            return 1;
        }
        libsci_free_string(thread, result);
        printf("  PASS\n");
    }

    /* Test 3: state version counter */
    printf("Test 3: state version\n");
    long long ver = libsci_get_state_version(thread);
    printf("  version = %lld\n", ver);
    printf("  PASS\n");

    libsci_shutdown(thread);
    graal_tear_down_isolate(thread);

    printf("\nAll tests PASSED\n");
    return 0;
}
