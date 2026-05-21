#include "duxrt.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Exception object ────────────────────────────────────────────────────── */

DuxException* duxrt_exception_new(const char* type, const char* msg) {
    DuxException* e = (DuxException*)malloc(sizeof(DuxException));
    if (!e) abort();
    e->type_name = type ? type : "Exception";
    e->message   = msg  ? strdup(msg) : NULL;
    e->line      = 0;
    e->file      = NULL;
    return e;
}

void duxrt_exception_free(DuxException* e) {
    if (!e) return;
    free(e->message);
    free(e);
}

/* ── Try/catch stack (setjmp-based) ──────────────────────────────────────── */

#define MAX_TRY_DEPTH 256

typedef struct {
    jmp_buf       env;
    DuxException* exception;
} TryFrame;

static TryFrame  try_stack[MAX_TRY_DEPTH];
static int       try_depth = 0;

/*
 * Call at the start of a try block.
 * First call: pushes a frame, returns 0 (in try body).
 * After duxrt_throw: setjmp returns non-zero, we pop the frame and return 1
 * (in catch body); *exception_out receives the thrown exception.
 */
int duxrt_try_enter(void** exception_out) {
    if (try_depth >= MAX_TRY_DEPTH) {
        fputs("duxrt: try stack overflow\n", stderr);
        abort();
    }
    try_stack[try_depth].exception = NULL;
    int r = setjmp(try_stack[try_depth].env);
    if (r != 0) {
        /* We longjmp'd here — catch path */
        if (exception_out)
            *exception_out = try_stack[try_depth].exception;
        /* don't decrement: duxrt_throw already incremented then jumped back */
        return 1;
    }
    try_depth++;
    return 0; /* try path */
}

/* Call at the end of the try body (before reaching catch) */
void duxrt_try_exit(void) {
    if (try_depth > 0) try_depth--;
}

/* Throw — longjmp to the nearest try frame */
void duxrt_throw(DuxException* e) {
    if (try_depth == 0) {
        fprintf(stderr, "Unhandled exception: %s: %s\n",
                e ? e->type_name : "Exception",
                e && e->message ? e->message : "");
        abort();
    }
    try_depth--;
    try_stack[try_depth].exception = e;
    longjmp(try_stack[try_depth].env, 1);
}
