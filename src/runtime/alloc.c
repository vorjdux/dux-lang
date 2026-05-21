#include "duxrt.h"
#include <stdlib.h>
#include <stdio.h>

void duxrt_assert_fail(const char* file, int line, const char* msg) {
    fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, msg);
    abort();
}
