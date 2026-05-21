#include "duxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void duxrt_println_int(int64_t v)    { printf("%ld\n", (long)v); }
void duxrt_println_double(double v)  { printf("%g\n",  v); }
void duxrt_println_str(DuxStr* s)    { puts(s ? s->data : "(null)"); }

void duxrt_print_int(int64_t v)      { printf("%ld",   (long)v); }
void duxrt_print_double(double v)    { printf("%g",    v); }
void duxrt_print_str(DuxStr* s)      { fputs(s ? s->data : "(null)", stdout); }

DuxStr* duxrt_readline(void) {
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return duxrt_str_new("", 0);
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') buf[--n] = '\0';
    return duxrt_str_new(buf, (int64_t)n);
}
