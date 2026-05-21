#include "duxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void duxrt_println_int(int64_t v)    { printf("%ld\n",  (long)v); }
void duxrt_println_double(double v)  { printf("%g\n",   v); }
void duxrt_println_str(const char* s){ puts(s ? s : "(null)"); }

void duxrt_print_int(int64_t v)      { printf("%ld",    (long)v); }
void duxrt_print_double(double v)    { printf("%g",     v); }
void duxrt_print_str(const char* s)  { fputs(s ? s : "(null)", stdout); }

char* duxrt_readline(void) {
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') buf[--n] = '\0';
    char* out = (char*)malloc(n + 1);
    if (!out) abort();
    memcpy(out, buf, n + 1);
    return out;
}
