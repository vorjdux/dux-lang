/*
 * path_rt.c — path manipulation runtime support for Dux stdlib io.path
 */
#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

DuxStr* duxrt_fs_realpath(DuxStr* path) {
    if (!path) return duxrt_str_new("", 0);
    char buf[PATH_MAX];
    const char* p = duxrt_str_cstr(path);
    char* r = realpath(p, buf);
    if (!r) return duxrt_str_new("", 0);
    return duxrt_str_new(r, (int64_t)strlen(r));
}

DuxStr* duxrt_fs_getcwd(void) {
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) return duxrt_str_new("", 0);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}
