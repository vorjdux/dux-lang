#include "duxrt.h"
#include <stdlib.h>
#include <stdio.h>

void* duxrt_alloc(int64_t size) {
    if (size <= 0) return NULL;
    void* p = malloc((size_t)size);
    if (!p) {
        fputs("duxrt: out of memory\n", stderr);
        abort();
    }
    return p;
}

void duxrt_free(void* ptr) {
    free(ptr);
}

void duxrt_assert_fail(const char* file, int line, const char* msg) {
    fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, msg);
    abort();
}
